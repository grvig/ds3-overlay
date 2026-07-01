#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <cstdint>
#include <string>
#include <sstream>
#include <vector>

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

BYTE* FindModuleBaseAddress(DWORD pid, const wchar_t* moduleName, DWORD* outModuleSize = nullptr) {
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
                if (outModuleSize != nullptr) {
                    *outModuleSize = entry.modBaseSize;
                }
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return base;
}

// Parses a Cheat Engine-style AOB pattern string, e.g. "48 8B 0D ?? ?? ?? ??".
// A value of -1 in the returned vector marks a wildcard byte.
std::vector<int> ParsePattern(const std::string& patternStr) {
    std::vector<int> pattern;
    std::istringstream stream(patternStr);
    std::string token;
    while (stream >> token) {
        if (token == "??" || token == "?") {
            pattern.push_back(-1);
        } else {
            pattern.push_back(std::stoi(token, nullptr, 16));
        }
    }
    return pattern;
}

bool MatchesAt(const std::vector<BYTE>& buffer, size_t offset, const std::vector<int>& pattern) {
    for (size_t j = 0; j < pattern.size(); j++) {
        if (pattern[j] != -1 && buffer[offset + j] != (BYTE)pattern[j]) {
            return false;
        }
    }
    return true;
}

// Scans the target process's memory in the [moduleBase, moduleBase + moduleSize)
// range for the first occurrence of the given byte pattern. Walks committed,
// readable memory regions individually via VirtualQueryEx so gaps or
// protected pages in the middle of the module don't abort the whole scan.
// Returns the address of the match, or nullptr if not found.
BYTE* FindPattern(HANDLE process, BYTE* moduleBase, SIZE_T moduleSize, const std::vector<int>& pattern) {
    BYTE* regionStart = moduleBase;
    BYTE* moduleEnd = moduleBase + moduleSize;

    while (regionStart < moduleEnd) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQueryEx(process, regionStart, &mbi, sizeof(mbi)) == 0) {
            break;
        }

        SIZE_T regionSize = mbi.RegionSize;
        BYTE* nextRegionStart = (BYTE*)mbi.BaseAddress + regionSize;

        bool readable = mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0;

        if (readable) {
            std::vector<BYTE> buffer(regionSize);
            SIZE_T bytesRead = 0;
            if (ReadProcessMemory(process, mbi.BaseAddress, buffer.data(), regionSize, &bytesRead) && bytesRead >= pattern.size()) {
                for (size_t i = 0; i + pattern.size() <= bytesRead; i++) {
                    if (MatchesAt(buffer, i, pattern)) {
                        return (BYTE*)mbi.BaseAddress + i;
                    }
                }
            }
        }

        regionStart = nextRegionStart;
    }

    return nullptr;
}

// Many "find this game system" patterns land on a CPU instruction that loads
// an address relative to itself (this is how 64-bit programs often reference
// global data). This resolves that into the actual absolute address it points to.
// instructionAddr = where the pattern match started.
// dispOffset = how many bytes into the instruction the 4-byte relative offset is.
// instructionLength = total length of the instruction in bytes.
BYTE* ResolveRipRelative(HANDLE process, BYTE* instructionAddr, int dispOffset, int instructionLength) {
    int32_t disp = 0;
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(process, instructionAddr + dispOffset, &disp, sizeof(disp), &bytesRead)) {
        return nullptr;
    }
    return instructionAddr + instructionLength + disp;
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

    // Test the AOB scanner against a known pattern (from The Grand Archives'
    // DS3 Cheat Engine table) that locates SprjEventFlagMan, the game's
    // event flag manager. This is just confirming the scanner works -
    // actually reading a flag through it is the next step.
    DWORD moduleSize = 0;
    FindModuleBaseAddress(pid, targetProcess, &moduleSize);

    std::vector<int> eventFlagManPattern = ParsePattern(
        "48 8B 0D ?? ?? ?? ?? 44 0F B6 CB 41 B8 07 00 00 00 8B D6"
    );
    BYTE* found = FindPattern(process, moduleBase, moduleSize, eventFlagManPattern);
    if (found == nullptr) {
        std::cout << "Pattern not found." << std::endl;
        CloseHandle(process);
        return 1;
    }
    std::cout << "Pattern found at: 0x" << std::hex << (uintptr_t)found
               << " (module offset 0x" << ((uintptr_t)found - (uintptr_t)moduleBase) << ")"
               << std::dec << std::endl;

    // The matched instruction is "mov rcx, [some address near here]" - resolve
    // that into the actual address of SprjEventFlagMan (a pointer that itself
    // points to the block of memory holding all of the game's event flags).
    BYTE* eventFlagManPtrAddr = ResolveRipRelative(process, found, 3, 7);
    std::cout << "SprjEventFlagMan pointer lives at: 0x" << std::hex << (uintptr_t)eventFlagManPtrAddr << std::dec << std::endl;

    uintptr_t eventFlagMan = 0;
    ReadProcessMemory(process, eventFlagManPtrAddr, &eventFlagMan, sizeof(eventFlagMan), &bytesRead);
    std::cout << "SprjEventFlagMan: 0x" << std::hex << eventFlagMan << std::dec << std::endl;

    // Double-check our result using a second, completely different pattern
    // (sourced from the SoulSplitter project) that also leads to
    // SprjEventFlagMan. If both independent patterns agree on the same
    // address, that's strong confirmation we found the right thing.
    std::vector<int> eventFlagManPattern2 = ParsePattern(
        "48 C7 05 ?? ?? ?? ?? 00 00 00 00 48 8B 7C 24 38 C7 46 54 FF FF FF FF 48 83 C4 20 5E C3"
    );
    BYTE* found2 = FindPattern(process, moduleBase, moduleSize, eventFlagManPattern2);
    if (found2 == nullptr) {
        std::cout << "Second pattern not found." << std::endl;
        CloseHandle(process);
        return 1;
    }

    BYTE* eventFlagManPtrAddr2 = ResolveRipRelative(process, found2, 3, 11);
    uintptr_t eventFlagMan2 = 0;
    ReadProcessMemory(process, eventFlagManPtrAddr2, &eventFlagMan2, sizeof(eventFlagMan2), &bytesRead);

    std::cout << "SprjEventFlagMan (via 2nd pattern): 0x" << std::hex << eventFlagMan2 << std::dec << std::endl;
    if (eventFlagMan == eventFlagMan2) {
        std::cout << "Both patterns agree - address is confirmed correct." << std::endl;
    } else {
        std::cout << "Mismatch! Something is wrong with one of the patterns." << std::endl;
    }

    // Rather than guess the exact byte/bit layout of the flag data ourselves
    // (which we tried and got wrong earlier), find the game's own built-in
    // function that checks a flag's state. This is a recognizable chunk of
    // machine code the game uses internally, found the same way we found
    // everything else: searching memory for a distinctive byte pattern.
    std::vector<int> getEventFlagPattern = ParsePattern(
        "40 53 48 83 EC 20 80 B9 28 02 00 00 00 8B DA 74 4D"
    );
    BYTE* getEventFlagAddr = FindPattern(process, moduleBase, moduleSize, getEventFlagPattern);
    if (getEventFlagAddr == nullptr) {
        std::cout << "get_event_flag function not found." << std::endl;
        CloseHandle(process);
        return 1;
    }
    std::cout << "get_event_flag function address: 0x" << std::hex << (uintptr_t)getEventFlagAddr << std::dec << std::endl;

    CloseHandle(process);
    return 0;
}
