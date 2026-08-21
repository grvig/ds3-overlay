// Checks that don't need the game running. Build and run with test.bat.
//
// Two things are covered:
//   1. The boss and bonfire lists are well-formed (no duplicate flags, no
//      empty names, areas grouped together).
//   2. The batch flag-reading code can never overwrite itself while it runs.
//      That overlap corrupted the game's own thread and closed the game
//      mid-session twice, so it's worth a permanent guard.
#include "ds3reader.h"
#include "tracked.h"
#include "layout.h"
#include "datafile.h"
#include "missable.h"

#include <iostream>
#include <set>
#include <string>
#include <vector>
#include <cstring>

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
    if (!ok) {
        std::cout << "  FAIL: " << what << std::endl;
        g_failures++;
    }
}

// Shared shape checks for any tracked list.
static void CheckList(const std::string& label, const std::vector<TrackedEntry>& entries) {
    Check(!entries.empty(), label + ": list is empty");

    std::set<uint32_t> seenFlags;
    for (size_t i = 0; i < entries.size(); i++) {
        Check(seenFlags.insert(entries[i].flag).second,
              label + ": duplicate flag " + std::to_string(entries[i].flag));
        Check(!entries[i].name.empty(), label + ": empty name at " + std::to_string(i));
        Check(!entries[i].group.empty(), label + ": empty group at " + std::to_string(i));
        // DS3 groups its event flags into bands. Bosses and bonfires live in
        // 13-15 million, questline reward lots in 50 million, and a handful
        // of NPC shop lots (Patches' Catarina set) in 73 million. Anything
        // outside all three is a typo rather than a real flag.
        uint32_t flag = entries[i].flag;
        bool worldFlag = flag >= 13000000 && flag <= 15999999;
        bool questLot  = flag >= 50000000 && flag <= 50999999;
        bool shopLot   = flag >= 73000000 && flag <= 73999999;
        Check(worldFlag || questLot || shopLot,
              label + ": flag " + std::to_string(flag) + " out of range at " + std::to_string(i));
    }

    // The overlay prints a header whenever the group changes, so a group that
    // reappears later would get a second header.
    std::set<std::wstring> closed;
    std::wstring current;
    bool haveCurrent = false;
    int groups = 0;
    for (size_t i = 0; i < entries.size(); i++) {
        if (!haveCurrent || entries[i].group != current) {
            if (haveCurrent) closed.insert(current);
            current = entries[i].group;
            haveCurrent = true;
            groups++;
            Check(closed.count(current) == 0, label + ": group is not contiguous");
        }
    }

    std::cout << "  " << label << ": " << entries.size() << " entries, "
              << groups << " groups, " << seenFlags.size() << " unique flags" << std::endl;
}

static void TestDataLists() {
    std::cout << "data lists:" << std::endl;

    // Anything the loader complained about is a test failure - a data file
    // typo should fail here rather than show up as a missing entry in game.
    for (size_t i = 0; i < g_tracked.problems.size(); i++) {
        Check(false, g_tracked.problems[i]);
    }

    CheckList("bosses", g_tracked.bosses);
    CheckList("bonfires", g_tracked.bonfires);
    CheckList("quests", g_tracked.quests);

    // Entries can share a name across lists ("Iudex Gundyr" is both a boss
    // and a bonfire) but never a flag - those mean different things.
    std::set<uint32_t> seen;
    const std::vector<TrackedEntry>* lists[] = {
        &g_tracked.bosses, &g_tracked.bonfires, &g_tracked.quests
    };
    for (auto* list : lists) {
        for (size_t i = 0; i < list->size(); i++) {
            Check(seen.insert((*list)[i].flag).second,
                  "flag " + std::to_string((*list)[i].flag) + " is used by more than one list");
        }
    }
}

