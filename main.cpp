#include "ds3reader.h"
#include <iostream>

// Prints one line per entry, with a header whenever the group changes, and
// returns how many were done. Mirrors how the overlay lays things out.
template <typename T, typename NameFn, typename GroupFn>
int PrintGroupedList(const T* list, int count, const std::vector<uint8_t>& flags,
                     const wchar_t* doneWord, const wchar_t* notDoneWord,
                     NameFn nameOf, GroupFn groupOf) {
    int doneCount = 0;
    const wchar_t* lastGroup = nullptr;
    for (int i = 0; i < count; i++) {
        if (lastGroup == nullptr || wcscmp(lastGroup, groupOf(list[i])) != 0) {
            lastGroup = groupOf(list[i]);
            std::wcout << L"\n  [" << lastGroup << L"]" << std::endl;
        }
        bool done = flags[i] != 0;
        if (done) {
            doneCount++;
        }
        std::wcout << L"    " << nameOf(list[i]) << L": "
                   << (done ? doneWord : notDoneWord) << std::endl;
    }
    return doneCount;
}

int main() {
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
    std::vector<uint8_t> bossFlags = ReadAllBossFlags(conn);
    int bossesDefeated = PrintGroupedList(
        BOSS_LIST, BOSS_COUNT, bossFlags, L"defeated", L"not defeated",
        [](const BossInfo& b) { return b.name; },
        [](const BossInfo& b) { return b.section; });

    std::cout << "\n=== BONFIRES ===" << std::endl;
    std::vector<uint8_t> bonfireFlags = ReadAllBonfireFlags(conn);
    int bonfiresLit = PrintGroupedList(
        BONFIRE_LIST, BONFIRE_COUNT, bonfireFlags, L"lit", L"not lit",
        [](const BonfireInfo& b) { return b.name; },
        [](const BonfireInfo& b) { return b.area; });

    std::cout << "\n=== TOTALS ===" << std::endl;
    std::cout << "Bosses defeated: " << bossesDefeated << " / " << BOSS_COUNT << std::endl;
    std::cout << "Bonfires lit:    " << bonfiresLit << " / " << BONFIRE_COUNT << std::endl;

    VirtualFreeEx(conn.process, conn.remoteBuffer, 0, MEM_RELEASE);
    CloseHandle(conn.process);
    return 0;
}
