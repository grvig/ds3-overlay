#include "ds3reader.h"
#include <iostream>

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

    std::vector<uint8_t> flags = ReadAllBossFlags(conn);
    for (int i = 0; i < BOSS_COUNT; i++) {
        std::wcout << BOSS_LIST[i].name << L": " << (flags[i] ? L"defeated" : L"not defeated") << std::endl;
    }

    uint32_t souls = 0;
    if (ReadSouls(conn, souls)) {
        std::cout << "Souls: " << souls << std::endl;
    } else {
        std::cout << "Souls: unavailable" << std::endl;
    }

    VirtualFreeEx(conn.process, conn.remoteBuffer, 0, MEM_RELEASE);
    CloseHandle(conn.process);
    return 0;
}
