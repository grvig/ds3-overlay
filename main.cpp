#include <windows.h>
#include <tlhelp32.h>
#include <iostream>

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

    CloseHandle(process);
    return 0;
}
