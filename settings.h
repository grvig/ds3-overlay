// Reads and writes the overlay's settings file, so things like where the
// overlay sits on screen can be changed without rebuilding.
//
// The file is plain "key=value" lines and lives next to overlay.exe. It's
// created automatically the first time settings are saved; if it's missing
// or unreadable the built-in defaults are used, so a bad file can never
// stop the overlay from starting.
#pragma once

#include <windows.h>
#include <string>
#include <fstream>
#include <sstream>

struct OverlaySettings {
    int x = 10;
    int y = 10;
};

// Settings live beside the executable rather than in the current working
// directory, so the overlay finds them no matter where it's launched from.
std::wstring GetSettingsPath() {
    wchar_t exePath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return L"overlay-settings.txt";
    }

    std::wstring path(exePath, len);
    size_t lastSlash = path.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) {
        return L"overlay-settings.txt";
    }
    return path.substr(0, lastSlash + 1) + L"overlay-settings.txt";
}

// Splits a "key=value" line. Returns false for blanks, comments, and
// anything malformed, which are all simply skipped.
bool ParseSettingLine(const std::string& line, std::string& outKey, std::string& outValue) {
    std::string trimmed = line;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    if (trimmed.empty() || trimmed[0] == '#') {
        return false;
    }

    size_t equals = trimmed.find('=');
    if (equals == std::string::npos) {
        return false;
    }

    outKey = trimmed.substr(0, equals);
    outValue = trimmed.substr(equals + 1);

    outKey.erase(outKey.find_last_not_of(" \t\r\n") + 1);
    outValue.erase(0, outValue.find_first_not_of(" \t\r\n"));
    outValue.erase(outValue.find_last_not_of(" \t\r\n") + 1);
    return !outKey.empty() && !outValue.empty();
}

// Reads an integer setting, leaving the target untouched if the value isn't
// a valid number - so one bad line doesn't discard a good setting.
void ApplyIntSetting(const std::string& value, int& target) {
    try {
        target = std::stoi(value);
    } catch (...) {
        // Leave the existing value alone.
    }
}

OverlaySettings LoadSettings() {
    OverlaySettings settings;

    std::ifstream file(GetSettingsPath().c_str());
    if (!file) {
        return settings;
    }

    std::string line;
    bool firstLine = true;
    while (std::getline(file, line)) {
        // Notepad and friends save UTF-8 files with a 3-byte marker at the
        // very start. Left in place it becomes part of the first key name,
        // silently ignoring whatever setting happens to be on line one.
        if (firstLine && line.size() >= 3 &&
            (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF) {
            line.erase(0, 3);
        }
        firstLine = false;

        std::string key, value;
        if (!ParseSettingLine(line, key, value)) {
            continue;
        }
        if (key == "x") {
            ApplyIntSetting(value, settings.x);
        } else if (key == "y") {
            ApplyIntSetting(value, settings.y);
        }
    }

    return settings;
}

bool SaveSettings(const OverlaySettings& settings) {
    std::ofstream file(GetSettingsPath().c_str(), std::ios::trunc);
    if (!file) {
        return false;
    }

    file << "# DS3 overlay settings. Delete this file to go back to defaults.\n";
    file << "# x, y = where the top-left corner of the overlay sits on screen.\n";
    file << "x=" << settings.x << "\n";
    file << "y=" << settings.y << "\n";
    return file.good();
}
