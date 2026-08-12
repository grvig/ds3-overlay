// Loads the tracked lists (bosses, bonfires, questline steps) from plain text
// files instead of having them compiled in.
//
// The lists only grow from here, and quest data especially will be edited far
// more often than the code around it. Keeping it in files means correcting a
// wrong flag id doesn't mean rebuilding, and means a wrong id can be fixed by
// anyone who spots it.
//
// Format is one entry per line:
//
//     Name | flagId | Group
//
// Blank lines and lines starting with # are ignored. Anything malformed is
// skipped and reported rather than silently dropped - a typo in a data file
// should be findable, not invisible.
#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdint>

struct TrackedEntry {
    std::wstring name;
    uint32_t flag;
    std::wstring group;
};

struct LoadedList {
    std::vector<TrackedEntry> entries;
    std::vector<std::string> problems; // human-readable, with line numbers
    bool fileFound = false;
};

// Data files live beside the executable, like the settings file, so they're
// found no matter what folder the program is launched from.
inline std::wstring GetDataPath(const std::wstring& fileName) {
    wchar_t exePath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return L"data\\" + fileName;
    }

    std::wstring path(exePath, len);
    size_t lastSlash = path.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) {
        return L"data\\" + fileName;
    }
    return path.substr(0, lastSlash + 1) + L"data\\" + fileName;
}

inline std::string TrimAscii(const std::string& text) {
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// The files are plain ASCII - every name in Dark Souls III fits - so this
// widening is enough and avoids dragging in a locale-dependent conversion.
inline std::wstring Widen(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

inline LoadedList LoadTrackedList(const std::wstring& fileName) {
    LoadedList result;

    std::ifstream file(GetDataPath(fileName).c_str());
    if (!file) {
        result.problems.push_back("could not open data file");
        return result;
    }
    result.fileFound = true;

    std::string line;
    int lineNumber = 0;
    bool firstLine = true;
    while (std::getline(file, line)) {
        lineNumber++;

        // Editors save UTF-8 files with a 3-byte marker; left in place it
        // becomes part of the first entry's name.
        if (firstLine && line.size() >= 3 &&
            (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF) {
            line.erase(0, 3);
        }
        firstLine = false;

        std::string trimmed = TrimAscii(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        size_t firstBar = trimmed.find('|');
        size_t secondBar = (firstBar == std::string::npos)
                         ? std::string::npos
                         : trimmed.find('|', firstBar + 1);
        if (firstBar == std::string::npos || secondBar == std::string::npos) {
            result.problems.push_back("line " + std::to_string(lineNumber) +
                                      ": expected 'Name | flagId | Group'");
            continue;
        }

        std::string name = TrimAscii(trimmed.substr(0, firstBar));
        std::string flagText = TrimAscii(trimmed.substr(firstBar + 1, secondBar - firstBar - 1));
        std::string group = TrimAscii(trimmed.substr(secondBar + 1));

        if (name.empty() || group.empty()) {
            result.problems.push_back("line " + std::to_string(lineNumber) +
                                      ": name and group cannot be empty");
            continue;
        }

        uint32_t flag = 0;
        try {
            unsigned long parsed = std::stoul(flagText);
            flag = (uint32_t)parsed;
        } catch (...) {
            result.problems.push_back("line " + std::to_string(lineNumber) +
                                      ": '" + flagText + "' is not a flag id");
            continue;
        }

        TrackedEntry entry;
        entry.name = Widen(name);
        entry.flag = flag;
        entry.group = Widen(group);
        result.entries.push_back(entry);
    }

    return result;
}
