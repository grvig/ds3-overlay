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

// Every flag check adds exactly this many bytes of machine code. The loop in
// BuildBatchCode below must keep matching this number - BatchCodeSize is
// asserted against the real generated length before anything runs.
const size_t BYTES_PER_FLAG_CHECK = 48;

// Size of the scratch buffer carved out inside the game. Sized so every flag
// we track fits in a single pass - see the note where it's allocated.
const size_t REMOTE_BUFFER_SIZE = 64 * 1024;

// Gap left between the end of the code and the start of the results. The two
// must never overlap: the code is still executing while it writes results,
// so a result landing on a not-yet-executed instruction corrupts the game's
// thread mid-run. That exact overlap is what used to crash the game.
const size_t BATCH_GAP = 64;

size_t BatchCodeSize(size_t flagCount) {
    return flagCount * BYTES_PER_FLAG_CHECK + 1; // +1 for the trailing ret
}

// Largest number of flags whose code, gap and results all fit in one buffer.
size_t FlagsPerBatch(size_t bufferSize) {
    size_t overhead = BATCH_GAP + 1;
    if (bufferSize <= overhead) {
        return 0;
    }
    // Each flag costs its code plus one result byte.
    return (bufferSize - overhead) / (BYTES_PER_FLAG_CHECK + 1);
}

std::vector<BYTE> BuildBatchCode(uintptr_t eventFlagMan, BYTE* functionAddr, const std::vector<uint32_t>& flagIds, size_t offset, size_t count, BYTE* resultsAddr) {
    std::vector<BYTE> code;
    for (size_t n = 0; n < count; n++) {
        uint32_t flagId = flagIds[offset + n];
        code.push_back(0x48); code.push_back(0xB9);           // mov rcx, <eventFlagMan>
        AppendBytes(code, eventFlagMan);

        code.push_back(0xBA);                                 // mov edx, <flagId>
        AppendBytes(code, flagId);

        code.push_back(0x48); code.push_back(0x83); code.push_back(0xEC); code.push_back(0x28); // sub rsp, 0x28

        code.push_back(0x48); code.push_back(0xB8);           // mov rax, <functionAddr>
        AppendBytes(code, (uintptr_t)functionAddr);

        code.push_back(0xFF); code.push_back(0xD0);           // call rax

        code.push_back(0x49); code.push_back(0xB8);           // mov r8, <resultsAddr + n>
        AppendBytes(code, (uintptr_t)(resultsAddr + n));

        code.push_back(0x41); code.push_back(0x88); code.push_back(0x00); // mov [r8], al

        code.push_back(0x48); code.push_back(0x83); code.push_back(0xC4); code.push_back(0x28); // add rsp, 0x28
    }
    code.push_back(0xC3); // ret, once, at the very end
    return code;
}

// Same idea as CallGetEventFlag, but checks many flags per trip instead of
// one: one write, one thread, one wait. If the list is too long to fit in the
// buffer it's split into as many passes as needed, so callers can ask for any
// number of flags without having to know the buffer size.
std::vector<uint8_t> CallGetEventFlagsBatch(HANDLE process, BYTE* remoteBuffer, size_t remoteBufferSize, BYTE* functionAddr, uintptr_t eventFlagMan, const std::vector<uint32_t>& flagIds) {
    std::vector<uint8_t> results(flagIds.size(), 0);

    size_t perBatch = FlagsPerBatch(remoteBufferSize);
    if (perBatch == 0) {
        return results; // buffer too small to do anything safely
    }

    for (size_t done = 0; done < flagIds.size(); ) {
        size_t count = flagIds.size() - done;
        if (count > perBatch) {
            count = perBatch;
        }

        // Park the results past the code, with a gap, so the running code
        // can never write over instructions it hasn't reached yet.
        size_t resultsOffset = BatchCodeSize(count) + BATCH_GAP;
        BYTE* resultsAddr = remoteBuffer + resultsOffset;

        std::vector<BYTE> code = BuildBatchCode(eventFlagMan, functionAddr, flagIds, done, count, resultsAddr);

        // Refuse to run anything that doesn't provably fit. Getting this
        // wrong corrupts the game's own thread, so bail rather than guess.
        bool codeFits = code.size() <= resultsOffset;
        bool resultsFit = resultsOffset + count <= remoteBufferSize;
        if (!codeFits || !resultsFit) {
            return results;
        }

        if (!WriteProcessMemory(process, remoteBuffer, code.data(), code.size(), nullptr)) {
            return results;
        }

        HANDLE thread = CreateRemoteThread(process, nullptr, 0, (LPTHREAD_START_ROUTINE)remoteBuffer, nullptr, 0, nullptr);
        if (thread == nullptr) {
            return results;
        }
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);

        SIZE_T bytesRead = 0;
        ReadProcessMemory(process, resultsAddr, results.data() + done, count, &bytesRead);

        done += count;
    }

    return results;
}

