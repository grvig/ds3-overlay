#include "ds3reader.h"
#include <iostream>

int main() {
    Ds3Connection conn;
    if (!ConnectToDs3(conn)) {
        std::cout << "Failed to connect to DarkSoulsIII.exe. Is it running?" << std::endl;
        return 1;
    }
    std::cout << "Connected to DarkSoulsIII.exe." << std::endl;

    std::vector<uint8_t> flags = ReadAllBossFlags(conn);
    for (int i = 0; i < BOSS_COUNT; i++) {
        std::wcout << BOSS_LIST[i].name << L": " << (flags[i] ? L"defeated" : L"not defeated") << std::endl;
    }

    std::cout << "Souls: " << ReadSouls(conn) << std::endl;

    VirtualFreeEx(conn.process, conn.remoteBuffer, 0, MEM_RELEASE);
    CloseHandle(conn.process);
    return 0;
}