static void TestBatchSafety() {
    std::cout << "batch flag reading:" << std::endl;

    // The declared per-flag size must match what the builder really emits -
    // every size calculation is derived from it.
    for (size_t n : { (size_t)1, (size_t)25, (size_t)77, (size_t)82 }) {
        std::vector<uint32_t> ids(n, 14000800);
        std::vector<BYTE> code = BuildBatchCode(0x7FF000000000ULL, (BYTE*)0x140000000ULL, ids, 0, n, (BYTE*)0x500000ULL);
        Check(code.size() == BatchCodeSize(n),
              "size formula disagrees with generated code at n=" + std::to_string(n));
    }

    const size_t BUFFER = 4096;
    size_t perBatch = FlagsPerBatch(BUFFER);
    Check(perBatch > 0, "a 4096-byte buffer must fit at least one flag");

    // Every allowed batch size must leave the results clear of the code.
    for (size_t n = 1; n <= perBatch; n++) {
        size_t resultsOffset = BatchCodeSize(n) + BATCH_GAP;
        std::vector<uint32_t> ids(n, 13000800);
        std::vector<BYTE> code = BuildBatchCode(0x7FF000000000ULL, (BYTE*)0x140000000ULL, ids, 0, n, (BYTE*)(0x500000ULL + resultsOffset));
        Check(code.size() <= resultsOffset, "code reaches into results at n=" + std::to_string(n));
        Check(resultsOffset + n <= BUFFER, "results run past the buffer at n=" + std::to_string(n));
    }

    // One past the limit must genuinely not fit.
    Check(BatchCodeSize(perBatch + 1) + BATCH_GAP + (perBatch + 1) > BUFFER,
          "the batch limit is not actually the boundary");

    // Everything tracked must go out in ONE pass at the real buffer size.
    // Each extra pass is another thread started inside the game, and that is
    // the riskiest thing this tool does.
    size_t allTracked = g_tracked.bosses.size() + g_tracked.bonfires.size()
                      + g_tracked.quests.size();
    size_t realPerBatch = FlagsPerBatch(REMOTE_BUFFER_SIZE);
    Check(allTracked <= realPerBatch,
          "everything tracked (" + std::to_string(allTracked) + " flags) no longer fits in one "
          "pass of " + std::to_string(realPerBatch) + " - that means extra threads in the game");
    std::cout << "  all " << allTracked << " tracked flags fit in one pass (room for "
              << realPerBatch << ")" << std::endl;

    // Lists too big for the buffer must split, not overflow.
    size_t small = FlagsPerBatch(1024);
    Check(small > 0 && small < g_tracked.bonfires.size(), "a small buffer should force splitting");
    Check(BatchCodeSize(small) + BATCH_GAP + small <= 1024, "split batch overflows its buffer");

    // Buffers too small to work with must refuse rather than try.
    Check(FlagsPerBatch(0) == 0, "zero-size buffer must yield nothing");
    Check(FlagsPerBatch(8) == 0, "tiny buffer must yield nothing");

    std::cout << "  " << perBatch << " flags would fit in a single 4096-byte pass"
              << std::endl;
}