// Some values (like the souls count) live at a fixed distance from where the
// game is loaded, but that distance changes between game patches. Each row
// here is one known game version and where its player data starts. Sourced
// from the darksoulsiii-practice-tool project's offset tables.
//
// Boss tracking doesn't need any of this - it finds what it needs by scanning
// for byte patterns, which survive version changes.
struct VersionOffsets {
    WORD major;
    WORD minor;
    WORD patch;
    uintptr_t baseA;
};

const VersionOffsets VERSION_TABLE[] = {
    { 1,  1, 1, 0x4692878 },
    { 1,  3, 1, 0x469adf8 },
    { 1,  3, 2, 0x469bdf8 },
    { 1,  4, 1, 0x469d118 },
    { 1,  4, 2, 0x469d118 },
    { 1,  4, 3, 0x469d118 },
    { 1,  5, 0, 0x46a1218 },
    { 1,  5, 1, 0x46a0218 },
    { 1,  6, 0, 0x46a1278 },
    { 1,  7, 0, 0x46a5ab8 },
    { 1,  8, 0, 0x4704268 },
    { 1,  9, 0, 0x47043a8 },
    { 1, 10, 0, 0x47043a8 },
    { 1, 11, 0, 0x4737698 },
    { 1, 12, 0, 0x473a818 },
    { 1, 13, 0, 0x473e018 },
    { 1, 14, 0, 0x4740178 },
    { 1, 15, 0, 0x4740178 },
    { 1, 15, 1, 0x47572b8 },
    { 1, 15, 2, 0x47572b8 },
};
const int VERSION_COUNT = sizeof(VERSION_TABLE) / sizeof(VERSION_TABLE[0]);

// One-time setup: finds the running game, opens a handle to it, and locates
// SprjEventFlagMan (the game's flag-tracking system) and its get_event_flag
// function. Returns true if everything was found successfully.
struct Ds3Connection {
    HANDLE process = nullptr;
    uintptr_t eventFlagMan = 0;
    BYTE* getEventFlagAddr = nullptr;
    LPVOID remoteBuffer = nullptr;
    size_t remoteBufferSize = 0;
    BYTE* moduleBase = nullptr;

    // Version of the running game, and the matching offset from the table
    // above. baseA stays 0 if we're on a version we don't have offsets for -
    // in that case the souls count is unavailable but bosses still work.
    WORD versionMajor = 0;
    WORD versionMinor = 0;
    WORD versionPatch = 0;
    uintptr_t baseA = 0;
};

const wchar_t* const DS3_PROCESS_NAME = L"DarkSoulsIII.exe";

// Reads the version number out of the running game's .exe file. This is the
// same "File version" you'd see in the file's Properties dialog in Windows.
bool GetGameVersion(HANDLE process, WORD& major, WORD& minor, WORD& patch) {
    wchar_t exePath[MAX_PATH] = {};
    DWORD pathLen = MAX_PATH;
    if (!QueryFullProcessImageNameW(process, 0, exePath, &pathLen)) {
        return false;
    }

    DWORD unusedHandle = 0;
    DWORD infoSize = GetFileVersionInfoSizeW(exePath, &unusedHandle);
    if (infoSize == 0) {
        return false;
    }

    std::vector<BYTE> infoBuffer(infoSize);
    if (!GetFileVersionInfoW(exePath, 0, infoSize, infoBuffer.data())) {
        return false;
    }

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT fileInfoLen = 0;
    if (!VerQueryValueW(infoBuffer.data(), L"\\", (LPVOID*)&fileInfo, &fileInfoLen) || fileInfo == nullptr) {
        return false;
    }

    major = HIWORD(fileInfo->dwFileVersionMS);
    minor = LOWORD(fileInfo->dwFileVersionMS);
    patch = HIWORD(fileInfo->dwFileVersionLS);
    return true;
}

