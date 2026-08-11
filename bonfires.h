// Every bonfire in the base game and both DLCs, and the event flag that says
// whether it's been lit. Just data - no memory-reading logic - same as
// bosses.h.
//
// Flag IDs come from the SoulSplitter project's bonfire list, cross-checked
// against The Grand Archives' cheat table. Where the two disagreed, this
// list follows SoulSplitter: the cheat table reuses flag 13300004 for both
// Keep Ruins and Abyss Watchers (Abyss Watchers is really 13300001) and is
// missing Farron Keep Perimeter entirely.
#pragma once

#include <cstdint>

struct BonfireInfo {
    const wchar_t* name;
    uint32_t litFlag;
    const wchar_t* area;
};

// In roughly the order you'd reach them. The overlay draws a header each
// time the area changes, so bonfires in the same area must stay together.
const BonfireInfo BONFIRE_LIST[] = {
    { L"Firelink Shrine", 14000000, L"Cemetery of Ash" },
    { L"Cemetery of Ash", 14000001, L"Cemetery of Ash" },
    { L"Iudex Gundyr", 14000002, L"Cemetery of Ash" },
    { L"Untended Graves", 14000003, L"Cemetery of Ash" },
    { L"Champion Gundyr", 14000004, L"Cemetery of Ash" },
    { L"High Wall of Lothric", 13000009, L"High Wall of Lothric" },
    { L"Tower on the Wall", 13000005, L"High Wall of Lothric" },
    { L"Vordt of the Boreal Valley", 13000002, L"High Wall of Lothric" },
    { L"Dancer of the Boreal Valley", 13000004, L"High Wall of Lothric" },
    { L"Oceiros, the Consumed King", 13000001, L"High Wall of Lothric" },
    { L"Foot of the High Wall", 13100004, L"Undead Settlement" },
    { L"Undead Settlement", 13100000, L"Undead Settlement" },
    { L"Cliff Underside", 13100002, L"Undead Settlement" },
    { L"Dilapidated Bridge", 13100003, L"Undead Settlement" },
    { L"Pit of Hollows", 13100001, L"Undead Settlement" },
    { L"Road of Sacrifices", 13300006, L"Road of Sacrifices" },
    { L"Halfway Fortress", 13300000, L"Road of Sacrifices" },
    { L"Crucifixion Woods", 13300007, L"Road of Sacrifices" },
    { L"Crystal Sage", 13300002, L"Road of Sacrifices" },
    { L"Farron Keep", 13300003, L"Road of Sacrifices" },
    { L"Keep Ruins", 13300004, L"Road of Sacrifices" },
    { L"Farron Keep Perimeter", 13300008, L"Road of Sacrifices" },
    { L"Old Wolf of Farron", 13300005, L"Road of Sacrifices" },
    { L"Abyss Watchers", 13300001, L"Road of Sacrifices" },
    { L"Cathedral of the Deep", 13500003, L"Cathedral of the Deep" },
    { L"Cleansing Chapel", 13500000, L"Cathedral of the Deep" },
    { L"Rosaria's Bed Chamber", 13500002, L"Cathedral of the Deep" },
    { L"Deacons of the Deep", 13500001, L"Cathedral of the Deep" },
    { L"Catacombs of Carthus", 13800006, L"Catacombs of Carthus" },
    { L"High Lord Wolnir", 13800000, L"Catacombs of Carthus" },
    { L"Abandoned Tomb", 13800001, L"Catacombs of Carthus" },
    { L"Old King's Antechamber", 13800002, L"Catacombs of Carthus" },
    { L"Demon Ruins", 13800003, L"Catacombs of Carthus" },
    { L"Old Demon King", 13800004, L"Catacombs of Carthus" },
    { L"Irithyll of the Boreal Valley", 13700007, L"Irithyll of the Boreal Valley" },
    { L"Central Irithyll", 13700004, L"Irithyll of the Boreal Valley" },
    { L"Church of Yorshka", 13700000, L"Irithyll of the Boreal Valley" },
    { L"Distant Manor", 13700005, L"Irithyll of the Boreal Valley" },
    { L"Pontiff Sulyvahn", 13700001, L"Irithyll of the Boreal Valley" },
    { L"Water Reserve", 13700006, L"Irithyll of the Boreal Valley" },
    { L"Anor Londo", 13700003, L"Irithyll of the Boreal Valley" },
    { L"Prison Tower", 13700008, L"Irithyll of the Boreal Valley" },
    { L"Aldrich, Devourer of Gods", 13700002, L"Irithyll of the Boreal Valley" },
    { L"Irithyll Dungeon", 13900000, L"Irithyll Dungeon" },
    { L"Profaned Capital", 13900002, L"Irithyll Dungeon" },
    { L"Yhorm the Giant", 13900001, L"Irithyll Dungeon" },
    { L"Lothric Castle", 13010000, L"Lothric Castle" },
    { L"Dragon Barracks", 13010002, L"Lothric Castle" },
    { L"Dragonslayer Armour", 13010001, L"Lothric Castle" },
    { L"Grand Archives", 13410001, L"Lothric Castle" },
    { L"Twin Princes", 13410000, L"Lothric Castle" },
    { L"Archdragon Peak", 13200000, L"Archdragon Peak" },
    { L"Dragon-Kin Mausoleum", 13200003, L"Archdragon Peak" },
    { L"Great Belfry", 13200002, L"Archdragon Peak" },
    { L"Nameless King", 13200001, L"Archdragon Peak" },
    { L"Flameless Shrine", 14100000, L"Kiln of the First Flame" },
    { L"Kiln of the First Flame", 14100001, L"Kiln of the First Flame" },
    { L"Snowfield", 14500001, L"The Painted World of Ariandel" },
    { L"Rope Bridge Cave", 14500002, L"The Painted World of Ariandel" },
    { L"Corvian Settlement", 14500003, L"The Painted World of Ariandel" },
    { L"Snowy Mountain Pass", 14500004, L"The Painted World of Ariandel" },
    { L"Ariandel Chapel", 14500005, L"The Painted World of Ariandel" },
    { L"Sister Friede", 14500000, L"The Painted World of Ariandel" },
    { L"Depths of the Painting", 14500007, L"The Painted World of Ariandel" },
    { L"Champion's Gravetender", 14500006, L"The Painted World of Ariandel" },
    { L"The Dreg Heap", 15000001, L"The Dreg Heap" },
    { L"Earthen Peak Ruins", 15000002, L"The Dreg Heap" },
    { L"Within the Earthen Peak Ruins", 15000003, L"The Dreg Heap" },
    { L"The Demon Prince", 15000000, L"The Dreg Heap" },
    { L"Mausoleum Lookout", 15100002, L"The Ringed City" },
    { L"Ringed Inner Wall", 15100003, L"The Ringed City" },
    { L"Ringed City Streets", 15100004, L"The Ringed City" },
    { L"Shared Grave", 15100005, L"The Ringed City" },
    { L"Church of Filianore", 15100000, L"The Ringed City" },
    { L"Darkeater Midir", 15100001, L"The Ringed City" },
    { L"Filianore's Rest", 15110001, L"The Ringed City" },
    { L"Slave Knight Gael", 15110000, L"The Ringed City" },
};
const int BONFIRE_COUNT = sizeof(BONFIRE_LIST) / sizeof(BONFIRE_LIST[0]);
