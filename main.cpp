#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <cstdint>

DWORD FindProcessId(const wchar_t* processName) {
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, processName) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pid;
}

BYTE* FindModuleBaseAddress(DWORD pid, const wchar_t* moduleName) {
    BYTE* base = nullptr;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return nullptr;
    }

    MODULEENTRY32W entry;
    entry.dwSize = sizeof(MODULEENTRY32W);

    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, moduleName) == 0) {
                base = entry.modBaseAddr;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return base;
}

int main() {
    const wchar_t* targetProcess = L"DarkSoulsIII.exe";

    DWORD pid = FindProcessId(targetProcess);
    if (pid == 0) {
        std::cout << "DarkSoulsIII.exe not found. Is the game running?" << std::endl;
        return 1;
    }
    std::cout << "Found DarkSoulsIII.exe with PID " << pid << std::endl;

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (process == NULL) {
        std::cout << "Failed to open process handle. Error code: " << GetLastError() << std::endl;
        return 1;
    }
    std::cout << "Successfully opened handle to DarkSoulsIII.exe" << std::endl;

    BYTE* moduleBase = FindModuleBaseAddress(pid, targetProcess);
    if (moduleBase == nullptr) {
        std::cout << "Failed to find module base address." << std::endl;
        CloseHandle(process);
        return 1;
    }
    std::cout << "Module base address: 0x" << std::hex << (uintptr_t)moduleBase << std::dec << std::endl;

    char header[2] = {0};
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(process, moduleBase, header, sizeof(header), &bytesRead)) {
        std::cout << "ReadProcessMemory failed. Error code: " << GetLastError() << std::endl;
        CloseHandle(process);
        return 1;
    }

    std::cout << "Read " << bytesRead << " bytes: " << header[0] << header[1] << std::endl;
    if (header[0] == 'M' && header[1] == 'Z') {
        std::cout << "PE header magic confirmed (MZ). Memory read pipeline works." << std::endl;
    } else {
        std::cout << "Unexpected bytes, something is off." << std::endl;
    }

    // Pointer chain for the player's souls count, sourced from the
    // darksoulsiii-practice-tool project's offset tables. Offset is specific
    // to game version 1.15.0.0 (checked via the exe's FileVersion) and will
    // need updating if the game version differs.
    const uintptr_t BASE_A_OFFSET = 0x4740178;

    uintptr_t baseA = (uintptr_t)moduleBase + BASE_A_OFFSET;

    uintptr_t ptr1 = 0;
    ReadProcessMemory(process, (LPCVOID)baseA, &ptr1, sizeof(ptr1), &bytesRead);
    std::cout << "ptr1 (base_a deref): 0x" << std::hex << ptr1 << std::dec << std::endl;

    uintptr_t ptr2Addr = ptr1 + 0x10;
    uintptr_t ptr2 = 0;
    ReadProcessMemory(process, (LPCVOID)ptr2Addr, &ptr2, sizeof(ptr2), &bytesRead);
    std::cout << "ptr2 (player struct ptr): 0x" << std::hex << ptr2 << std::dec << std::endl;

    uintptr_t soulsAddr = ptr2 + 0x74;
    std::cout << "soulsAddr: 0x" << std::hex << soulsAddr << std::dec << std::endl;
    uint32_t souls = 0;
    if (!ReadProcessMemory(process, (LPCVOID)soulsAddr, &souls, sizeof(souls), &bytesRead)) {
        std::cout << "Failed to read souls count. Error code: " << GetLastError() << std::endl;
        CloseHandle(process);
        return 1;
    }

    std::cout << "Current souls: " << souls << std::endl;

    CloseHandle(process);
    return 0;
}
