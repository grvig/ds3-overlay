#include "ds3reader.h"
#include <iostream>

int main() {
    Ds3Connection conn;
    if (!ConnectToDs3(conn)) {
        std::cout << "Failed to connect to DarkSoulsIII.exe. Is it running?" << std::endl;
        return 1;
    }
    std::cout << "Connected to DarkSoulsIII.exe." << std::endl;

    for (int i = 0; i < BOSS_COUNT; i++) {
        uint8_t defeated = ReadEventFlag(conn, BOSS_LIST[i].defeatedFlag);
        std::wcout << BOSS_LIST[i].name << L": " << (defeated ? L"defeated" : L"not defeated") << std::endl;
    }

    VirtualFreeEx(conn.process, conn.remoteBuffer, 0, MEM_RELEASE);
    CloseHandle(conn.process);
    return 0;
}
