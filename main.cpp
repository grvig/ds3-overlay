#include "ds3reader.h"
#include "tracked.h"
#include <iostream>

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

int main() {
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

    int bossCount = (int)g_tracked.bosses.size();
    int bonfireCount = (int)g_tracked.bonfires.size();
    int totalTracked = bossCount + bonfireCount;
    int totalDone = bossesDefeated + bonfiresLit;

    std::cout << "\n=== TOTALS ===" << std::endl;
    std::cout << "Bosses defeated: " << bossesDefeated << " / " << bossCount << std::endl;
    std::cout << "Bonfires lit:    " << bonfiresLit << " / " << bonfireCount << std::endl;
    if (totalTracked > 0) {
        std::cout << "Completion:      " << (totalDone * 100 / totalTracked) << "%  ("
                  << totalDone << "/" << totalTracked << ")" << std::endl;
    }

    VirtualFreeEx(conn.process, conn.remoteBuffer, 0, MEM_RELEASE);
    CloseHandle(conn.process);
    return 0;
}
