#include "ds3reader.h"
#include <iostream>

int main() {
    Ds3Connection conn;
    if (!ConnectToDs3(conn)) {
        std::cout << "Failed to connect to DarkSoulsIII.exe. Is it running?" << std::endl;
        return 1;
    }
    std::cout << "Connected to DarkSoulsIII.exe." << std::endl;

    const uint32_t IUDEX_GUNDYR_DEFEATED_FLAG = 14000800;
    const uint32_t SOUL_OF_CINDER_DEFEATED_FLAG = 14100800;

    uint8_t gundyrDefeated = ReadEventFlag(conn, IUDEX_GUNDYR_DEFEATED_FLAG);
    std::cout << "Iudex Gundyr defeated: " << (gundyrDefeated ? "true" : "false") << std::endl;

    uint8_t socDefeated = ReadEventFlag(conn, SOUL_OF_CINDER_DEFEATED_FLAG);
    std::cout << "Soul of Cinder defeated: " << (socDefeated ? "true" : "false") << std::endl;

    VirtualFreeEx(conn.process, conn.remoteBuffer, 0, MEM_RELEASE);
    CloseHandle(conn.process);
    return 0;
}
