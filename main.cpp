#include "ds3reader.h"
#include "tracked.h"
#include <iostream>
#include <map>
#include <fstream>

// Prints one line per entry, with a header whenever the group changes, and
// returns how many were done. Mirrors how the overlay lays things out.
int PrintGroupedList(const std::vector<TrackedEntry>& list, const std::vector<uint8_t>& flags,
                     const wchar_t* doneWord, const wchar_t* notDoneWord) {
    int doneCount = 0;
    std::wstring lastGroup;
    bool haveGroup = false;
    for (size_t i = 0; i < list.size(); i++) {
        if (!haveGroup || list[i].group != lastGroup) {
            lastGroup = list[i].group;
            haveGroup = true;
            std::wcout << L"\n  [" << lastGroup << L"]" << std::endl;
        }
        bool done = (i < flags.size()) && flags[i] != 0;
        if (done) {
            doneCount++;
        }
        std::wcout << L"    " << list[i].name << L": "
                   << (done ? doneWord : notDoneWord) << std::endl;
    }
    return doneCount;
}

// Polls every tracked flag and reports the moment one changes.
//
// This exists because the questline flag ids came from a single source and
// have never been checked against a real save. Rather than guessing whether
// an id is right, run this, do the thing in game, and see which entry fires -
// or see nothing fire, which means the id is wrong.
int RunWatch(Ds3Connection& conn) {
    struct Watched {
        const std::vector<TrackedEntry>* list;
        const char* label;
    };
    const Watched watched[] = {
        { &g_tracked.bosses,   "boss"    },
        { &g_tracked.bonfires, "bonfire" },
        { &g_tracked.quests,   "quest"   },
        { &g_tracked.npcDrops, "drop"    },
    };

    // One flat list, so everything is read in a single pass per tick.
    std::vector<uint32_t> flagIds;
    std::vector<std::wstring> labels;
    for (const Watched& w : watched) {
        for (size_t i = 0; i < w.list->size(); i++) {
            flagIds.push_back((*w.list)[i].flag);
            labels.push_back(L"[" + Widen(w.label) + L"] " + (*w.list)[i].group
                             + L" / " + (*w.list)[i].name);
        }
    }

    // Everything also goes to a file, because the point is to be playing
    // while this runs - scrollback in a console you aren't looking at is no
    // use afterwards.
    std::wstring logPath = GetDataPath(L"../watch-log.txt");
    std::wofstream log(logPath.c_str(), std::ios::app);
    bool logging = log.is_open();

    // Writes to the console and, when it opened, to the log as well.
    auto report = [&](const std::wstring& text) {
        std::wcout << text << std::endl;
        if (logging) {
            log << text << std::endl;
            log.flush(); // losing findings to a crash would defeat the point
        }
    };

    std::cout << "Watching " << flagIds.size()
              << " flags. Do something in game and it will show up here." << std::endl;
    if (logging) {
        std::wcout << L"Also writing to " << logPath << std::endl;
    } else {
        std::cout << "(could not open the log file; console only)" << std::endl;
    }
    std::cout << "Press Ctrl+C to stop." << std::endl << std::endl;

    report(L"=== watch session started ===");

    std::vector<uint8_t> previous = ReadFlags(conn, flagIds);
    int alreadySet = 0;
    for (size_t i = 0; i < previous.size(); i++) {
        if (previous[i]) {
            alreadySet++;
            report(L"  already set: " + std::to_wstring(flagIds[i]) + L"  " + labels[i]);
        }
    }
    report(L"  (" + std::to_wstring(alreadySet) + L" of "
           + std::to_wstring(flagIds.size()) + L" already set)");
    report(L"--- watching for changes ---");

    while (true) {
        Sleep(500);

        DWORD exitCode = 0;
        if (!GetExitCodeProcess(conn.process, &exitCode) || exitCode != STILL_ACTIVE) {
            report(L"=== game closed ===");
            return 0;
        }

        std::vector<uint8_t> current = ReadFlags(conn, flagIds);
        for (size_t i = 0; i < current.size() && i < previous.size(); i++) {
            if (current[i] != previous[i]) {
                report(std::wstring(current[i] ? L"  SET   " : L"  UNSET ")
                       + std::to_wstring(flagIds[i]) + L"  " + labels[i]);
            }
        }
        previous = current;
    }
}

