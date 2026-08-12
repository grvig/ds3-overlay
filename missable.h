// Works out which questline rewards have already been lost.
//
// The rule is deliberately narrow: a reward counts as missed when the event
// that closes it off has happened and the reward still isn't yours. Nothing
// here predicts what you're about to do, and nothing warns speculatively -
// a tool that cries wolf gets ignored, and one that wrongly reassures you
// costs a playthrough.
//
// No Windows drawing or game code in here, so tests.cpp can drive it directly.
#pragma once

#include "datafile.h"

#include <map>
#include <string>
#include <vector>

struct MissableRule {
    std::wstring reward;      // what you lose
    uint32_t rewardFlag = 0;  // set once you have it
    uint32_t blockerFlag = 0; // set once the window has closed
    std::wstring blockedBy;   // what closed it, for the message
};

struct LoadedRules {
    std::vector<MissableRule> rules;
    std::vector<std::string> problems;
    bool fileFound = false;
};

inline LoadedRules LoadMissableRules(const std::wstring& fileName) {
    LoadedRules result;

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

        // Split into exactly four fields.
        std::vector<std::string> fields;
        size_t start = 0;
        while (true) {
            size_t bar = trimmed.find('|', start);
            if (bar == std::string::npos) {
                fields.push_back(TrimAscii(trimmed.substr(start)));
                break;
            }
            fields.push_back(TrimAscii(trimmed.substr(start, bar - start)));
            start = bar + 1;
        }

        if (fields.size() != 4) {
            result.problems.push_back("line " + std::to_string(lineNumber) +
                ": expected 'Reward | its flag | closing flag | what closes it'");
            continue;
        }
        if (fields[0].empty() || fields[3].empty()) {
            result.problems.push_back("line " + std::to_string(lineNumber) +
                ": reward and closing-event names cannot be empty");
            continue;
        }

        MissableRule rule;
        try {
            rule.rewardFlag = (uint32_t)std::stoul(fields[1]);
            rule.blockerFlag = (uint32_t)std::stoul(fields[2]);
        } catch (...) {
            result.problems.push_back("line " + std::to_string(lineNumber) +
                ": flag ids must be numbers");
            continue;
        }
        if (rule.rewardFlag == rule.blockerFlag) {
            result.problems.push_back("line " + std::to_string(lineNumber) +
                ": a reward cannot be closed off by its own flag");
            continue;
        }

        rule.reward = Widen(fields[0]);
        rule.blockedBy = Widen(fields[3]);
        result.rules.push_back(rule);
    }

    return result;
}

// Every flag a set of rules depends on, so the caller can read them all in
// one batch rather than working out the list by hand.
inline std::vector<uint32_t> FlagsUsedByRules(const std::vector<MissableRule>& rules) {
    std::vector<uint32_t> flags;
    for (size_t i = 0; i < rules.size(); i++) {
        flags.push_back(rules[i].rewardFlag);
        flags.push_back(rules[i].blockerFlag);
    }
    return flags;
}

struct MissedThing {
    std::wstring reward;
    std::wstring blockedBy;

    std::wstring Describe() const {
        return reward + L" - lost to " + blockedBy;
    }
};

// A rule only fires when both its flags are actually known. An unread flag is
// treated as "no opinion" rather than assumed false, so a partial read can
// never invent a warning.
inline std::vector<MissedThing> FindMissed(const std::vector<MissableRule>& rules,
                                           const std::map<uint32_t, bool>& flagState) {
    std::vector<MissedThing> missed;
    for (size_t i = 0; i < rules.size(); i++) {
        const MissableRule& rule = rules[i];

        auto blocker = flagState.find(rule.blockerFlag);
        auto reward = flagState.find(rule.rewardFlag);
        if (blocker == flagState.end() || reward == flagState.end()) {
            continue;
        }

        if (blocker->second && !reward->second) {
            MissedThing thing;
            thing.reward = rule.reward;
            thing.blockedBy = rule.blockedBy;
            missed.push_back(thing);
        }
    }
    return missed;
}