static void TestColumnLayout() {
    std::cout << "column layout:" << std::endl;

    const int LINE_HEIGHT = 20;
    const int PADDING = 40;
    const int SCREEN_MARGIN = 10;
    const int SCREEN = 864; // the screen this was developed against
    // The overlay sits SCREEN_MARGIN from the top, so the layout only gets
    // the screen minus a margin at each end. Budgeting the full height once
    // put the bottom line 6px off-screen.
    const int BUDGET = SCREEN - 2 * SCREEN_MARGIN;

    // Short lists stay in a single column.
    ColumnLayout one = PlanColumns(10, LINE_HEIGHT, PADDING, BUDGET, 8);
    Check(one.columns == 1, "a 10-line list should need one column");

    // Nothing may ever be laid out so tall that it runs off the bottom once
    // placed - the overlay is click-through, so anything off-screen is
    // unreachable and there is nothing to scroll.
    const int MAX_COLUMNS = 8; // plenty at the default font size
    for (int lineCount = 1; lineCount <= 400; lineCount++) {
        ColumnLayout layout = PlanColumns(lineCount, LINE_HEIGHT, PADDING, BUDGET, MAX_COLUMNS);
        int height = PADDING + layout.linesPerColumn * LINE_HEIGHT;
        Check(SCREEN_MARGIN + height <= SCREEN,
              "runs off the bottom at " + std::to_string(lineCount) + " lines");
        Check(layout.columns >= 1, "column count must be at least 1");
        Check(layout.columns <= MAX_COLUMNS, "column count must respect the cap");
        Check(layout.linesPerColumn >= 1, "lines per column must be at least 1");
        // Every line must either land somewhere or be counted as dropped.
        Check(layout.columns * layout.linesPerColumn + layout.droppedLines >= lineCount,
              "layout loses lines at " + std::to_string(lineCount));
    }
    std::cout << "  1..400 lines: always fits on screen, never loses a line" << std::endl;

    // A hard column cap must report the shortfall rather than overflow.
    {
        ColumnLayout tight = PlanColumns(500, LINE_HEIGHT, PADDING, BUDGET, 2);
        Check(tight.columns == 2, "must respect a cap of 2 columns");
        Check(tight.droppedLines > 0, "must report lines that did not fit");
        Check(tight.columns * tight.linesPerColumn + tight.droppedLines == 500,
              "dropped count must account for exactly the leftover lines");
        std::cout << "  500 lines capped at 2 columns: "
                  << tight.columns * tight.linesPerColumn << " shown, "
                  << tight.droppedLines << " reported as not fitting" << std::endl;
    }

    // The real combined list must fit, placed at its real position.
    int bossLines = 2 + (int)g_tracked.bosses.size() + 3;          // souls + summary + bosses + section headers
    int bonfireLines = 1 + (int)g_tracked.bonfires.size() + 14;   // summary + bonfires + area headers
    int total = bossLines + bonfireLines;
    ColumnLayout real = PlanColumns(total, LINE_HEIGHT, PADDING, BUDGET, MAX_COLUMNS);
    Check(real.droppedLines == 0, "the real list should fit without dropping anything");
    int realHeight = PADDING + real.linesPerColumn * LINE_HEIGHT;
    Check(SCREEN_MARGIN + realHeight <= SCREEN, "the real combined list runs off the screen");
    std::cout << "  bosses + bonfires = " << total << " lines -> "
              << real.columns << " columns of " << real.linesPerColumn
              << " (" << realHeight << "px tall, bottom at "
              << (SCREEN_MARGIN + realHeight) << " on a " << SCREEN << "px screen)" << std::endl;
}

// The loader has to be forgiving about whitespace and comments, and loud
// about anything it can't make sense of - a typo in a data file should be
// findable rather than silently dropping an entry.
static void TestDataFileParsing() {
    std::cout << "data file loading:" << std::endl;

    // The real files must load cleanly and match the lists the code expects.
    struct { const wchar_t* file; int expected; const char* label; } files[] = {
        { L"bosses.txt",   (int)g_tracked.bosses.size(),   "bosses.txt" },
        { L"bonfires.txt", (int)g_tracked.bonfires.size(), "bonfires.txt" },
        { L"quests.txt",   (int)g_tracked.quests.size(),   "quests.txt" },
    };
    for (auto& f : files) {
        LoadedList loaded = LoadTrackedList(f.file);
        Check(loaded.fileFound, std::string(f.label) + " not found");
        for (size_t i = 0; i < loaded.problems.size(); i++) {
            Check(false, std::string(f.label) + ": " + loaded.problems[i]);
        }
        Check((int)loaded.entries.size() == f.expected,
              std::string(f.label) + ": expected " + std::to_string(f.expected) +
              " entries, got " + std::to_string(loaded.entries.size()));
        std::cout << "  " << f.label << ": " << loaded.entries.size()
                  << " entries, " << loaded.problems.size() << " problems" << std::endl;
    }

    // Loading something that isn't there must report it, not crash or
    // pretend the list is simply empty.
    LoadedList missing = LoadTrackedList(L"definitely-not-a-real-file.txt");
    Check(!missing.fileFound, "a missing file must be reported as missing");
    Check(!missing.problems.empty(), "a missing file must produce a problem message");

    std::cout << "  missing file reported rather than treated as empty" << std::endl;
}