// Looks up the player-data offset for a given game version. Returns 0 if we
// don't have offsets for that version.
uintptr_t LookUpBaseAOffset(WORD major, WORD minor, WORD patch) {
    for (int i = 0; i < VERSION_COUNT; i++) {
        if (VERSION_TABLE[i].major == major &&
            VERSION_TABLE[i].minor == minor &&
            VERSION_TABLE[i].patch == patch) {
            return VERSION_TABLE[i].baseA;
        }
    }
    return 0;
}

bool ConnectToDs3(Ds3Connection& conn) {
    const wchar_t* targetProcess = DS3_PROCESS_NAME;

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

    // Sized so every flag we track fits in a single pass. Each pass means
    // another thread started inside the game, and injecting into a running
    // game is the riskiest thing here - it has closed the game before - so
    // it's worth spending a few unused pages to do it once per tick instead
    // of three times. 64KB covers well over a thousand flags; the lists
    // would have to grow more than sevenfold before this splits again.
    conn.remoteBufferSize = REMOTE_BUFFER_SIZE;
    conn.remoteBuffer = VirtualAllocEx(conn.process, nullptr, conn.remoteBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (conn.remoteBuffer == nullptr) {
        conn.remoteBufferSize = 0;
        return false;
    }

    // Work out which game version is running and pick the matching offsets.
    // An unknown version isn't fatal - boss tracking doesn't depend on this.
    if (GetGameVersion(conn.process, conn.versionMajor, conn.versionMinor, conn.versionPatch)) {
        conn.baseA = LookUpBaseAOffset(conn.versionMajor, conn.versionMinor, conn.versionPatch);
    }

    return true;
}

uint8_t ReadEventFlag(const Ds3Connection& conn, uint32_t flagId) {
    return CallGetEventFlag(conn.process, (BYTE*)conn.remoteBuffer, conn.getEventFlagAddr, conn.eventFlagMan, flagId);
}

// Reads the player's current souls count, following the pointer chain
// discovered on day 1 from the version-appropriate starting offset. Returns
// false if we don't have offsets for the running game version, or if the
// player data isn't loaded yet (e.g. sitting at the main menu).
bool ReadSouls(const Ds3Connection& conn, uint32_t& outSouls) {
    if (conn.baseA == 0) {
        return false;
    }

    uintptr_t baseA = (uintptr_t)conn.moduleBase + conn.baseA;

    SIZE_T bytesRead = 0;
    uintptr_t ptr1 = 0;
    if (!ReadProcessMemory(conn.process, (LPCVOID)baseA, &ptr1, sizeof(ptr1), &bytesRead) || ptr1 == 0) {
        return false;
    }

    uintptr_t ptr2 = 0;
    if (!ReadProcessMemory(conn.process, (LPCVOID)(ptr1 + 0x10), &ptr2, sizeof(ptr2), &bytesRead) || ptr2 == 0) {
        return false;
    }

    uint32_t souls = 0;
    if (!ReadProcessMemory(conn.process, (LPCVOID)(ptr2 + 0x74), &souls, sizeof(souls), &bytesRead)) {
        return false;
    }

    outSouls = souls;
    return true;
}

std::vector<uint8_t> ReadFlags(const Ds3Connection& conn, const std::vector<uint32_t>& flagIds) {
    return CallGetEventFlagsBatch(conn.process, (BYTE*)conn.remoteBuffer, conn.remoteBufferSize,
                                  conn.getEventFlagAddr, conn.eventFlagMan, flagIds);
}

