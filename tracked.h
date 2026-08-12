// The lists the overlay tracks, loaded from data/ at startup.
//
// These used to be arrays compiled into the program (bosses.h, bonfires.h).
// They live in text files now so they can be corrected or extended without a
// rebuild, which matters most for the questline data - that's authored by
// hand and will be wrong before it's right.
#pragma once

#include "datafile.h"

#include <string>
#include <vector>

struct TrackedLists {
    std::vector<TrackedEntry> bosses;
    std::vector<TrackedEntry> bonfires;

    // Questline reward items. Whether a reward has been handed over is a
    // usable stand-in for how far that NPC's questline actually got.
    std::vector<TrackedEntry> quests;

    // Anything wrong with the data files, with file and line, so a bad entry
    // can be found instead of quietly going missing.
    std::vector<std::string> problems;

    bool AnyProblems() const { return !problems.empty(); }
};

inline TrackedLists g_tracked;

inline void LoadOneInto(TrackedLists& into, std::vector<TrackedEntry>& target,
                        const wchar_t* fileName, const char* label) {
    LoadedList loaded = LoadTrackedList(fileName);
    target = loaded.entries;
    for (size_t i = 0; i < loaded.problems.size(); i++) {
        into.problems.push_back(std::string(label) + ": " + loaded.problems[i]);
    }
    if (loaded.fileFound && loaded.entries.empty()) {
        into.problems.push_back(std::string(label) + ": file is empty");
    }
}

inline void LoadTrackedLists() {
    g_tracked = TrackedLists();
    LoadOneInto(g_tracked, g_tracked.bosses, L"bosses.txt", "bosses.txt");
    LoadOneInto(g_tracked, g_tracked.bonfires, L"bonfires.txt", "bonfires.txt");
    LoadOneInto(g_tracked, g_tracked.quests, L"quests.txt", "quests.txt");
}

// Pulls just the flag ids out of a list, for handing to the batch reader.
inline std::vector<uint32_t> FlagsOf(const std::vector<TrackedEntry>& entries) {
    std::vector<uint32_t> flags;
    flags.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); i++) {
        flags.push_back(entries[i].flag);
    }
    return flags;
}