// The missable rules are the one place where being wrong is worse than being
// absent: a false alarm gets the whole feature ignored, and a missed warning
// wrongly reassures someone about a questline they've already broken.
static void TestMissableRules() {
    std::cout << "missable rules:" << std::endl;

    LoadedRules loaded = LoadMissableRules(L"missable.txt");
    Check(loaded.fileFound, "missable.txt not found");
    for (size_t i = 0; i < loaded.problems.size(); i++) {
        Check(false, "missable.txt: " + loaded.problems[i]);
    }
    Check(!loaded.rules.empty(), "missable.txt has no rules");

    // Every flag a rule mentions must be a flag we actually track, or the
    // rule can never fire and is silently dead.
    std::set<uint32_t> known;
    const std::vector<TrackedEntry>* lists[] = {
        &g_tracked.bosses, &g_tracked.bonfires, &g_tracked.quests
    };
    for (auto* list : lists) {
        for (size_t i = 0; i < list->size(); i++) {
            known.insert((*list)[i].flag);
        }
    }
    for (size_t i = 0; i < loaded.rules.size(); i++) {
        const MissableRule& rule = loaded.rules[i];
        Check(known.count(rule.rewardFlag) > 0,
              "rule flag " + std::to_string(rule.rewardFlag) + " is not in any tracked list");
        Check(known.count(rule.blockerFlag) > 0,
              "closing flag " + std::to_string(rule.blockerFlag) + " is not in any tracked list");
    }

    // Behaviour, against a made-up rule so the check doesn't depend on the
    // real data staying the same.
    std::vector<MissableRule> rules;
    MissableRule rule;
    rule.reward = L"Test Reward";
    rule.rewardFlag = 50000001;
    rule.blockerFlag = 13000001;
    rule.blockedBy = L"Test Boss";
    rules.push_back(rule);

    // Nothing has happened yet - no warning.
    std::map<uint32_t, bool> state;
    state[50000001] = false;
    state[13000001] = false;
    Check(FindMissed(rules, state).empty(), "warns before the closing event happened");

    // Got the reward, then the event happened - still fine.
    state[50000001] = true;
    state[13000001] = true;
    Check(FindMissed(rules, state).empty(), "warns even though the reward was collected");

    // Event happened without the reward - this is the one case that warns.
    state[50000001] = false;
    state[13000001] = true;
    std::vector<MissedThing> missed = FindMissed(rules, state);
    Check(missed.size() == 1, "did not warn when the reward was genuinely lost");

    // Reward collected, event not yet reached - no warning.
    state[50000001] = true;
    state[13000001] = false;
    Check(FindMissed(rules, state).empty(), "warns when nothing has been lost");

    // An unread flag must produce no opinion rather than a false alarm. This
    // matters because a partial read would otherwise look like "not collected".
    std::map<uint32_t, bool> partial;
    partial[13000001] = true; // closing event known, reward flag never read
    Check(FindMissed(rules, partial).empty(),
          "invented a warning from a flag that was never read");

    std::cout << "  " << loaded.rules.size()
              << " rules loaded, all flags tracked, fires only on genuine loss" << std::endl;
}

int main() {
    LoadTrackedLists();
    TestDataLists();
    TestMissableRules();
    TestDataFileParsing();
    TestBatchSafety();
    TestColumnLayout();

    std::cout << std::endl;
    if (g_failures == 0) {
        std::cout << "ALL TESTS PASSED" << std::endl;
        return 0;
    }
    std::cout << g_failures << " CHECK(S) FAILED" << std::endl;
    return 1;
}
