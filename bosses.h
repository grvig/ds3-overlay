// The list of bosses the overlay tracks - just data, no memory-reading logic.
// Kept separate from ds3reader.h so adding or reordering bosses doesn't mean
// touching the code that talks to the game.
//
// Flag IDs are sourced from The Grand Archives' public DS3 Cheat Engine table.
#pragma once

#include <cstdint>

struct BossInfo {
    const wchar_t* name;
    uint32_t defeatedFlag;
    const wchar_t* section;
};

// Listed in roughly the order you'd encounter them. The overlay draws a
// header each time the section changes, so bosses in the same section need
// to stay next to each other.
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
