// Shared helpers for finding Dark Souls 3's process and reading its memory.
// Used by both the console test tool (main.cpp) and the overlay (overlay.cpp).
#pragma once

#include <windows.h>
#include <tlhelp32.h>
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

// Appends the raw bytes of a value (e.g. a number or address) to a byte list.
template <typename T>
void AppendBytes(std::vector<BYTE>& out, T value) {
    BYTE* asBytes = (BYTE*)&value;
    for (size_t i = 0; i < sizeof(T); i++) {
        out.push_back(asBytes[i]);
    }
}

// Builds a tiny piece of machine code that calls a function of the shape
// "u8 get_event_flag(uintptr_t eventFlagMan, uint32_t flagId)" inside the
// game, and stores the single-byte result at resultAddr. This is what lets
// us ask the game itself "is this flag set?" instead of guessing where that
// data lives in memory.
std::vector<BYTE> BuildGetEventFlagShellcode(BYTE* functionAddr, uintptr_t eventFlagMan, uint32_t flagId, BYTE* resultAddr) {
    std::vector<BYTE> code;

    code.push_back(0x48); code.push_back(0xB9);           // mov rcx, <eventFlagMan>
    AppendBytes(code, eventFlagMan);

    code.push_back(0xBA);                                 // mov edx, <flagId>
    AppendBytes(code, flagId);

    code.push_back(0x48); code.push_back(0x83); code.push_back(0xEC); code.push_back(0x28); // sub rsp, 0x28

    code.push_back(0x48); code.push_back(0xB8);           // mov rax, <functionAddr>
    AppendBytes(code, (uintptr_t)functionAddr);

    code.push_back(0xFF); code.push_back(0xD0);           // call rax

    code.push_back(0x49); code.push_back(0xB8);           // mov r8, <resultAddr>
    AppendBytes(code, (uintptr_t)resultAddr);

    code.push_back(0x41); code.push_back(0x88); code.push_back(0x00); // mov [r8], al

    code.push_back(0x48); code.push_back(0x83); code.push_back(0xC4); code.push_back(0x28); // add rsp, 0x28
    code.push_back(0xC3);                                  // ret

    return code;
}

// Writes the shellcode into the game's memory, tells Windows to run it as a
// new thread inside the game's process, waits for it to finish, then reads
// back the one-byte result it wrote.
uint8_t CallGetEventFlag(HANDLE process, BYTE* remoteBuffer, BYTE* functionAddr, uintptr_t eventFlagMan, uint32_t flagId) {
    BYTE* codeAddr = remoteBuffer;
    BYTE* resultAddr = remoteBuffer + 0x100;

    std::vector<BYTE> shellcode = BuildGetEventFlagShellcode(functionAddr, eventFlagMan, flagId, resultAddr);
    WriteProcessMemory(process, codeAddr, shellcode.data(), shellcode.size(), nullptr);

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, (LPTHREAD_START_ROUTINE)codeAddr, nullptr, 0, nullptr);
    if (thread == nullptr) {
        return 0;
    }
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);

    uint8_t result = 0;
    SIZE_T bytesRead = 0;
    ReadProcessMemory(process, resultAddr, &result, sizeof(result), &bytesRead);
    return result;
}

// One-time setup: finds the running game, opens a handle to it, and locates
// SprjEventFlagMan (the game's flag-tracking system) and its get_event_flag
// function. Returns true if everything was found successfully.
struct Ds3Connection {
    HANDLE process = nullptr;
    uintptr_t eventFlagMan = 0;
    BYTE* getEventFlagAddr = nullptr;
    LPVOID remoteBuffer = nullptr;
    BYTE* moduleBase = nullptr;
};

