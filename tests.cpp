// Checks that don't need the game running. Build and run with test.bat.
//
// Two things are covered:
//   1. The boss and bonfire lists are well-formed (no duplicate flags, no
//      empty names, areas grouped together).
//   2. The batch flag-reading code can never overwrite itself while it runs.
//      That overlap corrupted the game's own thread and closed the game
//      mid-session twice, so it's worth a permanent guard.
#include "ds3reader.h"
#include "bonfires.h"
#include "layout.h"

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

// Shared shape checks for any list of "thing with a name, a flag and a group".
struct Entry {
    const wchar_t* name;
    uint32_t flag;
    const wchar_t* group;
};

static void CheckList(const std::string& label, const std::vector<Entry>& entries) {
    std::set<uint32_t> seenFlags;
    for (size_t i = 0; i < entries.size(); i++) {
        Check(seenFlags.insert(entries[i].flag).second,
              label + ": duplicate flag " + std::to_string(entries[i].flag));
        Check(wcslen(entries[i].name) > 0, label + ": empty name at " + std::to_string(i));
        Check(wcslen(entries[i].group) > 0, label + ": empty group at " + std::to_string(i));
        // DS3 event flags for bosses and bonfires are 8-digit ids.
        Check(entries[i].flag >= 13000000 && entries[i].flag <= 15999999,
              label + ": flag out of range at " + std::to_string(i));
    }

    // The overlay prints a header whenever the group changes, so a group that
    // reappears later would get a second header.
    std::set<std::wstring> closed;
    const wchar_t* current = nullptr;
    int groups = 0;
    for (size_t i = 0; i < entries.size(); i++) {
        if (current == nullptr || wcscmp(current, entries[i].group) != 0) {
            if (current != nullptr) closed.insert(current);
            current = entries[i].group;
            groups++;
            Check(closed.count(current) == 0, label + ": group is not contiguous");
        }
    }

    std::cout << "  " << label << ": " << entries.size() << " entries, "
              << groups << " groups, " << seenFlags.size() << " unique flags" << std::endl;
}

static void TestDataLists() {
    std::cout << "data lists:" << std::endl;

    std::vector<Entry> bosses;
    for (int i = 0; i < BOSS_COUNT; i++) {
        bosses.push_back({ BOSS_LIST[i].name, BOSS_LIST[i].defeatedFlag, BOSS_LIST[i].section });
    }
    CheckList("bosses", bosses);

    std::vector<Entry> bonfires;
    for (int i = 0; i < BONFIRE_COUNT; i++) {
        bonfires.push_back({ BONFIRE_LIST[i].name, BONFIRE_LIST[i].litFlag, BONFIRE_LIST[i].area });
    }
    CheckList("bonfires", bonfires);

    // A boss and a bonfire can share a name ("Iudex Gundyr" is both), but
    // never a flag - those mean different things.
    std::set<uint32_t> bossFlags;
    for (int i = 0; i < BOSS_COUNT; i++) bossFlags.insert(BOSS_LIST[i].defeatedFlag);
    for (int i = 0; i < BONFIRE_COUNT; i++) {
        Check(bossFlags.count(BONFIRE_LIST[i].litFlag) == 0,
              "a bonfire reuses a boss flag");
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

    // Both real lists must be handled in one pass.
    Check((size_t)BOSS_COUNT <= perBatch, "bosses no longer fit in one pass");
    Check((size_t)BONFIRE_COUNT <= perBatch, "bonfires no longer fit in one pass");

    // Lists too big for the buffer must split, not overflow.
    size_t small = FlagsPerBatch(1024);
    Check(small > 0 && small < (size_t)BONFIRE_COUNT, "a small buffer should force splitting");
    Check(BatchCodeSize(small) + BATCH_GAP + small <= 1024, "split batch overflows its buffer");

    // Buffers too small to work with must refuse rather than try.
    Check(FlagsPerBatch(0) == 0, "zero-size buffer must yield nothing");
    Check(FlagsPerBatch(8) == 0, "tiny buffer must yield nothing");

    std::cout << "  " << perBatch << " flags fit per 4096-byte pass; "
              << BOSS_COUNT << " bosses and " << BONFIRE_COUNT
              << " bonfires each need one pass" << std::endl;
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
    ColumnLayout one = PlanColumns(10, LINE_HEIGHT, PADDING, BUDGET);
    Check(one.columns == 1, "a 10-line list should need one column");

    // Nothing may ever be laid out so tall that it runs off the bottom once
    // placed - the overlay is click-through, so anything off-screen is
    // unreachable and there is nothing to scroll.
    for (int lineCount = 1; lineCount <= 400; lineCount++) {
        ColumnLayout layout = PlanColumns(lineCount, LINE_HEIGHT, PADDING, BUDGET);
        int height = PADDING + layout.linesPerColumn * LINE_HEIGHT;
        Check(SCREEN_MARGIN + height <= SCREEN,
              "runs off the bottom at " + std::to_string(lineCount) + " lines");
        Check(layout.columns >= 1, "column count must be at least 1");
        Check(layout.linesPerColumn >= 1, "lines per column must be at least 1");
        // Every line must land somewhere.
        Check(layout.columns * layout.linesPerColumn >= lineCount,
              "layout drops lines at " + std::to_string(lineCount));
    }
    std::cout << "  1..400 lines: always fits on screen, never drops a line" << std::endl;

    // The real combined list must fit, placed at its real position.
    int bossLines = 2 + BOSS_COUNT + 3;          // souls + summary + bosses + section headers
    int bonfireLines = 1 + BONFIRE_COUNT + 14;   // summary + bonfires + area headers
    int total = bossLines + bonfireLines;
    ColumnLayout real = PlanColumns(total, LINE_HEIGHT, PADDING, BUDGET);
    int realHeight = PADDING + real.linesPerColumn * LINE_HEIGHT;
    Check(SCREEN_MARGIN + realHeight <= SCREEN, "the real combined list runs off the screen");
    std::cout << "  bosses + bonfires = " << total << " lines -> "
              << real.columns << " columns of " << real.linesPerColumn
              << " (" << realHeight << "px tall, bottom at "
              << (SCREEN_MARGIN + realHeight) << " on a " << SCREEN << "px screen)" << std::endl;
}

int main() {
    TestDataLists();
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