int main(int argc, char** argv) {
    bool watchMode = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--watch") {
            watchMode = true;
        }
    }

    LoadTrackedLists();
    if (g_tracked.AnyProblems()) {
        std::cout << "Problems in the data files:" << std::endl;
        for (size_t i = 0; i < g_tracked.problems.size(); i++) {
            std::cout << "  " << g_tracked.problems[i] << std::endl;
        }
        std::cout << std::endl;
    }

    Ds3Connection conn;
    if (!ConnectToDs3(conn)) {
        std::cout << "Failed to connect to DarkSoulsIII.exe. Is it running?" << std::endl;
        return 1;
    }
    std::cout << "Connected to DarkSoulsIII.exe." << std::endl;
    std::cout << "Game version: " << conn.versionMajor << "." << conn.versionMinor << "." << conn.versionPatch;
    if (conn.baseA == 0) {
        std::cout << " (no offsets for this version - souls unavailable)";
    }
    std::cout << std::endl;

    if (watchMode) {
        int result = RunWatch(conn);
        VirtualFreeEx(conn.process, conn.remoteBuffer, 0, MEM_RELEASE);
        CloseHandle(conn.process);
        return result;
    }

    uint32_t souls = 0;
    if (ReadSouls(conn, souls)) {
        std::cout << "Souls: " << souls << std::endl;
    } else {
        std::cout << "Souls: unavailable" << std::endl;
    }

    std::cout << "\n=== BOSSES ===" << std::endl;
    std::vector<uint8_t> bossFlags = ReadFlags(conn, FlagsOf(g_tracked.bosses));
    int bossesDefeated = PrintGroupedList(g_tracked.bosses, bossFlags, L"defeated", L"not defeated");

    std::cout << "\n=== BONFIRES ===" << std::endl;
    std::vector<uint8_t> bonfireFlags = ReadFlags(conn, FlagsOf(g_tracked.bonfires));
    int bonfiresLit = PrintGroupedList(g_tracked.bonfires, bonfireFlags, L"lit", L"not lit");

    std::cout << "\n=== QUEST REWARDS ===" << std::endl;
    std::vector<uint8_t> questFlags = ReadFlags(conn, FlagsOf(g_tracked.quests));
    int questsDone = PrintGroupedList(g_tracked.quests, questFlags, L"received", L"not received");

    std::cout << "\n=== NPC DROPS ===" << std::endl;
    std::vector<uint8_t> dropFlags = ReadFlags(conn, FlagsOf(g_tracked.npcDrops));
    int dropsTaken = PrintGroupedList(g_tracked.npcDrops, dropFlags, L"taken", L"not taken");

    // Work out what's already been lost, from everything read above.
    std::map<uint32_t, bool> flagState;
    for (size_t i = 0; i < g_tracked.bosses.size() && i < bossFlags.size(); i++) {
        flagState[g_tracked.bosses[i].flag] = bossFlags[i] != 0;
    }
    for (size_t i = 0; i < g_tracked.bonfires.size() && i < bonfireFlags.size(); i++) {
        flagState[g_tracked.bonfires[i].flag] = bonfireFlags[i] != 0;
    }
    for (size_t i = 0; i < g_tracked.quests.size() && i < questFlags.size(); i++) {
        flagState[g_tracked.quests[i].flag] = questFlags[i] != 0;
    }

    std::vector<MissedThing> missed = FindMissed(g_tracked.missableRules, flagState);
    std::cout << "\n=== MISSED (no longer obtainable) ===" << std::endl;
    if (missed.empty()) {
        std::cout << "  nothing missed - every tracked questline reward is still available"
                  << std::endl;
    } else {
        for (size_t i = 0; i < missed.size(); i++) {
            std::wcout << L"  " << missed[i].Describe() << std::endl;
        }
    }
    std::cout << "  (" << g_tracked.missableRules.size() << " rules checked; these are"
              << " provisional - see data/missable.txt)" << std::endl;


    int bossCount = (int)g_tracked.bosses.size();
    int bonfireCount = (int)g_tracked.bonfires.size();
    int questCount = (int)g_tracked.quests.size();
    int dropCount = (int)g_tracked.npcDrops.size();
    // Everything printed above counts towards the total, so the percentage
    // matches what's on screen rather than a subset of it.
    int totalTracked = bossCount + bonfireCount + questCount + dropCount;
    int totalDone = bossesDefeated + bonfiresLit + questsDone + dropsTaken;

    std::cout << "\n=== TOTALS ===" << std::endl;
    std::cout << "Bosses defeated: " << bossesDefeated << " / " << bossCount << std::endl;
    std::cout << "Bonfires lit:    " << bonfiresLit << " / " << bonfireCount << std::endl;
    std::cout << "Quest rewards:   " << questsDone << " / " << questCount << std::endl;
    std::cout << "NPC drops:       " << dropsTaken << " / " << dropCount << std::endl;
    if (totalTracked > 0) {
        std::cout << "Completion:      " << (totalDone * 100 / totalTracked) << "%  ("
                  << totalDone << "/" << totalTracked << ")" << std::endl;
    }

    // A wrong questline flag id fails quietly: it just reads as "not
    // received" forever, so the missable rules never fire and everything
    // looks fine. If this save has clearly made progress but not one quest
    // reward registered, bad ids are far likelier than a real playthrough,
    // so say so rather than let it pass unnoticed.
    bool madeProgress = bossesDefeated > 0 || bonfiresLit > 2;
    if (madeProgress && questsDone == 0 && questCount > 0) {
        std::cout << std::endl;
        std::cout << "WARNING: " << bossesDefeated << " bosses and " << bonfiresLit
                  << " bonfires registered, but 0 of " << questCount
                  << " quest rewards did." << std::endl;
        std::cout << "  The quest flag ids are probably wrong - they have never"
                  << " been checked against a real save." << std::endl;
        std::cout << "  Run \"main.exe --watch\", pick up a questline item, and"
                  << " see which id actually fires." << std::endl;
        std::cout << "  Until then, treat the MISSED section above as unreliable."
                  << std::endl;
    }

    VirtualFreeEx(conn.process, conn.remoteBuffer, 0, MEM_RELEASE);
    CloseHandle(conn.process);
    return 0;
}