bool ConnectToDs3(Ds3Connection& conn) {
    const wchar_t* targetProcess = L"DarkSoulsIII.exe";

    DWORD pid = FindProcessId(targetProcess);
    if (pid == 0) {
        return false;
    }

    conn.process = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_CREATE_THREAD,
        FALSE, pid);
    if (conn.process == nullptr) {
        return false;
    }

    DWORD moduleSize = 0;
    BYTE* moduleBase = FindModuleBaseAddress(pid, targetProcess, &moduleSize);
    if (moduleBase == nullptr) {
        return false;
    }
    conn.moduleBase = moduleBase;

    std::vector<int> eventFlagManPattern = ParsePattern(
        "48 8B 0D ?? ?? ?? ?? 44 0F B6 CB 41 B8 07 00 00 00 8B D6"
    );
    BYTE* found = FindPattern(conn.process, moduleBase, moduleSize, eventFlagManPattern);
    if (found == nullptr) {
        return false;
    }

    BYTE* eventFlagManPtrAddr = ResolveRipRelative(conn.process, found, 3, 7);
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(conn.process, eventFlagManPtrAddr, &conn.eventFlagMan, sizeof(conn.eventFlagMan), &bytesRead)) {
        return false;
    }

    std::vector<int> getEventFlagPattern = ParsePattern(
        "40 53 48 83 EC 20 80 B9 28 02 00 00 00 8B DA 74 4D"
    );
    conn.getEventFlagAddr = FindPattern(conn.process, moduleBase, moduleSize, getEventFlagPattern);
    if (conn.getEventFlagAddr == nullptr) {
        return false;
    }

    conn.remoteBuffer = VirtualAllocEx(conn.process, nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (conn.remoteBuffer == nullptr) {
        return false;
    }

    return true;
}

uint8_t ReadEventFlag(const Ds3Connection& conn, uint32_t flagId) {
    return CallGetEventFlag(conn.process, (BYTE*)conn.remoteBuffer, conn.getEventFlagAddr, conn.eventFlagMan, flagId);
}

// Reads the player's current souls count via the same pointer chain
// discovered on day 1. Specific to game version 1.15.0.0.
uint32_t ReadSouls(const Ds3Connection& conn) {
    const uintptr_t BASE_A_OFFSET = 0x4740178;
    uintptr_t baseA = (uintptr_t)conn.moduleBase + BASE_A_OFFSET;

    SIZE_T bytesRead = 0;
    uintptr_t ptr1 = 0;
    ReadProcessMemory(conn.process, (LPCVOID)baseA, &ptr1, sizeof(ptr1), &bytesRead);

    uintptr_t ptr2 = 0;
    ReadProcessMemory(conn.process, (LPCVOID)(ptr1 + 0x10), &ptr2, sizeof(ptr2), &bytesRead);

    uint32_t souls = 0;
    ReadProcessMemory(conn.process, (LPCVOID)(ptr2 + 0x74), &souls, sizeof(souls), &bytesRead);
    return souls;
}

// Every main boss in the base game plus both DLCs, and the event flag that
// tracks whether each one has been defeated. Flag IDs sourced from The
// Grand Archives' public DS3 Cheat Engine table.
struct BossInfo {
    const wchar_t* name;
    uint32_t defeatedFlag;
    const wchar_t* section;
};

const BossInfo BOSS_LIST[] = {
    { L"Iudex Gundyr", 14000800, L"Base Game" },
    { L"Vordt of the Boreal Valley", 13000800, L"Base Game" },
    { L"Curse-Rotted Greatwood", 13100800, L"Base Game" },
    { L"Crystal Sage", 13300850, L"Base Game" },
    { L"Deacons of the Deep", 13500800, L"Base Game" },
    { L"Abyss Watchers", 13300800, L"Base Game" },
    { L"High Lord Wolnir", 13800800, L"Base Game" },
    { L"Old Demon King", 13800830, L"Base Game" },
    { L"Yhorm the Giant", 13900800, L"Base Game" },
    { L"Pontiff Sulyvahn", 13700850, L"Base Game" },
    { L"Aldrich, Devourer of Gods", 13700800, L"Base Game" },
    { L"Dancer of the Boreal Valley", 13000890, L"Base Game" },
    { L"Oceiros, the Consumed King", 13000830, L"Base Game" },
    { L"Champion Gundyr", 14000830, L"Base Game" },
    { L"Ancient Wyvern", 13200800, L"Base Game" },
    { L"Nameless King", 13200850, L"Base Game" },
    { L"Dragonslayer Armour", 13010800, L"Base Game" },
    { L"Twin Princes", 13410830, L"Base Game" },
    { L"Soul of Cinder", 14100800, L"Base Game" },
    { L"Champion's Gravetender", 14500860, L"Ashes of Ariandel" },
    { L"Father Ariandel and Sister Friede", 14500800, L"Ashes of Ariandel" },
    { L"Demon Prince", 15000800, L"The Ringed City" },
    { L"Halflight, Spear of the Church", 15100800, L"The Ringed City" },
    { L"Darkeater Midir", 15100850, L"The Ringed City" },
    { L"Slave Knight Gael", 15110800, L"The Ringed City" },
};
const int BOSS_COUNT = sizeof(BOSS_LIST) / sizeof(BOSS_LIST[0]);
