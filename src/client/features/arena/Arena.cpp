// Ground battles: /arena <enemies> <place> <waves> (the host; or alone, offline).
//
//   /arena yakuza random 5      /arena баллас притон 3      /arena list      /arena stop      /skip
//
// Every player is taken into the place: an interior of the story's missions (Big Smoke's crack palace, Madd Dogg's
// mansion, the casinos, the meat factory, the stadiums...) or a random one of them. Waves of the chosen gang or force
// come at them there (more each wave, with better guns, aim and armour); a wave is beaten when every one of it is
// down, and the next comes 6 s later.
// They come in squads of 3-4 from all over the place, away from the players and from where the last wave came (the
// host's game works out the place's floor first), some going round to the players' backs. Each squad has a grenadier
// who throws when the arc to the player is clear; knifemen come alone and steal up to a back out of the players' sight
// (StealthKnife kills from there); the last two waves bring snipers who shoot from far off. They know no fear and
// always know where the players are: out of sight they come along the floor, in sight they fight, take cover, wait
// and push on all together.
// The last wave brings a boss: Tenpenny, Pulaski, Hernandez, Big Smoke, Ryder, OG Loc, T-Bone, Jizzy or Toreno (one
// that fits the enemies, when there is one) with an AK-47 (a gang's) or an M4 (the state's), and with him, once and
// unannounced, his men: they say nothing and work round the players. All waves beaten: everyone gets $1000 a wave
// and is taken back where it stood. Everyone dead or out of the place: lost. /arena stop ends it. The place's doors
// out are shut meanwhile, and the armour of the battle is gone after it.
// No wanted stars meanwhile, and the weapons the players took up there they do not keep (game::challenge); the enemies
// drop none.
//
// The host's game makes the enemies (they come to the others as its peds do); it counts them by its own handles and
// tells the others how many are left. The boss is one of the game's special characters (slot 10, model 299): every
// game loads the same one into that slot first, so that its copy has the right look there too (G_ARENA).
#include <windows.h>
#undef GetObject  // <windows.h>'s, not the game's object handles

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "features/arena/Arena.h"

#include "common/api/protocol/Protocol.h"
#include "common/lib/ini/Ini.h"
#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "entities/ped/Peds.h"
#include "entities/player/Players.h"
#include "entities/radar-icons/RadarIcons.h"
#include "entities/stealth-knife/StealthKnife.h"
#include "features/progress/SkinChoice.h"
#include "features/revive/Revive.h"
#include "entities/session/Flow.h"
#include "pages/connect-menu/ConnectMenu.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Challenge.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/lib/memory/Memory.h"
#include "shared/ui/chat/Chat.h"
#include "shared/ui/marks/Marks.h"
#include "shared/ui/renderer/Renderer.h"
#include "shared/ui/widgets/Widgets.h"

namespace coop {

namespace {
using namespace game;
using game::script::Cmd;

constexpr int PED_TYPE_MISSION1 = 24;
constexpr int kBossSlot = 10, kBossModel = 290 + kBossSlot - 1;  // the special characters' models: 290-299
int g_knifeModel = -1;  // that special character, when the game has it
std::mt19937 g_rng{(uint32_t)GetTickCount() ^ 0xA2E7u};
int RandInt(int a, int b) { return a + (int)(g_rng() % (uint32_t)(b - a + 1)); }
float Rand(float a, float b) { return a + (b - a) * (float)(g_rng() % 10001) / 10000.0f; }

// ---------------------------------------------------------------- what can be chosen
struct Faction {
    const char* keys[2][2];  // what may be typed (lower case): in Russian, in English
    const char* name[2];
    int models[4];
    int weapons[4][2];  // by wave: the first ones get the first, later ones better (one of two at each step)
    int boss[3];        // the bosses that fit (index into kBosses, -1: none)
    bool medics;        // its squads have medics (every faction now)
    bool flamers;       // its waves bring a flamethrower man (every faction now)
};
// weapons: 22 pistol, 23 silenced, 24 desert eagle, 25 shotgun, 26 sawn-off, 27 combat shotgun, 28 uzi, 29 mp5,
// 30 ak-47, 31 m4, 32 tec-9, 33 country rifle
const Faction kFactions[] = {
    {{TRP("якудза", "yakuza"), TRP("дананг", "danang")}, TRP("Якудза", "Yakuza"), {121, 122, 123, 186}, {{22, 23}, {28, 32}, {29, 26}, {30, 31}}, {7, -1, -1}, true, true},
    {{TRP("баллас", "ballas"), TRP("балласы", "ballas")}, TRP("Баллас", "Ballas"), {102, 103, 104, 102}, {{22, 24}, {32, 28}, {25, 26}, {30, 29}}, {3, 4, 5}, true, true},
    {{TRP("вагос", "vagos"), TRP("вагос", "vagos")}, TRP("Вагос", "Vagos"), {108, 109, 110, 108}, {{22, 24}, {32, 28}, {25, 26}, {30, 27}}, {3, 5, -1}, true, true},
    {{TRP("ацтекас", "aztecas"), TRP("ацтеки", "aztecas")}, TRP("Ацтекас", "Aztecas"), {114, 115, 116, 114}, {{22, 24}, {28, 32}, {25, 26}, {30, 29}}, {6, -1, -1}, true, true},
    {{TRP("триады", "triads"), TRP("триада", "triads")}, TRP("Триады", "Triads"), {117, 118, 120, 117}, {{22, 23}, {28, 32}, {29, 27}, {30, 31}}, {7, -1, -1}, true, true},
    {{TRP("мафия", "mafia"), TRP("мафия", "mafia")}, TRP("Мафия", "Mafia"), {111, 112, 113, 111}, {{22, 24}, {29, 28}, {27, 25}, {30, 31}}, {8, 0, -1}, true, true},
    {{TRP("рифа", "rifa"), TRP("рифа", "rifa")}, TRP("Сан-Фиеро Рифа", "San Fierro Rifa"), {173, 174, 175, 173}, {{22, 24}, {32, 28}, {25, 26}, {30, 29}}, {6, -1, -1}, true, true},
    {{TRP("байкеры", "bikers"), TRP("байкеры", "bikers")}, TRP("Байкеры", "Bikers"), {247, 248, 254, 100}, {{22, 24}, {25, 26}, {30, 33}, {27, 30}}, {-1, -1, -1}, true, true},
    {{TRP("полиция", "police"), TRP("копы", "cops")}, TRP("Полиция", "Police"), {280, 281, 282, 284}, {{22, 24}, {25, 29}, {29, 27}, {31, 27}}, {0, 1, 2}, true, true},
    {{TRP("спецназ", "swat"), TRP("фбр", "fbi")}, TRP("Спецназ", "SWAT"), {285, 286, 285, 286}, {{29, 24}, {29, 27}, {31, 27}, {31, 29}}, {0, 1, 8}, true, true},
    {{TRP("армия", "army"), TRP("военные", "army")}, TRP("Армия", "Army"), {287, 287, 287, 287}, {{29, 31}, {31, 30}, {31, 27}, {31, 33}}, {8, -1, -1}, true, true},
};
struct Boss {
    const char* special;  // the special character's name
    const char* name[2];
    int weapon;  // a gang's boss: the AK-47 (30), the state's: the M4 (31)
};
const Boss kBosses[] = {
    {"TENPEN", TRP("Тенпенни", "Tenpenny"), 31},           // 0
    {"PULASKI", TRP("Пуласки", "Pulaski"), 31},            // 1
    {"HERN", TRP("Эрнандес", "Hernandez"), 31},            // 2
    {"SMOKEV", TRP("Биг Смоук", "Big Smoke"), 30},         // 3
    {"RYDER", TRP("Райдер", "Ryder"), 30},                 // 4
    {"OGLOC", TRP("Оу Джи Лок", "OG Loc"), 30},            // 5
    {"TBONE", TRP("Ти-Бон Мендез", "T-Bone Mendez"), 30},  // 6
    {"JIZZY", TRP("Джиззи Би", "Jizzy B"), 30},            // 7
    {"TORENO", TRP("Майк Торено", "Mike Toreno"), 31},     // 8
};
constexpr int kBossCount = (int)(sizeof(kBosses) / sizeof(kBosses[0]));
constexpr int kTBone = 6;  // T-Bone Mendez: the AK-47 with a grenade launcher under it

struct Place {
    const char* keys[2][2];  // in Russian, in English (the second Russian word with the same English one)
    const char* name[2];
    int area;
    CVector pos;
    float reach;  // how far from the middle the enemies may come from
    bool big;     // a big place: 30 s between the waves (20 s before the first), more to pick up
    bool soda;    // with drinks machines (health there): less armour lying about
};
const Place kPlaces[] = {
    {{TRP("притон", "crack"), TRP("смоук", "crack")}, TRP("Крэк-дворец Биг Смоука", "Big Smoke's crack palace"), 2, {2543.46f, -1308.38f, 1026.73f}, 14.0f, false, false},
    {{TRP("особняк", "mansion"), TRP("мэддогг", "mansion")}, TRP("Особняк Мэдд Догга", "Madd Dogg's mansion"), 5, {1267.66f, -781.32f, 1091.91f}, 18.0f, true, false},
    {{TRP("мясокомбинат", "meat"), TRP("бойня", "meat")}, TRP("Мясокомбинат", "The meat factory"), 1, {963.42f, 2108.29f, 1011.03f}, 16.0f, true, false},
    {{TRP("мотель", "motel"), TRP("джефферсон", "motel")}, TRP("Мотель «Джефферсон»", "The Jefferson motel"), 15, {2215.45f, -1147.48f, 1025.80f}, 20.0f, true, true},
    {{TRP("либерти", "liberty"), TRP("бистро", "liberty")}, TRP("Либерти-Сити (бистро Марко)", "Liberty City (Marco's bistro)"), 1, {-794.81f, 497.74f, 1376.20f}, 14.0f, false, false},
    {{TRP("полиция", "police"), TRP("участок", "police")}, TRP("Полицейский участок", "The police station"), 6, {246.78f, 63.90f, 1003.64f}, 14.0f, false, true},
    {{TRP("дамба", "dam"), TRP("шерман", "dam")}, TRP("Дамба Шерман", "The Sherman dam"), 17, {-959.56f, 1848.58f, 9.00f}, 30.0f, true, false},
    {{TRP("кикстарт", "kickstart"), TRP("кикстарт", "kickstart")}, TRP("Стадион «Кикстарт»", "The Kickstart stadium"), 14, {-1465.27f, 1557.87f, 1052.53f}, 35.0f, true, true},
};
constexpr int kPlaceCount = (int)(sizeof(kPlaces) / sizeof(kPlaces[0]));
constexpr int kFactionCount = (int)(sizeof(kFactions) / sizeof(kFactions[0]));

// lower case, the Cyrillic letters too (А-Я, Ё in UTF-8)
std::string LowerRu(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        uint8_t c = (uint8_t)in[i];
        if (c == 0xD0 && i + 1 < in.size()) {
            uint8_t d = (uint8_t)in[i + 1];
            if (d >= 0x90 && d <= 0x9F) out += (char)0xD0, out += (char)(d + 0x20);        // А-П
            else if (d >= 0xA0 && d <= 0xAF) out += (char)0xD1, out += (char)(d - 0x20);   // Р-Я
            else if (d == 0x81) out += (char)0xD1, out += (char)0x91;                      // Ё
            else out += (char)c, out += (char)d;
            ++i;
        } else {
            out += (char)std::tolower(c);
        }
    }
    return out;
}
bool Matches(const std::string& word, const char* const (&keys)[2][2]) {
    for (auto& pair : keys)
        for (const char* k : pair)
            if (k && word == k) return true;
    return false;
}
bool Matches(const std::string& word, const char* const (&pair)[2]) { return word == pair[0] || word == pair[1]; }
// the commands' words
const char* const kWordList[2] = TRP("список", "list");
const char* const kWordStop[2] = TRP("стоп", "stop");
const char* const kWordRandom[2] = TRP("рандом", "random");
const char* const kWordAny[2] = TRP("случайно", "any");
const char* const kWordArena[2] = TRP("арена", "arena");
const char* const kWordSkip[2] = TRP("скип", "skip");
const char* const kWordEndless[2] = TRP("бесконечно", "endless");
int Lang() { return Cfg().english ? 1 : 0; }

// ---------------------------------------------------------------- the battle
// A_SCORE .. A_TWINS: the zombies mode (points, barricades, the box, the perks, the power-ups, the Twins' look)
enum ArenaOp : uint8_t {
    A_START = 1, A_WAVE, A_LEFT, A_BOSS, A_END, A_ITEMS, A_PREP, A_MARK, A_SKIP,
    A_SCORE, A_BARS, A_BOARD, A_REPAIR, A_BOX, A_BOXUSE, A_BOXROLL, A_PERKS, A_PSPAWN, A_GRAB, A_POWER, A_TWINS,
    A_WALLS, A_DROP, A_TAKE, A_TAKEN, A_BUILD, A_LOBBY, A_FLAMER, A_STAGE, A_VOTE, A_VOTES, A_GIVE, A_MAPVOTE, A_MOVE,
    A_HEALGAS, A_BOSSNAME, A_HIT, A_SHELL, A_SPEC, A_BOXTAKEN
};
struct Arena {
    bool on = false, host = false;
    int faction = 0, place = 0, waves = 5, wave = 0, left = 0, boss = -1;
    uint64_t startMs = 0, nextWaveMs = 0, waveMs = 0, outSince = 0, endAt = 0;
    int itemsFor = 0;  // host: the wave whose weapons and armour are to be laid out (once the floor is known)
    bool won = false;
    // where this player stood, to go back
    int backArea = 0;
    CVector back;
    float backHeading = 0;
    bool taken = false;
} g_arena;
std::set<CPed*> g_enemies;  // the host's: this wave's (and the boss)
CPed* g_bossPed = nullptr;
CPed* g_twinPeds[2] = {nullptr, nullptr};  // the host's: the mini-boss of this wave
CPed* g_sawPed = nullptr;                    // the host's: the Butcher (the chainsaw)
CPed* g_officerPed = nullptr;                // the host's: the Officer
// a partner's: the copies of the Butcher [0] and the Officer [1], and their net ids till the copies are here
CPed* g_tagCopies[2] = {nullptr, nullptr};
uint16_t g_tagIds[2] = {0, 0};
uint64_t g_tagIdsUntil = 0;
bool g_fireWave = false;
// the spectators: whoever fell in the arena watches a team mate till the next wave (every game knows who: A_SPEC)
bool g_spectating = false;
std::set<uint8_t> g_spectators;
uint8_t g_specTarget = 0xFF;
bool Spectator(uint8_t id) { return g_spectators.count(id) != 0; }
// this player has T-Bone's AK with the grenade launcher: loaded, or loading till then
bool g_hasUgl = false, g_uglLoaded = false;
uint64_t g_uglReadyMs = 0;                     // the 10% event: this wave is four flamethrower men and nothing else
bool g_lobby = false, g_modeLost = false;  // the arena's game mode: in its lobby now; the last one lost (GameMode)
bool g_modeOn = false;  // this game plays the arena's game mode (the host's choice; a partner learns it from A_STAGE)
void WearChosen();
void OpenLobby(bool open);
std::vector<std::pair<int, uint8_t>> g_hiddenSaves;  // the save disks hidden meanwhile (HideSaves): the pickup, its type
// a partner's: the Twins' copies (by index: Otto, Ernando) and their net ids till the copies are here
std::vector<uint16_t> g_twinIds;  // by index: Otto, Ernando
uint64_t g_twinIdsUntil = 0;
CPed* g_twinCopies[2] = {nullptr, nullptr};
std::vector<CPed*> g_flamers;              // the host's: this wave's flamethrower men

// waves = 0: no end (the zombies' rounds): a boss every 5th wave, the Twins every 5th from the 3rd
bool Endless() { return g_arena.waves == 0; }
float Progress(int w) {
    if (Endless()) return std::min(1.0f, (float)(w - 1) / 9.0f);
    return g_arena.waves > 1 ? (float)(w - 1) / (float)(g_arena.waves - 1) : 1.0f;
}
bool BossWave(int w) { return Endless() ? w % 5 == 0 : w == g_arena.waves; }
int MiniBoss(int w);
bool TwinsWave(int w) { return MiniBoss(w) == 1; }
// the mini-bosses: 1 the Twins, 2 the Butcher, 3 the Officer, 0 none (never in the boss's wave)
int MiniBoss(int w) {
    if (Endless()) return w % 5 == 3 ? 1 : w % 5 == 4 ? 2 : w % 5 == 2 && w >= 7 ? 3 : 0;
    int n = g_arena.waves;
    if (n < 3 || w < 2 || w >= n) return 0;
    int twins = (n + 1) / 2, saw = n - 1, officer = 2;
    if (w == twins) return 1;
    if (n >= 4 && w == saw) return 2;
    if (n >= 5 && w == officer) return 3;
    return 0;
}
bool SniperWave(int w) { return Endless() ? w >= 4 : w >= std::max(1, g_arena.waves - 1); }
std::string WaveOf(int w) {
    return Endless() ? Format(TR("волна %d", "wave %d"), w) : Format(TR("волна %d из %d", "wave %d of %d"), w, g_arena.waves);
}

// ---- the barricades (shared by every game: the host decides their boards)
constexpr int kBoards = 6;
struct Bar {
    CVector pos;    // its floor
    CVector spawn;  // the floor behind it, where the enemies come from
    float heading = 0;
    int boards = kBoards;
    int handle = -1;  // this game's object
    float lift = 0, height = 1.2f;
    uint64_t tearMs = 0;
    int usedWave = 0;  // host: the last wave that came through it
    bool built = false;  // a player built it (no one comes from behind it)
    int breaks = 0;      // host: how often shots smashed its fence (3 smashes: a board)
};
std::vector<Bar> g_bars;
// the wait between the waves (every game): the barricades let everyone through, a vote (Y) skips it
bool g_break = true;
uint64_t g_breakEndMs = 0;
bool g_barsSolid = true;
std::set<uint8_t> g_votes;    // host: who voted to skip
std::set<uint8_t> g_mapVotes; // host: who voted for another place
int g_voteCount = 0, g_voteNeed = 1, g_mapVoteCount = 0;
bool g_iVoted = false, g_iMapVoted = false;
void MapVote(uint8_t by);
void Relocate(int place);
void SendVotes();
void AddGas(const CVector& from, const CVector& vel);
void SetBreak(bool on, uint64_t endMs);
void Vote(uint8_t by);

bool Hosting() { return !Net().Online() || Net().IsHost(); }

void Send(ArenaOp op, const std::function<void(Writer&)>& body = nullptr) {
    if (!Net().Online()) return;
    Writer w(proto::G_ARENA);
    w.U8(op);
    if (body) body(w);
    Net().Send(proto::CH_RELIABLE, w);
}

std::string FactionName(int f) { return kFactions[f].name[Lang()]; }
std::string PlaceName(int p) { return kPlaces[p].name[Lang()]; }
std::string BossName(int b) { return b >= 0 && b < kBossCount ? std::string(kBosses[b].name[Lang()]) : std::string(); }

void Teleport(int area, const CVector& p, float heading) {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me) return;
    int h = GetPedRef(me);
    if (CVehicle* in = me->VehicleIfIn()) Cmd(0x0362).I(h).F(in->GetPosition().x).F(in->GetPosition().y).F(in->GetPosition().z + 1.0f).Run();  // WARP_CHAR_FROM_CAR_TO_COORD
    Cmd(0x04BB).I(area).Run();                         // SET_AREA_VISIBLE
    Cmd(0x0860).I(h).I(area).Run();                    // SET_CHAR_AREA_VISIBLE
    Cmd(0x03CB).F(p.x).F(p.y).F(p.z).Run();            // LOAD_SCENE: its ground first
    Cmd(0x00A1).I(h).F(p.x).F(p.y).F(p.z - 1.0f).Run();  // SET_CHAR_COORDINATES
    Cmd(0x0173).I(h).F(heading).Run();                 // SET_CHAR_HEADING
    LightArea(area, p);
}

// The place's doors out (its entrances' markers) are shut while the battle is on: SET_INTERIOR_ACCESS off by the
// names of the entrances in it (a name is both sides of a door), and every entrance's flags as they were afterwards.
std::vector<std::pair<int, uint16_t>> g_enexFlags;  // pool index, CEntryExit::m_nFlags
void ShutDoors(bool shut) {
    auto* pool = Ref<CPoolHeader*>(0x96A7D8);  // CEntryExitManager::mp_poolEntryExits, CEntryExit 0x3C bytes
    if (!pool) return;
    if (!shut) {
        for (auto& [i, flags] : g_enexFlags)
            if (i < pool->size && pool->byteMap[i] >= 0) Ref<uint16_t>((uintptr_t)pool->objects + (uintptr_t)i * 0x3C + 0x30) = flags;
        if (!g_enexFlags.empty()) LOGI("Arena: the place's doors open again");
        g_enexFlags.clear();
        return;
    }
    g_enexFlags.clear();
    const Place& pl = kPlaces[g_arena.place];
    std::set<std::string> names;
    for (int i = 0; i < pool->size; ++i) {
        if (pool->byteMap[i] < 0) continue;
        uintptr_t e = (uintptr_t)pool->objects + (uintptr_t)i * 0x3C;
        g_enexFlags.push_back({i, Ref<uint16_t>(e + 0x30)});
        if (Ref<int8_t>(e + 0x32) != pl.area || (DoorSpot((void*)e) - pl.pos).Length2D() > 150.0f) continue;  // m_nArea
        char name[9] = {};
        std::memcpy(name, (const void*)e, 8);  // CEntryExit::m_szName
        if (name[0]) names.insert(name);
    }
    // SET_INTERIOR_ACCESS's own work, by hand: each door of those names, both sides, bEnableAccess off (+0x30, 0x4000;
    // the marker and the way through go). The opcode took the name as 8 bytes without an end, unsafe for 8 letters.
    int closed = 0;
    for (int i = 0; i < pool->size; ++i) {
        if (pool->byteMap[i] < 0) continue;
        uintptr_t e = (uintptr_t)pool->objects + (uintptr_t)i * 0x3C;
        char name[9] = {};
        std::memcpy(name, (const void*)e, 8);
        if (!name[0] || !names.count(name)) continue;
        Ref<uint16_t>(e + 0x30) &= (uint16_t)~0x4000;
        ++closed;
    }
    LOGI("Arena: %d doors of the place shut (%d entrances)", (int)names.size(), closed);
}

void LoadKnifeModel() { g_knifeModel = -1; }  // the knifemen are men of their own force

// into the place, a little apart from the others
void TakeIn() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || g_arena.taken) return;
    g_arena.backArea = me->AreaCode();
    g_arena.back = me->VehicleIfIn() ? me->VehicleIfIn()->GetPosition() + CVector{0, 0, 1.0f} : me->GetPosition();
    g_arena.backHeading = me->CurrentRotation() * 57.29578f;
    const Place& pl = kPlaces[g_arena.place];
    float a = (float)Net().MyId() * 1.25f;
    CVector at = pl.pos + CVector{std::cos(a) * 1.2f, std::sin(a) * 1.2f, 0.0f};
    if (!Net().Online()) at = pl.pos;
    Teleport(pl.area, at, Rand(0.0f, 360.0f));
    g_arena.taken = true;
    ShutDoors(true);
}

void TakeBack() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!g_arena.taken || !me) return;
    g_arena.taken = false;
    // dead and taken to hospital, or gone out on its own: it is somewhere else already
    if (me->Health() <= 0.0f || me->AreaCode() != kPlaces[g_arena.place].area) return;
    Teleport(g_arena.backArea, g_arena.back, g_arena.backHeading);
}

// ---------------------------------------------------------------- the place's floor
// Where people can stand and walk in the place, worked out once by the host as the arena begins (a few hundred cells a
// frame): from the middle, cell by cell (1.5 m apart, 2 m in a big place), a step to a neighbour is taken when there
// is floor there within a metre up or down of this one (a ray down), no wall between them at the knees and at the
// chest, and room over it for a body. What it reaches is the walkable place, its other rooms and floors too (stairs
// are steps): the enemies come from cells of it far from the players and spread round them, the weapons and armour
// lie on cells spread over it, the ones who got stuck are set one step on along it, and cover is looked for on it.
struct ColPoint {
    CVector point;
    uint8_t rest[0x2C - 12];
};
bool Ray(const CVector& a, const CVector& b, CVector& at) {  // the first wall or floor on the way (buildings, objects)
    ColPoint cp{};
    CEntity* hit = nullptr;
    bool struck = Call<0x56BA00, bool>(&a, &b, &cp, &hit, true, false, false, true, false, false, false, false);  // ProcessLineOfSight
    if (struck) at = cp.point;
    return struck;
}
bool Clear(const CVector& a, const CVector& b) {
    return Call<0x56A490, bool>(&a, &b, true, false, false, true, false, false, false);  // GetIsLineOfSightClear
}

struct Cell {
    CVector p;              // the floor
    std::vector<int> next;  // the cells a step away
    bool room = false;      // half a metre free all round at the waist: one can stand there (not against a wall)
};
std::vector<Cell> g_cells;
std::map<int64_t, int> g_cellAt;  // (column, row, storey) -> cell
std::vector<int> g_open;          // the cells whose neighbours are still to be tried
std::vector<int> g_roomy;         // the cells with room round them
float g_step = 1.5f;
int g_reachCells = 0;
bool g_gridDone = false;

int64_t CellKey(int i, int j, float z) { return ((int64_t)(i + 512) << 40) | ((int64_t)(j + 512) << 20) | (int64_t)((z + 2000.0f) / 2.2f); }

bool FloorBelow(float x, float y, float fromZ, float& z) {
    CVector at;
    if (!Ray({x, y, fromZ}, {x, y, fromZ - 3.5f}, at)) return false;
    z = at.z;
    return true;
}

bool Roomy(const CVector& floor) {
    CVector waist = floor + CVector{0, 0, 1.0f}, knee = floor + CVector{0, 0, 0.45f};
    const float r = 0.45f;
    for (const CVector& d : {CVector{r, 0, 0}, CVector{-r, 0, 0}, CVector{0, r, 0}, CVector{0, -r, 0}})
        if (!Clear(waist, waist + d) || !Clear(knee, knee + d)) return false;
    return true;
}

void StartGrid() {
    g_cells.clear();
    g_cellAt.clear();
    g_open.clear();
    g_roomy.clear();
    g_gridDone = false;
    const Place& pl = kPlaces[g_arena.place];
    g_step = pl.reach > 30.0f ? 2.0f : 1.5f;
    g_reachCells = (int)(pl.reach * 1.2f / g_step) + 1;
    float z = 0;
    if (!FloorBelow(pl.pos.x, pl.pos.y, pl.pos.z + 1.0f, z)) z = pl.pos.z - 1.0f;
    CVector start{pl.pos.x, pl.pos.y, z};
    g_cells.push_back({start, {}, Roomy(start)});
    g_cellAt[CellKey(0, 0, z)] = 0;
    g_open.push_back(0);
}

// A step to a neighbour square: its middle first, and when that does not go (a doorway narrower than the grid, a
// pillar on the way) four spots off the middle of the square; the cell keeps the spot that went.
void GridStep() {  // a part of it each frame
    const Place& pl = kPlaces[g_arena.place];
    const float o = g_step * 0.3f;
    const float offs[5][2] = {{0, 0}, {o, 0}, {-o, 0}, {0, o}, {0, -o}};
    for (int budget = 25; budget > 0 && !g_open.empty() && g_cells.size() < 3000; --budget) {
        int ci = g_open.back();
        g_open.pop_back();
        CVector c = g_cells[(size_t)ci].p;
        int i0 = (int)std::lround((c.x - pl.pos.x) / g_step), j0 = (int)std::lround((c.y - pl.pos.y) / g_step);
        for (int di = -1; di <= 1; ++di)
            for (int dj = -1; dj <= 1; ++dj) {
                if (!di && !dj) continue;
                int i = i0 + di, j = j0 + dj;
                if (std::abs(i) > g_reachCells || std::abs(j) > g_reachCells) continue;
                for (const auto& of : offs) {
                    float x = pl.pos.x + (float)i * g_step + of[0], y = pl.pos.y + (float)j * g_step + of[1], z = 0;
                    if (!FloorBelow(x, y, c.z + 1.3f, z) || std::fabs(z - c.z) > 1.0f) continue;
                    auto known = g_cellAt.find(CellKey(i, j, z));
                    if (known != g_cellAt.end()) {  // a way to a cell found before
                        auto& a = g_cells[(size_t)ci].next;
                        CVector kp = g_cells[(size_t)known->second].p;
                        if (known->second != ci && std::find(a.begin(), a.end(), known->second) == a.end() &&
                            Clear(c + CVector{0, 0, 0.9f}, kp + CVector{0, 0, 0.9f}))
                            a.push_back(known->second), g_cells[(size_t)known->second].next.push_back(ci);
                        break;
                    }
                    CVector n{x, y, z};
                    if (!Clear(c + CVector{0, 0, 0.9f}, n + CVector{0, 0, 0.9f}) || !Clear(c + CVector{0, 0, 1.5f}, n + CVector{0, 0, 1.5f}) ||
                        !Clear(n + CVector{0, 0, 0.2f}, n + CVector{0, 0, 1.85f}))
                        continue;
                    int ni = (int)g_cells.size();
                    g_cells.push_back({n, {ci}, Roomy(n)});
                    g_cells[(size_t)ci].next.push_back(ni);
                    g_cellAt[CellKey(i, j, z)] = ni;
                    g_open.push_back(ni);
                    break;
                }
            }
    }
    if (g_open.empty() || g_cells.size() >= 3000) {
        g_gridDone = true;
        for (size_t k = 0; k < g_cells.size(); ++k)
            if (g_cells[k].room) g_roomy.push_back((int)k);
        LOGI("Arena: %d cells of floor found in %s (%d with room round them)", (int)g_cells.size(), kPlaces[g_arena.place].name[1],
             (int)g_roomy.size());
    }
}
bool GridUsable() { return g_gridDone && g_cells.size() >= 15; }
// the cells one may be put on: those with room round them (nobody comes out of a wall), all of them when too few
const std::vector<int>& StandCells() {
    static std::vector<int> all;
    if (g_roomy.size() >= 15) return g_roomy;
    if (all.size() != g_cells.size()) {
        all.resize(g_cells.size());
        for (size_t k = 0; k < all.size(); ++k) all[k] = (int)k;
    }
    return all;
}

int CellNear(const CVector& p, float within = 3.0f) {
    int best = -1;
    float bestD = within;
    for (size_t k = 0; k < g_cells.size(); ++k) {
        CVector d = g_cells[k].p - p;
        if (std::fabs(d.z + 1.0f) > 2.5f) continue;  // p is a body (a metre over its feet)
        float l = d.Length2D();
        if (l < bestD) bestD = l, best = (int)k;
    }
    return best;
}

// steps from every cell to cell `to` (BFS), -1 where it does not lead
std::vector<int> StepsTo(int to) {
    std::vector<int> dist(g_cells.size(), -1);
    if (to < 0) return dist;
    std::vector<int> q{to};
    dist[(size_t)to] = 0;
    for (size_t h = 0; h < q.size(); ++h)
        for (int n : g_cells[(size_t)q[h]].next)
            if (dist[(size_t)n] < 0) dist[(size_t)n] = dist[(size_t)q[h]] + 1, q.push_back(n);
    return dist;
}

std::vector<CVector> PlayerSpots() {  // the live players in the place
    std::vector<CVector> at;
    int area = kPlaces[g_arena.place].area;
    if (CPlayerPed* me = FindPlayerPed(0); me && !g_spectating && me->Health() > 0.0f && me->AreaCode() == area) at.push_back(me->GetPosition());
    for (auto& [id, rp] : Players::Instance().All())
        if (rp.hasState && !Spectator(id) && !(rp.state.flags & OnFootState::F_DEAD) && rp.state.area == area) at.push_back(rp.state.pos);
    if (at.empty()) at.push_back(kPlaces[g_arena.place].pos);
    return at;
}
float DistToPlayers(const CVector& p, const std::vector<CVector>& players) {
    float d = 1e9f;
    for (const CVector& q : players) d = std::min(d, (q - p).Length());
    return d;
}
float AngleDiff(float a, float b) {
    float d = std::fmod(std::fabs(a - b), 6.2831853f);
    return d > 3.14159265f ? 6.2831853f - d : d;
}

// ---------------------------------------------------------------- the enemies
constexpr int WEAPON_KNIFE = 4, WEAPON_GRENADE = 16, WEAPON_MOLOTOV = 18, WEAPON_PISTOL = 22,
              WEAPON_SAWNOFF = 26, WEAPON_SNIPER = 34, WEAPON_FLAMETHROWER = 37;
constexpr int kEmptyDecisionMaker = 65542;  // SET_CHAR_DECISION_MAKER "Empty": reacts to nothing by itself
// R_FLAMER: the Bikers' and the Vagos' flamethrower man (huge health, slow, molotovs); R_MEDIC: heals the others near it
// and gets the fallen up; R_TWIN: one of the Twins, the mini-boss (two sawn-offs / two desert eagles)
// R_SAW: the chainsaw mini-boss (winds up, then charges straight on till a wall); R_OFFICER: the mini-boss with a
// katana who keeps to the back, calls 4 men every 30 s, falls back and heals
enum Role : uint8_t { R_GUN, R_GRENADIER, R_KNIFE, R_BOSS, R_SNIPER, R_FLAMER, R_MEDIC, R_TWIN, R_SAW, R_OFFICER };
// what one does now: fights as the game has it, goes round to a back, runs to cover, holds it (down, up and shooting,
// down), pushes on at the players, (a knifeman) steals up to a back out of the player's sight, (a sniper) moves to
// another spot it sees the players from
// M_BREACH: it came at a barricade and tears its boards off before it gets through (the zombies' windows)
enum Mode : uint8_t { M_FIGHT, M_FLANK, M_COVER_GO, M_COVER_HOLD, M_PUSH, M_SNEAK, M_RELOCATE, M_BREACH };
struct Enemy {
    int squad = 0;
    Role role = R_GUN;
    int weapon = 0;  // its gun
    Mode mode = M_FIGHT;
    uint64_t modeUntil = 0, nextCoverMs = 0;
    bool popped = false;  // up and shooting in its cover
    uint64_t popUntil = 0;
    CVector cover;
    uint64_t grenadeUntil = 0;  // throwing till then
    uint64_t throwAt = 0;       // the grenade leaves the hand then
    uint64_t swingAt = 0;       // the wind-up over: the throw's swing then
    float throwFlight = 1.0f;
    int grenades = 0;
    CPed* throwTarget = nullptr;
    float spawnHealth = 0;
    CPed* after = nullptr;  // the player it was set on
    uint64_t setOnMs = 0;   // when it was set on it
    CVector lastPos;        // where it was when it last moved on
    uint64_t lastMoveMs = 0;
    // going by the floor's cells (the player out of its sight): where to, since when
    bool routing = false;
    CVector routeTo;
    uint64_t routeMs = 0;
    uint64_t evalMs = 0, waitSince = 0, blindSince = 0;  // the knifeman's and the sniper's own reckoning
    int model = 0, accuracy = 50, armour = 0;
    float maxHealth = 100;  // what a medic heals it up to
    int throwWeapon = WEAPON_GRENADE;
    uint64_t nextThrowMs = 0;
    int barricade = -1;  // M_BREACH: the barricade it tears down
    uint64_t breachSince = 0;
    bool tearing = false;
    // a medic's
    int revives = 0;
    uint64_t cprUntil = 0, healMs = 0;
    CPed* cprTarget = nullptr;
    CPed* revivedBy = nullptr;  // a dead one: the medic at it now
    bool counted = false;       // a dead one: its death seen (the power-ups)
    bool silent = false;
    int vest = -1;  // the flamethrower man's body armour (an object on it)
    bool healGas = false;  // a medic's one healing gas grenade, thrown
    uint64_t gasAt = 0;    // it leaves the hand then
    CVector gasAim;
    float gasFlight = 1.0f;
    // the saw's charge: 0 walking at the player, 1 winding up, 2 charging, 3 stunned against a wall
    int sawStage = 0;
    uint64_t sawUntil = 0, nextChargeMs = 0;
    CVector sawDir;
    int bounces = 0;
    std::set<CPed*> sawHit;  // the players this charge put down already
    // the officer's
    uint64_t nextCallMs = 0, lastHurtMs = 0;
    float lastHealth = 0;
    bool retreating = false;
    CVector home;
    // T-Bone's grenade launcher under his AK: 0 fighting, 1 running back to reload it, 2 reloading (4 s)
    bool uglLoaded = true;
    int uglStage = 0;
    uint64_t uglUntil = 0, nextUglMs = 0;
};
struct Squad {
    float angle = 0;  // where round the player it goes (degrees, 180 = behind)
    bool flank = false;
    uint64_t nextGrenadeMs = 0, nextPushMs = 0;
};
std::map<CPed*, Enemy> g_info;
std::vector<Squad> g_squads;
std::vector<CVector> g_lastSpawns, g_spawnsNow;  // where the last wave's and this wave's came from (the next ones elsewhere)

bool OnFloor(CVector& p, float baseZ) {  // the fallback without the floor's cells
    float z = -100.0f;
    Cmd(0x02CE).F(p.x).F(p.y).F(baseZ + 2.0f).Out(&z).Run();  // GET_GROUND_Z_FOR_3D_COORD
    if (z < baseZ - 4.0f || z > baseZ + 3.0f) return false;
    p.z = z + 1.0f;
    return true;
}

// any of the players sees a body at `at` (from their heads)
bool PlayersSee(const CVector& at, const std::vector<CVector>& players) {
    for (const CVector& q : players)
        if (Clear(q + CVector{0, 0, 0.6f}, at + CVector{0, 0, 0.2f})) return true;
    return false;
}

// A spot to come from (a body's, a metre over the floor): far from the players, as far as may be from the other
// spots of this wave (the squads spread over the whole place, not in one heap), not where the last wave came from,
// towards `angle` round the players (radians) if it can be, and out of the players' sight rather than in it. The
// distance kept from the players is what the place allows (a narrow hall has its far ends only). `minApart` from this
// wave's others; off the cells (too few): towards the angle from the middle.
CVector SpawnSpot(float angle, float halfSector, float minApart) {
    const Place& pl = kPlaces[g_arena.place];
    std::vector<CVector> players = PlayerSpots();
    CVector mid{};
    for (const CVector& q : players) mid = mid + q;
    mid = mid * (1.0f / (float)players.size());
    if (GridUsable()) {
        const std::vector<int>& cells = StandCells();
        float farthest = 0;
        for (int k : cells) farthest = std::max(farthest, DistToPlayers(g_cells[(size_t)k].p, players));
        float safe = std::min(pl.big ? 18.0f : 9.0f, farthest * 0.6f);
        for (int pass = 0; pass < 3; ++pass, safe *= 0.6f, minApart *= 0.5f) {
            struct Cand {
                int k;
                float score;
            };
            std::vector<Cand> cands;
            for (int k : cells) {
                const CVector& c = g_cells[(size_t)k].p;
                float dp = DistToPlayers(c, players);
                if (dp < safe) continue;
                float apart = 1e9f;
                for (const CVector& sp : g_spawnsNow) apart = std::min(apart, (sp - c - CVector{0, 0, 1.0f}).Length());
                if (apart < minApart) continue;
                float off = AngleDiff(std::atan2(c.y - mid.y, c.x - mid.x), angle);
                float score = std::min(dp, 30.0f) * 0.5f + std::min(apart, 30.0f) - std::max(0.0f, off - halfSector) * 6.0f + Rand(0.0f, 4.0f);
                for (const CVector& sp : g_lastSpawns) score -= std::max(0.0f, 10.0f - (sp - c).Length());  // not where they came from
                cands.push_back({k, score});
            }
            if (cands.empty()) continue;
            std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
            int best = cands[0].k;
            float bestScore = -1e9f;
            for (size_t n = 0; n < cands.size() && n < 12; ++n) {  // the best few: one out of sight is better
                CVector body = g_cells[(size_t)cands[n].k].p + CVector{0, 0, 1.0f};
                float score = cands[n].score + (PlayersSee(body, players) ? 0.0f : 8.0f);
                if (score > bestScore) bestScore = score, best = cands[n].k;
            }
            CVector p = g_cells[(size_t)best].p + CVector{0, 0, 1.0f};
            g_spawnsNow.push_back(p);
            return p;
        }
    }
    CVector best = pl.pos;
    float bestScore = -1.0f;
    for (int t = 0; t < 24; ++t) {
        float a = angle + Rand(-0.35f, 0.35f), d = Rand(pl.reach * 0.45f, pl.reach);
        CVector p = pl.pos + CVector{std::cos(a) * d, std::sin(a) * d, 0.0f};
        if (!OnFloor(p, pl.pos.z)) continue;
        float dp = DistToPlayers(p, players), score = std::min(dp, 25.0f) + d * 0.3f;
        if (dp > 6.0f && score > bestScore) bestScore = score, best = p;
    }
    g_spawnsNow.push_back(best);
    return best;
}
// a member's place by its squad's (a cell with room round it a few metres off it, a way from it)
CVector MemberSpot(const CVector& anchor, int k) {
    if (GridUsable()) {
        std::vector<int> close;
        for (int i : StandCells()) {
            CVector d = g_cells[(size_t)i].p + CVector{0, 0, 1.0f} - anchor;
            if (d.Length2D() > 0.9f && d.Length2D() < 4.0f && std::fabs(d.z) < 1.5f && Clear(anchor, g_cells[(size_t)i].p + CVector{0, 0, 1.0f}))
                close.push_back(i);
        }
        if (!close.empty()) return g_cells[(size_t)close[(size_t)RandInt(0, (int)close.size() - 1)]].p + CVector{0, 0, 1.0f};
    }
    for (int t = 0; t < 8; ++t) {
        float a = Rand(0.0f, 6.2831853f), d = Rand(1.2f, 2.6f);
        CVector p = anchor + CVector{std::cos(a) * d, std::sin(a) * d, 0.0f};
        if (OnFloor(p, anchor.z - 1.0f) && Clear(anchor, p)) return p;
    }
    return anchor + CVector{0.6f * (float)(k % 2 ? 1 : -1), 0.6f * (float)(k / 2), 0.0f};
}

// the nearest live player ped (this game's own or a copy) in the place
CPed* NearestPlayer(const CVector& at) {
    CPed* best = nullptr;
    float bestD = 1e9f;
    int area = kPlaces[g_arena.place].area;
    auto consider = [&](CPed* ped) {
        if (!ped || ped->Health() <= 0.0f || ped->AreaCode() != area) return;
        float d = (ped->GetPosition() - at).Length();
        if (d < bestD) bestD = d, best = ped;
    };
    if (!g_spectating) consider(FindPlayerPed(0));
    for (auto& [id, rp] : Players::Instance().All())
        if (rp.ped && rp.hasState && !Spectator(id) && !(rp.state.flags & OnFootState::F_DEAD)) consider(rp.ped);
    return best;
}

int GunFor(int weapon) {  // the weapon if its model is in memory now, else a pistol (a gun without its model crashes)
    int wm = -1;
    Cmd(0x0781).I(weapon).Out(&wm).Run();  // GET_WEAPONTYPE_MODEL
    if (wm <= 0 || IsModelLoaded(wm) || LoadModelNow(wm)) return weapon;
    return IsModelLoaded(346) || LoadModelNow(346) ? WEAPON_PISTOL : 0;
}

void Give(int h, int weapon, int ammo) {
    weapon = GunFor(weapon);
    if (weapon) Cmd(0x01B2).I(h).I(weapon).I(ammo).Run();  // GIVE_WEAPON_TO_CHAR
}

void SetOn(CPed* p, Enemy& e, CPed* target) {
    e.after = target;
    e.routing = false;
    e.setOnMs = NowMs();
    if (!target) return;
    int h = GetPedRef(p), t = GetPedRef(target);
    const Squad& sq = g_squads[(size_t)e.squad];
    if (e.mode == M_FLANK) {
        // round to the player's back (the offset turns with the player), then the fight
        Cmd(0x06A8).I(h).I(t).I(14000).F(7.0f).F(sq.angle).Run();  // TASK_GOTO_CHAR_OFFSET
    } else {
        Cmd(0x05E2).I(h).I(t).Run();  // TASK_KILL_CHAR_ON_FOOT
    }
}

// on to a spot (the floor's) running (6) or sprinting (7); not given again while it is on its way to the same one
void GoTo(CPed* p, Enemy& e, const CVector& to, int move) {
    uint64_t now = NowMs();
    if (e.routing && (e.routeTo - to).Length() < 0.6f && now - e.routeMs < 2500) return;
    e.routing = true;
    e.routeTo = to;
    e.routeMs = now;
    Cmd(0x05D3).I(GetPedRef(p)).F(to.x).F(to.y).F(to.z).I(move).I(5000).Run();  // TASK_GO_STRAIGHT_TO_COORD
}

int MakeEnemy(int model, int weapon, int health, int armour, int accuracy, const CVector& at, int squad, Role role, bool silent) {
    if (!HasRoom(PedPool(), kKeepPeds + 2, "ped") || !LoadModelNow(model)) return -1;
    int h = -1;
    Cmd(0x009A).I(PED_TYPE_MISSION1).I(model).F(at.x).F(at.y).F(at.z - 1.0f).Out(&h).Run();  // CREATE_CHAR (on the floor)
    if (h < 0) return -1;
    if (CPed* made = GetPed(h)) Peds::Instance().Share(made);  // the others see it (no mission made it)
    Cmd(0x0860).I(h).I(kPlaces[g_arena.place].area).Run();  // SET_CHAR_AREA_VISIBLE
    Cmd(0x087E).I(h).I(0).Run();                             // SET_CHAR_DROPS_WEAPONS_WHEN_DEAD: none
    Cmd(0x0223).I(h).I(health).Run();                        // SET_CHAR_HEALTH
    Cmd(0x02E2).I(h).I(accuracy).Run();                      // SET_CHAR_ACCURACY
    Cmd(0x060F).I(h).F(150.0f).Run();                        // SET_SENSE_RANGE: it knows where the players are
    Cmd(0x060B).I(h).I(kEmptyDecisionMaker).Run();           // SET_CHAR_DECISION_MAKER: no fear (no running off, no cowering)
    if (silent) Cmd(0x094E).I(h).I(1).Run();                 // DISABLE_CHAR_SPEECH
    if (role == R_BOSS || role == R_FLAMER || role == R_TWIN || role == R_SAW || role == R_OFFICER) Cmd(0x0446).I(h).I(0).Run();  // SET_CHAR_SUFFERS_CRITICAL_HITS: no one-shot headshot
    if (role == R_KNIFE) {
        Give(h, WEAPON_KNIFE, 1);
        weapon = WEAPON_KNIFE;
        Cmd(0x0245).I(h).S8("player").Run();  // SET_ANIM_GROUP_FOR_CHAR: it runs as CJ does
    } else if (role == R_FLAMER) {
        Give(h, WEAPON_MOLOTOV, 5);  // the bottles it throws (in hand only for the throw)
        Give(h, WEAPON_FLAMETHROWER, 5000);
        weapon = GunFor(WEAPON_FLAMETHROWER);
        Cmd(0x0245).I(h).S8("fatman").Run();                       // SET_ANIM_GROUP_FOR_CHAR: heavy and slow
        Cmd(0x02AB).I(h).I(0).I(1).I(0).I(0).I(0).Run();            // SET_CHAR_PROOFS: its own fire does not burn it
        Cmd(0x0946).I(h).I(1).Run();  // its actions not cut by hits: no stagger, no knocking back
    } else {
        Give(h, weapon, role == R_SNIPER ? 80 : 500);
        weapon = GunFor(weapon);
    }
    if (role == R_TWIN) Cmd(0x081A).I(h).I(2).Run();  // SET_CHAR_WEAPON_SKILL pro: a gun in each hand
    if (role == R_SAW || role == R_OFFICER) {
        Cmd(0x0946).I(h).I(1).Run();  // no stagger
        if (role == R_SAW) Cmd(0x0245).I(h).S8("player").Run();
    }
    if (role == R_SNIPER) Cmd(0x0350).I(h).I(1).Run();  // SET_CHAR_STAY_IN_SAME_PLACE: it shoots from where it is
    if (weapon) Cmd(0x01B9).I(h).I(weapon).Run();  // SET_CURRENT_CHAR_WEAPON
    CPed* ped = GetPed(h);
    if (!ped) return h;
    ped->Armour() = (float)armour;
    Enemy e;
    e.squad = squad;
    e.role = role;
    e.weapon = weapon;
    e.spawnHealth = (float)health;
    e.maxHealth = (float)health;
    e.model = model;
    e.accuracy = accuracy;
    e.armour = armour;
    e.silent = silent;
    e.grenades = role == R_GRENADIER ? 3 : role == R_FLAMER ? 5 : 0;  // 3 grenades a man, the flamethrower man's 5 molotovs
    e.throwWeapon = role == R_FLAMER ? WEAPON_MOLOTOV : WEAPON_GRENADE;
    e.nextThrowMs = NowMs() + (uint64_t)RandInt(5000, 8000);
    e.mode = role == R_KNIFE ? M_SNEAK : role == R_TWIN ? M_PUSH : g_squads[(size_t)squad].flank ? M_FLANK : M_FIGHT;
    e.modeUntil = NowMs() + (role == R_TWIN ? 20000 : 14000);
    e.nextCoverMs = NowMs() + (uint64_t)RandInt(3000, 8000);
    e.lastPos = at;
    e.lastMoveMs = NowMs();
    g_info[ped] = e;
    g_enemies.insert(ped);
    if (role == R_KNIFE) knife::MarkKnifeman(ped);
    if (role != R_KNIFE) SetOn(ped, g_info[ped], NearestPlayer(at));
    else g_info[ped].after = NearestPlayer(at);
    return h;
}

// the SWAT's armour is half the others' (they do not die of a headshot, Difficulty)
int ArmourFor(int wave, int extra) {
    int a = std::min(250, 25 * wave + extra);
    return kFactions[g_arena.faction].models[0] == 285 ? a / 2 : a;
}

// the barricade a squad comes through (host): one whose spawn is far from the players, not the last wave's if it can be;
// -1: none (no barricades in this place)
int PickBarricade(int wave) {
    if (g_bars.empty()) return -1;
    std::vector<CVector> players = PlayerSpots();
    int best = -1;
    float bestScore = -1e9f;
    for (size_t i = 0; i < g_bars.size(); ++i) {
        const Bar& b = g_bars[i];
        if (b.built) continue;
        float score = std::min(DistToPlayers(b.spawn, players), 30.0f) + Rand(0.0f, 8.0f);
        if (b.usedWave == wave) score -= 25.0f;  // one squad a barricade a wave, when there are enough
        if (b.usedWave == wave - 1) score -= 6.0f;
        if (score > bestScore) bestScore = score, best = (int)i;
    }
    return best;
}

// a squad of 3-4 towards angle `dir` (radians round the players), going round to `aroundDeg` (180: the back) or not;
// most squads come through a barricade (the zombies' windows): from behind it, tearing its boards off first
int MakeSquad(float dir, float halfSector, bool flank, float aroundDeg, int size, int wave, bool minions) {
    const Faction& f = kFactions[g_arena.faction];
    float progress = Progress(wave);
    int gunTier = std::clamp((int)std::lround(progress * 3.0f) + (minions ? 1 : 0), 0, 3);
    Squad sq;
    sq.angle = aroundDeg;
    sq.flank = flank;
    sq.nextGrenadeMs = NowMs() + (uint64_t)RandInt(4000, 7000);
    sq.nextPushMs = NowMs() + (uint64_t)RandInt(18000, 26000);
    g_squads.push_back(sq);
    int squad = (int)g_squads.size() - 1;
    int bar = minions || RandInt(0, 99) >= 75 ? -1 : PickBarricade(wave);
    CVector anchor = bar >= 0 ? g_bars[(size_t)bar].spawn + CVector{0, 0, 1.0f} : SpawnSpot(dir, halfSector, 8.0f);
    if (bar >= 0) g_bars[(size_t)bar].usedWave = wave, g_spawnsNow.push_back(anchor);
    bool scatter = kPlaces[g_arena.place].big && !minions && bar < 0;  // a big place: its members come from all over their side
    int made = 0;
    for (int k = 0; k < size; ++k) {
        Role role = k == 1 ? R_GRENADIER : R_GUN;
        CVector at = k == 0 ? anchor : scatter ? SpawnSpot(dir, halfSector * 1.3f, 6.0f) : MemberSpot(anchor, k);
        int weapon = f.weapons[std::clamp(gunTier - (k == 2 ? 1 : 0), 0, 3)][RandInt(0, 1)];
        int model = f.models[(size_t)RandInt(0, 3)];
        int health = 100 + 15 * wave, armour = ArmourFor(wave, minions ? 50 : 0);
        int accuracy = std::clamp(40 + 6 * wave + (minions ? 10 : 0), 40, 90);
        int h = MakeEnemy(model, weapon, health, armour, accuracy, at, squad, role, minions);
        made += h >= 0;
        CPed* p = h >= 0 ? GetPed(h) : nullptr;
        if (p && bar >= 0 && g_bars[(size_t)bar].boards > 0) {
            Enemy& e = g_info[p];
            e.mode = M_BREACH;
            e.barricade = bar;
            e.breachSince = NowMs();
            e.routing = false;
        }
    }
    return made;
}

// A knifeman comes alone, from a spot far from the squads (and from the players), silent, round to a player's back
void MakeKnifeman(int wave) {
    const Faction& f = kFactions[g_arena.faction];
    Squad sq;
    sq.angle = 180.0f;
    sq.flank = true;
    g_squads.push_back(sq);
    std::vector<CVector> players = PlayerSpots();
    CVector at;
    bool found = false;
    // the far side of the place from the squads (from the middle of where they came from)
    CVector squadsMid{};
    for (const CVector& sp : g_spawnsNow) squadsMid = squadsMid + sp;
    if (!g_spawnsNow.empty()) squadsMid = squadsMid * (1.0f / (float)g_spawnsNow.size());
    if (GridUsable()) {
        float best = -1;
        for (int k : StandCells()) {
            CVector p = g_cells[(size_t)k].p + CVector{0, 0, 1.0f};
            float fromSquads = 1e9f;
            for (const CVector& s : g_spawnsNow) fromSquads = std::min(fromSquads, (s - p).Length());
            float dp = DistToPlayers(p, players);
            if (fromSquads < 10.0f || dp < 10.0f) continue;
            float away = g_spawnsNow.empty() ? fromSquads : (p - squadsMid).Length();
            float score = away * 2.0f + std::min(dp, 20.0f) * 0.3f + Rand(0.0f, 4.0f);
            if (score + 6.0f <= best) continue;
            if (!PlayersSee(p, players)) score += 6.0f;  // out of their sight is better
            if (score > best) best = score, at = p, found = true;
        }
    }
    if (!found) at = SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 10.0f);
    else g_spawnsNow.push_back(at);
    int model = g_knifeModel >= 0 && IsModelLoaded(g_knifeModel) ? g_knifeModel : f.models[(size_t)RandInt(0, 3)];
    MakeEnemy(model, WEAPON_KNIFE, 100 + 15 * wave, ArmourFor(wave, 0), 60, at, (int)g_squads.size() - 1, R_KNIFE, true);
}

// Snipers (the last two waves): each alone on a spot far off that sees a player, staying there and shooting from
// there; one that loses sight of the players for 5 s moves to another such spot
bool SniperSpot(const CVector& from, CPed* target, float maxMove, CVector& out) {
    std::vector<CVector> players;
    if (target) players.push_back(target->GetPosition());
    else players = PlayerSpots();
    const Place& pl = kPlaces[g_arena.place];
    float want = pl.big ? 20.0f : 10.0f, best = -1e9f;
    for (int k : StandCells()) {
        CVector body = g_cells[(size_t)k].p + CVector{0, 0, 1.0f};
        if (maxMove > 0 && (body - from).Length() > maxMove) continue;
        float dp = DistToPlayers(body, players);
        if (dp < want * 0.5f) continue;
        bool apart = true;
        for (auto& [q, e] : g_info)
            if (e.role == R_SNIPER && IsPedValid(q) && (q->GetPosition() - body).Length() < 8.0f && (q->GetPosition() - from).Length() > 1.0f)
                apart = false;
        if (!apart) continue;
        float score = std::min(dp, 45.0f) + Rand(0.0f, 5.0f) - (dp < want ? 15.0f : 0.0f);
        if (score <= best) continue;
        CVector eye = body + CVector{0, 0, 0.6f};
        bool sees = false;
        for (const CVector& q : players) sees |= Clear(eye, q + CVector{0, 0, 0.4f});
        if (!sees) continue;
        best = score, out = body;
    }
    return best > -1e8f;
}
void MakeSniper(int wave) {
    const Faction& f = kFactions[g_arena.faction];
    g_squads.push_back(Squad{});
    CVector at;
    if (!GridUsable() || !SniperSpot({}, nullptr, 0.0f, at)) at = SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 8.0f);
    else g_spawnsNow.push_back(at);
    MakeEnemy(f.models[(size_t)RandInt(0, 3)], WEAPON_SNIPER, 100 + 15 * wave, ArmourFor(wave, 0) / 2, 80, at,
              (int)g_squads.size() - 1, R_SNIPER, false);
}

void DropVest(Enemy& e) {
    if (e.vest >= 0 && GetObject(e.vest)) Cmd(0x0108).I(e.vest).Run();  // DELETE_OBJECT
    e.vest = -1;
}
void ReleaseDead() {  // the last wave's dead: the game's own again (they filled the ped pool)
    for (auto& [q, qe] : g_info) DropVest(qe);
    for (CPed* p : g_enemies)
        if (IsPedValid(p)) Cmd(p->Health() > 0.0f ? 0x009B : 0x01C2).I(GetPedRef(p)).Run();  // DELETE_CHAR / no longer needed
    g_enemies.clear();
    g_info.clear();
    g_squads.clear();
}

void LoadBoss(int b) {
    if (b < 0 || b >= kBossCount) return;
    Cmd(0x023C).I(kBossSlot).S8(kBosses[b].special).Run();  // LOAD_SPECIAL_CHARACTER
    Cmd(0x038B).Run();                                      // LOAD_ALL_MODELS_NOW
}
void UnloadBoss() { Cmd(0x0296).I(kBossSlot).Run(); }  // UNLOAD_SPECIAL_CHARACTER

int PickBoss(int faction) {
    std::vector<int> fit;
    for (int b : kFactions[faction].boss)
        if (b >= 0) fit.push_back(b);
    if (!fit.empty() && RandInt(0, 3) != 0) return fit[(size_t)RandInt(0, (int)fit.size() - 1)];
    return RandInt(0, kBossCount - 1);
}

int PlayersIn() {
    int n = 1;
    if (Net().Online())
        for (auto& [id, rp] : Players::Instance().All()) n += rp.ped != nullptr;
    return n;
}

// ---------------------------------------------------------------- things to pick up
// Before each wave (the first one too, once the floor is known) the host lays weapons and body armour about the place
// (A_ITEMS: every game makes the same pickups, and one taken is gone for all, Pickups): better ones as the waves go
// on, more in a big place, the sniper rifle and the rocket launcher only in a big one; where drinks machines stand
// (health from them) less armour. They lie spread over the whole place, each away from the others and from where the
// last wave's lay, so every wave they are somewhere else.
struct Loot {
    int weapon, model, ammo;
};
constexpr int kLootN = 9;
const Loot kLoot[3][kLootN] = {
    // the first waves: pistols, the silenced one, the eagle, shotguns, the uzi and the tec-9, a bat, molotovs
    {{22, 346, 34}, {23, 347, 34}, {24, 348, 21}, {22, 346, 51}, {23, 347, 51}, {24, 348, 28}, {22, 346, 34}, {23, 347, 34}, {24, 348, 21}},
    // the middle ones: the mp5, the ak, the combat shotgun, grenades, the country rifle, a katana
    {{22, 346, 51}, {23, 347, 51}, {24, 348, 28}, {22, 346, 68}, {23, 347, 68}, {24, 348, 35}, {22, 346, 51}, {23, 347, 51}, {24, 348, 28}},
    // the last ones: the m4, the sniper rifle, rockets, the flamethrower, a chainsaw
    {{22, 346, 68}, {23, 347, 68}, {24, 348, 35}, {22, 346, 85}, {23, 347, 85}, {24, 348, 42}, {22, 346, 68}, {23, 347, 68}, {24, 348, 35}},
};
constexpr int MODEL_ARMOUR = 1242;
struct Item {
    uint16_t model = 0, ammo = 0;
    CVector pos;
};
std::vector<int> g_pickups;          // this game's pickups of the arena (handles)
std::vector<CVector> g_lastItemsAt;  // host: where the last ones lay

// drinks machines in the place (the game's objects near its middle)
int Machines() {
    static const int kModels[] = {955, 956, 1209, 1302, 1775, 1776, 1977};
    CPoolHeader* pool = ObjectPool();
    if (!pool) return 0;
    const Place& pl = kPlaces[g_arena.place];
    int n = 0;
    for (int i = 0; i < pool->size; ++i) {
        if (pool->byteMap[i] < 0) continue;
        auto* o = (CEntity*)(pool->objects + (size_t)i * kObjectPoolElemSize);
        CVector d = o->GetPosition() - pl.pos;
        if (std::fabs(d.z) > 12.0f || d.Length2D() > pl.reach + 15.0f) continue;
        for (int m : kModels) n += o->ModelIndex() == m;
    }
    return n;
}

void RemoveItems() {
    for (int h : g_pickups) Cmd(0x0215).I(h).Run();  // REMOVE_PICKUP
    g_pickups.clear();
}

void MakeItems(const std::vector<Item>& items) {
    RemoveItems();
    game::challenge::SetMakingPickups(true);  // every game makes these: not announced as this one's
    for (const Item& it : items) {
        if (!LoadModelNow(it.model)) continue;
        int h = -1;
        if (it.model == MODEL_ARMOUR) Cmd(0x0213).I(it.model).I(3).F(it.pos.x).F(it.pos.y).F(it.pos.z).Out(&h).Run();  // CREATE_PICKUP, once
        else Cmd(0x032B).I(it.model).I(3).I(it.ammo).F(it.pos.x).F(it.pos.y).F(it.pos.z).Out(&h).Run();  // CREATE_PICKUP_WITH_AMMO
        if (h >= 0) g_pickups.push_back(h);
    }
    game::challenge::SetMakingPickups(false);
}

void PlaceItems(int forWave) {
    const Place& pl = kPlaces[g_arena.place];
    float progress = Progress(forWave);
    int tier = std::clamp((int)(progress * 2.99f), 0, 2);
    bool soda = pl.soda || Machines() > 0;
    int guns = 1 + forWave / 3 + (pl.big ? 1 : 0);  // free ones: pistols only, a few
    int armour = soda ? (forWave % 2 == 0 ? 1 : 0) + (pl.big ? 1 : 0) : 1 + forWave / 3 + (pl.big ? 1 : 0);
    guns = std::min(guns, 3), armour = std::min(armour, 4);
    std::vector<Item> items;
    std::vector<CVector> players = PlayerSpots();
    // the next spot: of the place's cells the farthest from those taken and from the last wave's (a little luck too)
    auto spot = [&](CVector& out) {
        if (GridUsable()) {
            float best = -1;
            for (const Cell& c : g_cells) {
                if (DistToPlayers(c.p, players) < 3.0f) continue;
                float apart = 1e9f;
                for (const Item& it : items) apart = std::min(apart, (it.pos - c.p).Length());
                for (const CVector& q : g_lastItemsAt) apart = std::min(apart, (q - c.p).Length() + 2.0f);
                if (apart > 1e8f) apart = 30.0f;
                float score = std::min(apart, 40.0f) + Rand(0.0f, 6.0f);
                if (score > best) best = score, out = c.p + CVector{0, 0, 0.7f};
            }
            return best >= 0;
        }
        for (int t = 0; t < 20; ++t) {
            float a = Rand(0.0f, 6.2831853f), d = Rand(2.0f, pl.reach * 0.9f);
            CVector p = pl.pos + CVector{std::cos(a) * d, std::sin(a) * d, 0.0f};
            if (!OnFloor(p, pl.pos.z)) continue;
            bool apart = true;
            for (const Item& it : items) apart &= (it.pos - p).Length() > 3.5f;
            if (!apart) continue;
            out = p - CVector{0, 0, 0.3f};
            return true;
        }
        return false;
    };
    for (int k = 0; k < guns; ++k) {
        const Loot* l = nullptr;
        for (int t = 0; t < 6 && !l; ++t) {
            const Loot& c = kLoot[tier][(size_t)RandInt(0, kLootN - 1)];
            if (!pl.big && (c.weapon == 34 || c.weapon == 35)) continue;  // no sniper rifle or rockets in a small room
            l = &c;
        }
        Item it;
        if (!l || !spot(it.pos)) continue;
        it.model = (uint16_t)l->model;
        it.ammo = (uint16_t)l->ammo;
        items.push_back(it);
    }
    for (int k = 0; k < armour; ++k) {
        Item it;
        if (!spot(it.pos)) continue;
        it.model = MODEL_ARMOUR;
        items.push_back(it);
    }
    g_lastItemsAt.clear();
    for (const Item& it : items) g_lastItemsAt.push_back(it.pos);
    Send(A_ITEMS, [&](Writer& w) {
        w.U8((uint8_t)items.size());
        for (const Item& it : items) {
            w.U16(it.model);
            w.U16(it.ammo);
            w.Vec({it.pos.x, it.pos.y, it.pos.z});
        }
    });
    MakeItems(items);
    LOGI("Arena: %d of %d weapons and %d armour laid out for wave %d (drinks machines: %d)", (int)items.size() - armour, guns,
         armour, forWave, soda ? 1 : 0);
}

// the waits before the waves: 30 s in a big place and in the crack palace (index 0), 6 s elsewhere
// the waits before the waves: 30 s on every arena (20 s before the first)
uint64_t PrepMs(bool first) { return first ? 20000 : 30000; }

// ---------------------------------------------------------------- points, barricades, the weapon box (from CoD)
// Points: every player has its own (500 to start): 10 a hit on an enemy, 50 a kill, 100 a kill by a headshot, 130 with a
// blade or the fists, more for the flamethrower man, the Twins and the boss. Its game counts them and tells the others
// (A_SCORE). They pay for the weapon box (950) and come from repairing barricades (10 a board, 500 a wave at most).
// Barricades: the host puts 3 (5 in a big place) across the ways in, far from the middle; most squads come from behind
// one and tear its boards off before they get through (1 every 1.6 s, quicker with more of them at it). A player at a
// barricade presses E to nail a board back. The weapon box: E with 950 points, 3 s of turning over weapons, then one of
// them (rare ones too: the minigun, rockets, the flamethrower); now and then a teddy bear: the points back, and the box
// moves elsewhere.
constexpr int kStartPoints = 500, kBoxCost = 950, kRepairCost = 10;
int g_points = kStartPoints;
// the other players' line on the scoreboard (Tab): their money, kills, falls
struct Score {
    int cash = 0, kills = 0, falls = 0;
};
std::map<uint8_t, Score> g_scores;
int g_kills = 0, g_falls = 0;  // this player's
float g_cashMult = 1.0f;       // the outfit's money bonus
uint64_t g_lastRepairMs = 0;
struct Popup {
    std::string text;
    uint64_t at;
    uint32_t color;
};
std::vector<Popup> g_popups;
std::map<CPed*, uint64_t> g_hitMs;  // the last hit that gave points (shotgun pellets: one a shot)
std::set<CPed*> g_killed;           // the kills already paid
bool g_placed = false;              // host: the barricades and the box are out
bool g_dead = false;                // this player died in the arena: back at the next wave

void SendScore() {
    Send(A_SCORE, [](Writer& w) {
        w.I32(g_points);
        w.U16((uint16_t)std::min(g_kills, 65000));
        w.U16((uint16_t)std::min(g_falls, 65000));
    });
}
// the arena's money (g_points): what the enemies drop, spent at the box, the wardrobe, the barricades
void AddPoints(int n, uint32_t color = 0xFF70E070) {
    if (n == 0) return;
    g_points = std::max(0, g_points + n);
    g_popups.push_back({Format(n > 0 ? "+$%d" : "-$%d", n > 0 ? n : -n), NowMs(), n > 0 ? color : 0xFFFF5050});
    if (g_popups.size() > 8) g_popups.erase(g_popups.begin());
    SendScore();
}

const char* WeaponName(int w) {
    switch (w) {
        case 4: return TR("нож", "knife");
        case 5: return TR("бита", "bat");
        case 8: return TR("катана", "katana");
        case 9: return TR("бензопила", "chainsaw");
        case 16: return TR("гранаты", "grenades");
        case 18: return TR("коктейли Молотова", "molotovs");
        case 22: return TR("пистолет", "pistol");
        case 23: return TR("пистолет с глушителем", "silenced pistol");
        case 24: return TR("Desert Eagle", "Desert Eagle");
        case 25: return TR("дробовик", "shotgun");
        case 26: return TR("обрез", "sawn-off");
        case 27: return TR("боевой дробовик", "combat shotgun");
        case 28: return TR("Micro Uzi", "Micro Uzi");
        case 29: return TR("MP5", "MP5");
        case 30: return TR("АК-47", "AK-47");
        case 31: return TR("M4", "M4");
        case 32: return TR("Tec-9", "Tec-9");
        case 33: return TR("винтовка", "country rifle");
        case 34: return TR("снайперская винтовка", "sniper rifle");
        case 35: return TR("гранатомёт", "rocket launcher");
        case 36: return TR("ракетница с наведением", "heat-seeker");
        case 37: return TR("огнемёт", "flamethrower");
        case 38: return TR("миниган", "minigun");
        default: return "?";
    }
}

struct BoxGun {
    int weapon, ammo, weight;
};
const BoxGun kBoxGuns[] = {
    {38, 400, 1}, {35, 6, 2}, {36, 5, 1}, {37, 500, 3}, {34, 30, 4}, {31, 300, 7}, {30, 300, 7}, {27, 70, 6},
    {26, 70, 6},  {25, 50, 5}, {29, 300, 7}, {28, 300, 5}, {32, 300, 5}, {24, 70, 7}, {33, 40, 4}, {16, 8, 4},
    {18, 8, 4},   {9, 1, 2},   {8, 1, 3},   {0, 0, 4},  // the last: a homie who guards whoever got him
};
const char* BoxGunName(int i) {
    int w = kBoxGuns[(size_t)i].weapon;
    return w ? WeaponName(w) : TR("Кент", "A homie");
}
// the slot a weapon goes in and what this player holds there now (0: nothing, or the same weapon)
int SlotTaken(CPlayerPed* me, int weapon) {
    if (!me || weapon <= 0) return 0;
    void* info = Call<0x743C60, void*>(weapon, (uint8_t)1);  // CWeaponInfo::GetWeaponInfo
    int slot = info ? Field<int32_t>(info, 0x14) : -1;      // m_nSlot
    if (slot < 0 || slot >= NUM_WEAPON_SLOTS) return 0;
    int held = me->Weapons()[slot].type;
    return held == weapon ? 0 : held;
}
int RollGun() {
    int total = 0;
    for (const BoxGun& g : kBoxGuns) total += g.weight;
    int r = RandInt(0, total - 1);
    for (int i = 0; i < (int)(sizeof(kBoxGuns) / sizeof(kBoxGuns[0])); ++i)
        if ((r -= kBoxGuns[i].weight) < 0) return i;
    return 0;
}

// ---- objects of the arena (each game makes its own, at the host's places)
// how far its origin is over the bottom of its collision (CBaseModelInfo::m_pColModel, CColModel's box), and its height
void ColOf(int model, float& lift, float& height) {
    lift = 0, height = 1.2f;
    void* info = Ref<void*>(0xA9B0C8 + (uintptr_t)model * 4);  // CModelInfo::ms_modelInfoPtrs
    void* col = info ? Field<void*>(info, 0x14) : nullptr;
    if (!col) return;
    CVector mn = Field<CVector>(col, 0x00), mx = Field<CVector>(col, 0x0C);
    if (mx.z > mn.z && mx.z - mn.z < 10.0f) lift = -mn.z, height = mx.z - mn.z;
}
int MakeObj(int model, const CVector& floor, float heading, float& lift, float& height) {
    if (!IsObjectModel(model) || !LoadModelNow(model) || !HasRoom(ObjectPool(), kKeepObjects, "object")) return -1;
    ColOf(model, lift, height);
    int h = -1;
    Cmd(0x0107).I(model).F(floor.x).F(floor.y).F(floor.z + lift).Out(&h).Run();  // CREATE_OBJECT
    if (h < 0) return -1;
    Cmd(0x0177).I(h).F(heading).Run();                       // SET_OBJECT_HEADING
    Cmd(0x0566).I(h).I(kPlaces[g_arena.place].area).Run();   // LINK_OBJECT_TO_INTERIOR
    Cmd(0x0392).I(h).I(0).Run();                             // not moved by hits
    return h;
}
void DelObj(int& h) {
    if (h >= 0 && GetObject(h)) Cmd(0x0108).I(h).Run();  // DELETE_OBJECT
    h = -1;
}
int PickModel(std::initializer_list<int> models) {
    for (int m : models)
        if (IsObjectModel(m)) return m;
    return -1;
}

// ---- barricades
// a barricade's boards as its fence: lower into the floor board by board, gone with the last
void ShowBar(Bar& b) {
    if (b.boards <= 0) {
        DelObj(b.handle);
        return;
    }
    if (b.handle < 0 || !GetObject(b.handle)) {
        b.handle = -1;
        int model = PickModel({1418, 1408, 1424});  // DYN_F_WOOD_3, DYN_F_WOOD_2, DYN_ROADBARRIER_2
        if (model < 0) return;
        b.handle = MakeObj(model, b.pos, b.heading, b.lift, b.height);
        if (b.handle < 0) return;
    }
    Cmd(0x0382).I(b.handle).I(g_barsSolid ? 1 : 0).Run();  // SET_OBJECT_COLLISION: none between the waves
    float sink = (1.0f - (float)b.boards / (float)kBoards) * b.height * 0.85f;
    Cmd(0x01BC).I(b.handle).F(b.pos.x).F(b.pos.y).F(b.pos.z + b.lift - sink).Run();  // SET_OBJECT_COORDINATES
}
void SetBoards(int i, int boards, uint8_t by);
// Shots smash the fence (a breakable one): it is put up again at once, and only every 3rd smash costs a board (host)
void CheckSmashed() {
    for (size_t i = 0; i < g_bars.size(); ++i) {
        Bar& b = g_bars[i];
        CObject* o = b.handle >= 0 ? GetObject(b.handle) : nullptr;
        if (!o || o->IsVisible() || (o->Flags() & 1)) continue;  // smashed: no rendering, no collision
        DelObj(b.handle);
        if (g_arena.host && ++b.breaks % 3 == 0) SetBoards((int)i, b.boards - 1, 0xFF);
        else ShowBar(b);
    }
}

void ClearBars() {
    for (Bar& b : g_bars) DelObj(b.handle);
    g_bars.clear();
}
void SendBars() {
    Send(A_BARS, [](Writer& w) {
        w.U8((uint8_t)g_bars.size());
        for (const Bar& b : g_bars) {
            w.Vec({b.pos.x, b.pos.y, b.pos.z});
            w.Vec({b.spawn.x, b.spawn.y, b.spawn.z});
            w.F32(b.heading);
            w.U8((uint8_t)b.boards);
            w.U8(b.built);
        }
    });
}
// a board more or less (host: and every game hears of it); `by`: who nailed it on (0xFF: torn off)
void SetBoards(int i, int boards, uint8_t by) {
    if (i < 0 || i >= (int)g_bars.size()) return;
    Bar& b = g_bars[(size_t)i];
    int was = b.boards;
    b.boards = std::clamp(boards, 0, kBoards);
    ShowBar(b);
    if (g_arena.host) Send(A_BOARD, [&](Writer& w) {
        w.U8((uint8_t)i);
        w.U8((uint8_t)b.boards);
        w.U8(by);
    });
    if (by == Net().MyId() && b.boards > was) AddPoints(-kRepairCost);  // a board nailed on costs 10 points
}

// host: the barricades across the ways in (spawn cells far from the middle and from each other, the barricade two
// steps on towards the middle), the box near the middle
void PlaceBarricades() {
    ClearBars();
    if (!GridUsable()) return;
    const Place& pl = kPlaces[g_arena.place];
    int mid = CellNear(pl.pos + CVector{0, 0, 1.0f}, 6.0f);
    if (mid < 0) return;
    std::vector<int> steps = StepsTo(mid);
    int want = pl.big ? 5 : 3;
    std::vector<int> chosen;
    for (int n = 0; n < want; ++n) {
        int best = -1;
        float bestScore = -1;
        for (int k : StandCells()) {
            if (steps[(size_t)k] < 4) continue;  // far enough from the middle to have a way in
            const CVector& c = g_cells[(size_t)k].p;
            float apart = 1e9f;
            for (int o : chosen) apart = std::min(apart, (g_cells[(size_t)o].p - c).Length());
            if (apart < 7.0f) continue;
            float score = std::min((float)steps[(size_t)k], 25.0f) + std::min(apart, 30.0f) * 0.7f + Rand(0.0f, 5.0f);
            if (score > bestScore) bestScore = score, best = k;
        }
        if (best < 0) break;
        chosen.push_back(best);
        int at = best;
        for (int s = 0; s < 2; ++s) {  // two steps towards the middle
            int next = at;
            for (int nb : g_cells[(size_t)at].next)
                if (steps[(size_t)nb] >= 0 && steps[(size_t)nb] < steps[(size_t)next]) next = nb;
            at = next;
        }
        Bar b;
        b.spawn = g_cells[(size_t)best].p;
        b.pos = g_cells[(size_t)at].p;
        CVector d = b.pos - b.spawn;
        if (d.Length2D() < 0.5f) d = pl.pos - b.spawn;
        b.heading = std::atan2(-d.x, d.y) * 57.29578f;
        g_bars.push_back(b);
    }
    for (Bar& b : g_bars) ShowBar(b);
    SendBars();
    LOGI("Arena: %d barricades", (int)g_bars.size());
}

// ---- the weapon box
struct BoxState {
    bool placed = false;
    CVector pos;
    float heading = 0;
    int handle = -1;
    float lift = 0, height = 1.0f;
    int handle2 = -1;  // the second crate of it
    int uses = 0;  // host: since it last moved
    bool rolling = false;
    uint64_t rollEnd = 0, moveAt = 0;
    uint8_t by = 0;
    int gun = 0;
    bool teddy = false;
    bool offered = false;  // its weapon waits over it for the one who paid (E takes it) till then
    uint64_t offerUntil = 0;
} g_box;

void PreloadBoxGuns();
void ShowWardrobe();
// The box is big: two crates side by side, each half as big again (CObject::m_fScale +0x15C): three crates wide and
// one and a half high
void ShowBox() {
    DelObj(g_box.handle);
    DelObj(g_box.handle2);
    if (!g_box.placed) return;
    int model = PickModel({3014, 1271, 964});  // cr_guncrate, gunbox, CJ_METAL_CRATE
    if (model < 0) return;
    constexpr float kScale = 1.5f;
    float lift = 0, height = 1.0f;
    ColOf(model, lift, height);
    float width = 0.8f;
    if (void* info = Ref<void*>(0xA9B0C8 + (uintptr_t)model * 4))
        if (void* col = Field<void*>(info, 0x14)) width = std::clamp(Field<CVector>(col, 0x0C).x - Field<CVector>(col, 0x00).x, 0.3f, 3.0f);
    float r = g_box.heading / 57.29578f;
    CVector right{std::cos(r), std::sin(r), 0.0f};
    for (int k = 0; k < 2; ++k) {
        CVector at = g_box.pos + right * (width * kScale * 0.5f * (k ? 1.0f : -1.0f));
        float l2 = 0, h2 = 0;
        int h = MakeObj(model, at - CVector{0, 0, 0.0f}, g_box.heading, l2, h2);
        if (h < 0) continue;
        Cmd(0x01BC).I(h).F(at.x).F(at.y).F(at.z + lift * kScale).Run();  // SET_OBJECT_COORDINATES: on the floor, grown
        if (CObject* o = GetObject(h)) Field<float>(o, 0x15C) = kScale;    // CObject::m_fScale
        (k ? g_box.handle2 : g_box.handle) = h;
    }
    g_box.lift = lift * kScale;
    g_box.height = height * kScale;
    PreloadBoxGuns();  // its weapons turn over it, without a hitch then
    ShowWardrobe();
}
void SendBox() {
    Send(A_BOX, [](Writer& w) {
        w.U8(g_box.placed);
        w.Vec({g_box.pos.x, g_box.pos.y, g_box.pos.z});
        w.F32(g_box.heading);
    });
}
// host: a spot for the box on the floor (near the middle, away from where it stood and from the barricades)
// ---- the wardrobe: a box of clothes by the weapon box; E and $600: a random outfit, each with its own perk
enum OutfitPerk : uint8_t { O_ARMOUR, O_REGEN, O_HEALTH, O_SPRINT, O_RELOAD, O_CASH };
struct Outfit {
    int model;
    const char* name[2];
    const char* perk[2];
    OutfitPerk kind;
};
const Outfit kOutfits[] = {
    {285, TRP("Спецназ", "SWAT"), TRP("+50 брони каждую волну", "+50 armour each wave"), O_ARMOUR},
    {274, TRP("Медик", "Medic"), TRP("здоровье само восстанавливается", "health comes back by itself"), O_REGEN},
    {287, TRP("Солдат", "Soldier"), TRP("150 здоровья", "150 health"), O_HEALTH},
    {247, TRP("Байкер", "Biker"), TRP("бег без усталости", "running without tiring"), O_SPRINT},
    {108, TRP("Вагос", "Vagos"), TRP("быстрая перезарядка", "fast reload"), O_RELOAD},
    {111, TRP("Мафиози", "Mafioso"), TRP("+50% денег", "+50% money"), O_CASH},
};
constexpr int kOutfitCount = (int)(sizeof(kOutfits) / sizeof(kOutfits[0]));
constexpr int kWardCost = 600;
struct Wardrobe {
    CVector pos;
    float heading = 0;
    int handle = -1;
} g_ward;
int g_outfit = -1;            // this player's outfit in the arena (-1: none)
int g_modelBefore = -1;       // what it wore before it
uint8_t g_maxHealthBefore = 0;
void ShowWardrobe() {  // every game: by the box, to its right, on the floor
    DelObj(g_ward.handle);
    if (!g_box.placed) return;
    int model = PickModel({1220, 1230, 1221});  // a cardboard box
    if (model < 0) return;
    float r = g_box.heading / 57.29578f;
    CVector right{std::cos(r), std::sin(r), 0.0f};
    CVector at = g_box.pos + right * 2.6f;
    float z = 0;
    if (FloorBelow(at.x, at.y, at.z + 1.2f, z) && std::fabs(z - g_box.pos.z) < 1.0f) at.z = z;
    else at = g_box.pos - right * 2.6f;  // no floor there: the other side
    float lift = 0, height = 0;
    g_ward.pos = at;
    g_ward.heading = g_box.heading;
    g_ward.handle = MakeObj(model, at, g_box.heading, lift, height);
    if (CObject* o = g_ward.handle >= 0 ? GetObject(g_ward.handle) : nullptr) {
        Field<float>(o, 0x15C) = 1.6f;  // CObject::m_fScale
        Cmd(0x01BC).I(g_ward.handle).F(at.x).F(at.y).F(at.z + lift * 1.6f).Run();
    }
}
void OutfitPerks(bool on) {  // the perks that stay on while it is worn
    if (g_outfit < 0) return;
    OutfitPerk k = kOutfits[g_outfit].kind;
    if (k == O_SPRINT) Cmd(0x0330).I(0).I(on ? 1 : 0).Run();  // SET_PLAYER_NEVER_GETS_TIRED
    if (k == O_RELOAD) Cmd(0x0331).I(0).I(on ? 1 : 0).Run();  // SET_PLAYER_FAST_RELOAD
    if (k == O_CASH) g_cashMult = on ? 1.5f : 1.0f;
    if (k == O_HEALTH) {
        if (on) {
            if (!g_maxHealthBefore) g_maxHealthBefore = PlayerInfos()[0].maxHealth;
            PlayerInfos()[0].maxHealth = 150;
        } else if (g_maxHealthBefore) {
            PlayerInfos()[0].maxHealth = g_maxHealthBefore;
            g_maxHealthBefore = 0;
        }
    }
}
void OutfitWaveStart() {  // at each wave: the armour, the health
    CPlayerPed* me = FindPlayerPed(0);
    if (g_outfit < 0 || !me || g_spectating) return;
    OutfitPerk k = kOutfits[g_outfit].kind;
    if (k == O_ARMOUR) me->Armour() = std::min(100.0f, me->Armour() + 50.0f);
    if (k == O_HEALTH) me->Health() = std::max(me->Health(), 150.0f);
}
void BuyOutfit() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me) return;
    if (g_points < kWardCost) {
        ui::TheChat().AddError(Format(TR("Нужно $%d.", "You need $%d."), kWardCost));
        return;
    }
    int k = RandInt(0, kOutfitCount - 1);
    if (k == g_outfit) k = (k + 1) % kOutfitCount;
    OutfitPerks(false);
    if (g_modelBefore < 0) g_modelBefore = me->ModelIndex();
    g_outfit = k;
    AddPoints(-kWardCost);
    skin::Wear((uint16_t)kOutfits[k].model);
    OutfitPerks(true);
    OutfitWaveStart();
    CallMethod<0x506EA0>((void*)0xB6BC90, 40 /* AE_FRONTEND_PURCHASE_WEAPON */, 0.0f, 1.0f);
    ui::TheChat().AddSystem(Format("%s: %s", kOutfits[k].name[Lang()], kOutfits[k].perk[Lang()]));
}
void TakeOffOutfit() {  // the arena is over: its perks go, the clothes from before it come back
    OutfitPerks(false);
    if (g_outfit >= 0 && g_modelBefore >= 0) skin::Wear((uint16_t)g_modelBefore);
    g_outfit = -1;
    g_modelBefore = -1;
    g_cashMult = 1.0f;
}

// ---- the homie from the box: a Grove Street man with an M4 who follows this player (its group) and fights the arena's
// people; this player's game keeps him (the others see him as its ped). He is gone with the arena.
std::vector<int> g_guards;
void SpawnBodyguard() {
    CPlayerPed* me = FindPlayerPed(0);
    int model = 105 + RandInt(0, 2);
    if (!me || !LoadModelNow(model) || !HasRoom(PedPool(), kKeepPeds + 2, "ped")) return;
    CVector f = cam::Front();
    f.z = 0;
    float l = std::max(0.01f, f.Length());
    CVector at = me->GetPosition() - f * (1.2f / l);
    int h = -1;
    Cmd(0x009A).I(8 /* PED_TYPE_GANG2 */).I(model).F(at.x).F(at.y).F(at.z - 1.0f).Out(&h).Run();  // CREATE_CHAR
    if (h < 0) return;
    if (CPed* made = GetPed(h)) {
        Peds::Instance().Share(made);
        made->Armour() = 100.0f;
    }
    Cmd(0x0860).I(h).I(me->AreaCode()).Run();  // SET_CHAR_AREA_VISIBLE
    Cmd(0x0223).I(h).I(300).Run();             // SET_CHAR_HEALTH
    Cmd(0x087E).I(h).I(0).Run();               // no weapons off him
    Give(h, 31, 9999);
    Cmd(0x01B9).I(h).I(GunFor(31)).Run();
    Cmd(0x02E2).I(h).I(75).Run();              // SET_CHAR_ACCURACY
    Cmd(0x077A).I(h).I(4).I(PED_TYPE_MISSION1).Run();  // SET_CHAR_RELATIONSHIP: hates the arena's people
    int group = -1;
    Cmd(0x07AF).I(0).Out(&group).Run();        // GET_PLAYER_GROUP
    if (group >= 0) Cmd(0x0631).I(group).I(h).Run();  // SET_GROUP_MEMBER: he follows and fights for this player
    g_guards.push_back(h);
    ui::TheChat().AddSystem(TR("Кент с вами.", "A homie is with you."));
}
void GuardsTo(const CVector& at, int area) {  // with this player to another place
    for (int h : g_guards)
        if (CPed* p = GetPed(h); p && p->Health() > 0.0f) {
            Cmd(0x0860).I(h).I(area).Run();
            Cmd(0x00A1).I(h).F(at.x + 1.0f).F(at.y).F(at.z - 1.0f).Run();
        }
}
void ClearGuards() {
    for (int h : g_guards)
        if (GetPed(h)) Cmd(0x009B).I(h).Run();  // DELETE_CHAR
    g_guards.clear();
}

void PlaceBox() {
    const Place& pl = kPlaces[g_arena.place];
    CVector best = pl.pos - CVector{0, 0, 1.0f};
    bool found = false;
    if (GridUsable()) {
        float bestScore = -1e9f;
        for (int k : StandCells()) {
            const CVector& c = g_cells[(size_t)k].p;
            float mid = (c - pl.pos).Length2D();
            if (mid < 3.0f || mid > std::max(8.0f, pl.reach * 0.7f)) continue;
            float score = Rand(0.0f, 10.0f) - mid * 0.3f;
            if (g_box.placed) score += std::min((c - g_box.pos).Length(), 20.0f);
            for (const Bar& b : g_bars) score -= std::max(0.0f, 6.0f - (c - b.pos).Length()) * 3.0f;
            if (score > bestScore) bestScore = score, best = c, found = true;
        }
    }
    if (!found) {
        CVector p = pl.pos + CVector{2.5f, 0, 0};
        if (OnFloor(p, pl.pos.z)) best = p - CVector{0, 0, 1.0f};
    }
    g_box.placed = true;
    g_box.pos = best;
    CVector d = pl.pos - best;
    g_box.heading = std::atan2(-d.x, d.y) * 57.29578f;
    g_box.uses = 0;
    g_box.rolling = false;
    ShowBox();
    SendBox();
}

// every game: someone opened the box
void StartRoll(uint8_t by, int gun, bool teddy) {
    g_box.rolling = true;
    g_box.rollEnd = NowMs() + 3200;
    g_box.by = by;
    g_box.gun = gun;
    g_box.teddy = teddy;
    if (by == Net().MyId() || !Net().Online()) AddPoints(-kBoxCost);
}
// host: a player opens it (its points it checked itself)
void UseBox(uint8_t by) {
    if (!g_box.placed || g_box.rolling || g_box.offered || g_box.moveAt) return;
    ++g_box.uses;
    bool teddy = g_box.uses > 4 && RandInt(0, 4) == 0;
    int gun = RollGun();
    Send(A_BOXROLL, [&](Writer& w) {
        w.U8(by);
        w.U8((uint8_t)gun);
        w.U8(teddy);
    });
    StartRoll(by, gun, teddy);
}
void SpawnBodyguard();
// E at the box while its weapon waits for this player: it takes it (a weapon in the same slot goes for it)
void TakeOffer() {
    if (!g_box.offered || !(g_box.by == Net().MyId() || !Net().Online())) return;
    g_box.offered = false;
    Send(A_BOXTAKEN);
    const BoxGun& g = kBoxGuns[(size_t)g_box.gun % (sizeof(kBoxGuns) / sizeof(kBoxGuns[0]))];
    if (!g.weapon) {
        SpawnBodyguard();
        return;
    }
    if (CPlayerPed* me = FindPlayerPed(0)) {
        int h = GetPedRef(me);
        Give(h, g.weapon, g.ammo);
        Cmd(0x01B9).I(h).I(g.weapon).Run();  // SET_CURRENT_CHAR_WEAPON
        CallMethod<0x506EA0>((void*)0xB6BC90, 6 /* AE_FRONTEND_PICKUP_WEAPON */, 0.0f, 1.0f);
    }
}
void BoxSound(int event, float speed);
void UpdateBox(uint64_t now) {
    if (g_box.rolling && now >= g_box.rollEnd) {
        g_box.rolling = false;
        bool mine = g_box.by == Net().MyId() || !Net().Online();
        if (g_box.teddy) {
            if (mine) AddPoints(kBoxCost);
            BoxSound(4 /* AE_FRONTEND_ERROR */, 1.0f);
            ui::TheChat().AddSystem(TR("Мишка! Ящик переезжает.", "A teddy bear! The box moves."));
            DelObj(g_box.handle);
            DelObj(g_box.handle2);
            if (g_arena.host) g_box.moveAt = now + 6000;
        } else {  // the weapon waits over the box: the one who paid takes it with E (8 s), or not
            g_box.offered = true;
            g_box.offerUntil = now + 8000;
        }
    }
    if (g_box.offered && now >= g_box.offerUntil) g_box.offered = false;  // not taken: back into the box
    if (g_arena.host && g_box.moveAt && now >= g_box.moveAt) {
        g_box.moveAt = 0;
        PlaceBox();
    }
}

// ---- the box's turning weapon: over the open box the models of its weapons go round, then the one it gives
int g_spinHandle = -1, g_spinModel = -1;
int WeaponModel(int weapon) {
    int wm = -1;
    Cmd(0x0781).I(weapon).Out(&wm).Run();  // GET_WEAPONTYPE_MODEL
    return wm;
}
// an object hanging free (no collision, no physics) at `at`
int MakeFloating(int model, const CVector& at, float heading) {
    if (model <= 0 || !IsModelLoaded(model) || !HasRoom(ObjectPool(), kKeepObjects, "object")) return -1;
    int h = -1;
    Cmd(0x0107).I(model).F(at.x).F(at.y).F(at.z).Out(&h).Run();  // CREATE_OBJECT
    if (h < 0) return -1;
    Cmd(0x0382).I(h).I(0).Run();                            // SET_OBJECT_COLLISION off
    Cmd(0x0392).I(h).I(0).Run();
    Cmd(0x0566).I(h).I(kPlaces[g_arena.place].area).Run();  // LINK_OBJECT_TO_INTERIOR
    Cmd(0x01BC).I(h).F(at.x).F(at.y).F(at.z).Run();         // SET_OBJECT_COORDINATES (as it is, not on the floor)
    Cmd(0x0177).I(h).F(heading).Run();
    return h;
}
void PreloadBoxGuns() {
    for (const BoxGun& g : kBoxGuns) {
        int m = WeaponModel(g.weapon);
        if (m > 0 && !IsModelLoaded(m)) LoadModelNow(m);
    }
}
// the box's sounds (the frontend's: a tick each weapon it turns over, a fanfare for the one it gives), heard near it
void BoxSound(int event, float speed = 1.0f) {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || (me->GetPosition() - g_box.pos).Length() > 30.0f) return;
    CallMethod<0x506EA0>((void*)0xB6BC90, event, 0.0f, speed);  // CAudioEngine::ReportFrontendAudioEvent
}
int g_spinShown = -1;
bool g_spinDone = false;
void SpinBox(uint64_t now) {
    bool show = g_box.placed && g_box.handle >= 0 && (g_box.rolling || g_box.offered);
    if (!show) {
        DelObj(g_spinHandle);
        g_spinModel = -1;
        return;
    }
    int n = (int)(sizeof(kBoxGuns) / sizeof(kBoxGuns[0]));
    bool final = now + 500 >= g_box.rollEnd;
    int gun = final ? g_box.gun : (int)((now / 140) % (uint64_t)n);
    if (g_box.rolling && gun != g_spinShown && !final) BoxSound(3 /* AE_FRONTEND_HIGHLIGHT */, 0.9f + 0.4f * Rand(0.0f, 1.0f));
    if (!g_box.rolling) g_spinShown = -1;
    else g_spinShown = gun;
    if (final && !g_spinDone && !g_box.teddy) g_spinDone = true, BoxSound(43 /* AE_FRONTEND_PART_MISSION_COMPLETE */);
    if (g_box.rolling && !final) g_spinDone = false;
    int model = WeaponModel(kBoxGuns[(size_t)gun].weapon);
    if (final && model > 0 && !IsModelLoaded(model)) LoadModelNow(model);
    float rise = std::min(1.0f, (float)(now + 3200 - std::min<uint64_t>(g_box.rollEnd, now + 3200)) / 1200.0f);
    CVector at = g_box.pos + CVector{0, 0, g_box.height + 0.25f + 0.45f * rise};
    float heading = (float)(now % 2000) * 0.18f;  // a turn in 2 s
    if (model != g_spinModel || g_spinHandle < 0 || !GetObject(g_spinHandle)) {
        DelObj(g_spinHandle);
        g_spinModel = model;
        g_spinHandle = MakeFloating(model, at, heading);
        return;
    }
    Cmd(0x01BC).I(g_spinHandle).F(at.x).F(at.y).F(at.z).Run();
    Cmd(0x0177).I(g_spinHandle).F(heading).Run();
}

// ---- weapons on the walls: bought for points (again: its ammo for half the price)
struct WallGun {
    int weapon, ammo, price;
};
const WallGun kWallGuns[] = {
    {22, 68, 250},   {24, 42, 700},   {25, 40, 500},   {26, 40, 800},  {28, 240, 600}, {32, 240, 600}, {29, 240, 1000},
    {30, 200, 1200}, {31, 200, 1500}, {27, 50, 1500},  {33, 30, 600},  {34, 20, 1500}, {16, 4, 250},   {18, 4, 250},
};
constexpr int kWallGunCount = (int)(sizeof(kWallGuns) / sizeof(kWallGuns[0]));
struct Wall {
    int gun = 0;   // index into kWallGuns
    CVector pos;   // on the wall
    CVector stand; // where the buyer stands
    float heading = 0;
    int handle = -1;
};
std::vector<Wall> g_walls;
void ShowWall(Wall& w) {
    DelObj(w.handle);
    int model = WeaponModel(kWallGuns[(size_t)w.gun].weapon);
    if (model > 0 && !IsModelLoaded(model)) LoadModelNow(model);
    w.handle = MakeFloating(model, w.pos, w.heading);
}
void ClearWalls() {
    for (Wall& w : g_walls) DelObj(w.handle);
    g_walls.clear();
}
void SendWalls() {
    Send(A_WALLS, [](Writer& wr) {
        wr.U8((uint8_t)g_walls.size());
        for (const Wall& w : g_walls) {
            wr.U8((uint8_t)w.gun);
            wr.Vec({w.pos.x, w.pos.y, w.pos.z});
            wr.Vec({w.stand.x, w.stand.y, w.stand.z});
            wr.F32(w.heading);
        }
    });
}
// a wall by a floor cell: of 8 rays at the chest the nearest one that meets a building's wall (not an object, not a
// door) standing upright and flat there (the rays 30 cm higher and lower meet it as far off); the gun 8 cm off it,
// lying along it
bool WallRay(const CVector& a, const CVector& b, CVector& at, CVector& normal) {
    struct {
        CVector point;
        float c;
        CVector normal;
        uint8_t rest[0x2C - 0x1C];
    } cp{};
    CEntity* hit = nullptr;
    bool struck = Call<0x56BA00, bool>(&a, &b, &cp, &hit, true, false, false, false, false, false, false, false);  // buildings only
    if (struck) at = cp.point, normal = cp.normal;
    return struck;
}
bool WallBy(const CVector& floor, Wall& out) {
    CVector chest = floor + CVector{0, 0, 1.3f};
    float best = 1.6f;
    bool found = false;
    for (int k = 0; k < 8; ++k) {
        float a = 6.2831853f * (float)k / 8.0f;
        CVector d{std::cos(a), std::sin(a), 0.0f}, hit, n;
        if (!WallRay(chest, chest + d * 1.6f, hit, n)) continue;
        float l = (hit - chest).Length();
        if (l < 0.35f || l >= best || std::fabs(n.z) > 0.25f) continue;  // a wall stands upright
        if (d.x * n.x + d.y * n.y > -0.7f) continue;                    // and faces the cell
        CVector h2, n2, h3, n3;
        if (!WallRay(chest + CVector{0, 0, 0.3f}, chest + CVector{0, 0, 0.3f} + d * 1.6f, h2, n2) ||
            !WallRay(chest - CVector{0, 0, 0.3f}, chest - CVector{0, 0, 0.3f} + d * 1.6f, h3, n3))
            continue;
        if (std::fabs((h2 - chest - CVector{0, 0, 0.3f}).Length() - l) > 0.12f || std::fabs((h3 - chest + CVector{0, 0, 0.3f}).Length() - l) > 0.12f)
            continue;  // not flat there (a ledge, a frame, a gap)
        // and as flat 40 cm to each side, so the gun lies on the wall and not over a corner
        CVector side{-n.y, n.x, 0.0f};
        CVector h4, n4, h5, n5;
        if (!WallRay(chest + side * 0.4f, chest + side * 0.4f + d * 1.6f, h4, n4) ||
            !WallRay(chest - side * 0.4f, chest - side * 0.4f + d * 1.6f, h5, n5))
            continue;
        if (std::fabs((h4 - hit).x * n.x + (h4 - hit).y * n.y) > 0.08f || std::fabs((h5 - hit).x * n.x + (h5 - hit).y * n.y) > 0.08f) continue;
        best = l;
        CVector flatN{n.x, n.y, 0.0f};
        float nl = std::max(0.01f, flatN.Length());
        flatN = flatN * (1.0f / nl);
        out.pos = hit + flatN * 0.08f;
        out.stand = floor;
        out.heading = std::atan2(-flatN.x, flatN.y) * 57.29578f;  // the model's barrel (its X) along the wall
        found = true;
    }
    return found;
}
bool AwayFromAll(const CVector& c, float apart) {
    for (const Wall& w : g_walls)
        if ((w.stand - c).Length() < apart) return false;
    for (const Bar& b : g_bars)
        if ((b.pos - c).Length() < 3.0f) return false;
    if (g_box.placed && (g_box.pos - c).Length() < 3.0f) return false;
    return true;
}
int RandomWallGun() {
    for (int t = 0; t < 10; ++t) {
        int g = RandInt(0, kWallGunCount - 1);
        bool taken = false;
        for (const Wall& w : g_walls) taken |= w.gun == g;
        if (!taken) return g;
    }
    return RandInt(0, kWallGunCount - 1);
}
void PlaceWalls(int want) {
    if (!GridUsable()) return;
    for (int t = 0; t < 400 && (int)g_walls.size() < want; ++t) {
        const Cell& c = g_cells[(size_t)RandInt(0, (int)g_cells.size() - 1)];
        Wall w;
        if (!AwayFromAll(c.p, 5.0f) || !WallBy(c.p, w)) continue;
        w.gun = RandomWallGun();
        g_walls.push_back(w);
    }
}
// host, at a new wave: now and then one of the walls gets another gun
void ShuffleWalls() {
    if (g_walls.empty() || RandInt(0, 99) >= 35) return;
    Wall& w = g_walls[(size_t)RandInt(0, (int)g_walls.size() - 1)];
    w.gun = RandomWallGun();
    ShowWall(w);
    SendWalls();
}
void BuyWall(int i) {
    const WallGun& g = kWallGuns[(size_t)g_walls[(size_t)i].gun];
    CPlayerPed* me = FindPlayerPed(0);
    if (!me) return;
    bool have = false;
    for (int k = 0; k < NUM_WEAPON_SLOTS; ++k) have |= me->Weapons()[k].type == g.weapon;
    int price = have ? g.price / 2 : g.price;
    if (g_points < price) {
        ui::TheChat().AddError(Format(TR("Нужно %d очков.", "You need %d points."), price));
        return;
    }
    AddPoints(-price);
    int h = GetPedRef(me);
    Give(h, g.weapon, g.ammo);
    Cmd(0x01B9).I(h).I(g.weapon).Run();  // SET_CURRENT_CHAR_WEAPON
}

// ---- what the special enemies drop (the flamethrower man, the Twins, the medics, the boss): ammo for everyone, or
// health for everyone; whoever comes up to it takes it (the host decides who was first)
// D_SMALL: a few clips for the one who takes it (now and then off a dead enemy); D_GUN: a weapon a player put down (J)
// with its ammo, for a team mate
enum DropType : uint8_t { D_AMMO, D_HEALTH, D_SMALL, D_GUN, D_CASH };  // D_CASH: money off a dead enemy (its sum in ammo)
struct Drop {
    uint8_t id = 0, type = 0;
    uint8_t weapon = 0, owner = 0xFF;  // D_GUN: what it is, who put it down (not its to take back for 2 s)
    uint8_t special = 0;               // D_GUN: 1 T-Bone's AK-47 with the grenade launcher under it
    uint16_t ammo = 0;
    CVector pos;
    int handle = -1;
    uint64_t until = 0, born = 0;
    bool asked = false;
};
std::vector<Drop> g_drops;
uint8_t g_nextDrop = 1;
const char* WeaponName(int w);
int WeaponModel(int weapon);
std::string DropName(const Drop& d) {
    switch (d.type) {
        case D_AMMO: return TR("Пополнение патронов", "Ammo refill");
        case D_HEALTH: return TR("Пополнение здоровья", "Health refill");
        case D_SMALL: return TR("Патроны", "Ammo");
        case D_CASH: return Format("$%d", (int)d.ammo);
        default:
            if (d.special == 1) return Format(TR("АК-47 с подствольником · %d", "AK-47 with a grenade launcher · %d"), (int)d.ammo);
            return Format("%s · %d", WeaponName(d.weapon), (int)d.ammo);
    }
}
void ShowDrop(Drop& d) {
    int model = d.type == D_AMMO || d.type == D_SMALL ? PickModel({2358, 2969, 2040})  // an ammo box
                : d.type == D_HEALTH                  ? PickModel({1240, 1580})        // the heart
                : d.type == D_CASH                    ? PickModel({1212, 1550})        // a wad of money, a money bag
                                                      : WeaponModel(d.weapon);
    if (model > 0 && !IsModelLoaded(model)) LoadModelNow(model);
    d.handle = MakeFloating(model, d.pos, 0.0f);
}
void AddDrop(const Drop& from) {
    Drop d = from;
    d.born = NowMs();
    d.until = d.born + (d.type == D_GUN ? 90000 : d.type == D_CASH ? 45000 : 30000);
    ShowDrop(d);
    g_drops.push_back(d);
}
void WriteDrop(Writer& w, const Drop& d) {
    w.U8(d.id);
    w.U8(d.type);
    w.Vec({d.pos.x, d.pos.y, d.pos.z});
    w.U8(d.weapon);
    w.U16(d.ammo);
    w.U8(d.owner);
    w.U8(d.special);
}
void SpawnDrop(uint8_t type, const CVector& at, uint8_t weapon = 0, uint16_t ammo = 0, uint8_t owner = 0xFF, uint8_t special = 0) {  // host
    Drop d;
    d.special = special;
    d.id = g_nextDrop++;
    if (!g_nextDrop) g_nextDrop = 1;
    d.type = type, d.pos = at, d.weapon = weapon, d.ammo = ammo, d.owner = owner;
    Send(A_DROP, [&](Writer& w) { WriteDrop(w, d); });
    AddDrop(d);
}
// a full load more for every gun this player has (GIVE_WEAPON_TO_CHAR adds), `loads` of them; it keeps the one in hand
void MoreAmmo(CPlayerPed* me, float loads) {
    int current = me->ActiveWeapon().type;
    for (int k = 0; k < NUM_WEAPON_SLOTS; ++k) {
        CWeapon& w = me->Weapons()[k];
        if (w.type < 16 || w.type > 38) continue;  // no melee, no cameras or sprays
        int clip = 0;
        if (void* info = Call<0x743C60, void*>(w.type, (uint8_t)1)) clip = Field<int16_t>(info, 0x20);  // m_nAmmoClip
        int full = w.type <= 18 ? 8 : w.type == 35 || w.type == 36 ? 10 : w.type == 37 ? 500 : w.type == 38 ? 500 : std::max(30, clip * 8);
        int add = std::max(1, (int)((float)full * loads));
        if (w.ammoTotal < 9000) Cmd(0x01B2).I(GetPedRef(me)).I(w.type).I(add).Run();  // GIVE_WEAPON_TO_CHAR: that much more
    }
    Cmd(0x01B9).I(GetPedRef(me)).I(current).Run();  // SET_CURRENT_CHAR_WEAPON: the one it held
}
int GunFor(int weapon);
void ApplyDrop(const Drop& d, uint8_t by) {
    CPlayerPed* me = FindPlayerPed(0);
    bool mine = by == Net().MyId() || !Net().Online();
    std::string who = mine ? std::string(TR("Вы", "You")) : Net().PeerName(by);
    if (d.type == D_AMMO || d.type == D_HEALTH)
        ui::TheChat().AddSystem(Format(TR("%s: %s для всех", "%s: %s for everyone"), who.c_str(), DropName(d).c_str()));
    if (!me || me->Health() <= 0.0f || !arena::On()) return;
    if (d.type == D_HEALTH) {
        me->Health() = std::max(me->Health(), (float)std::max<int>(PlayerInfos()[0].maxHealth, 100));
    } else if (d.type == D_AMMO) {
        MoreAmmo(me, 1.0f);
    } else if (d.type == D_SMALL && mine) {
        MoreAmmo(me, 0.35f);
    } else if (d.type == D_CASH && mine) {
        AddPoints((int)((float)d.ammo * g_cashMult));
        CallMethod<0x506EA0>((void*)0xB6BC90, 7 /* AE_FRONTEND_PICKUP_MONEY */, 0.0f, 1.0f);
    } else if (d.type == D_GUN && mine) {
        int h = GetPedRef(me);
        int w = GunFor(d.weapon);
        if (w) Cmd(0x01B2).I(h).I(w).I(std::max<int>(1, d.ammo)).Run(), Cmd(0x01B9).I(h).I(w).Run();
        ui::TheChat().AddSystem(Format(TR("Вы подобрали: %s.", "You picked up: %s."), DropName(d).c_str()));
        if (d.special == 1) {
            g_hasUgl = true;
            g_uglLoaded = true;
            ui::TheChat().AddSystem(TR("[N] Подствольник. Заряжается гранатой.", "[N] Launcher. Loads with a grenade."));
        }
    }
}
void TakeDrop(uint8_t id, uint8_t by) {  // host
    for (size_t i = 0; i < g_drops.size(); ++i)
        if (g_drops[i].id == id) {
            Drop d = g_drops[i];
            if (d.type == D_GUN && d.owner == by && NowMs() - d.born < 2000) return;  // just put down by that one
            DelObj(g_drops[i].handle);
            g_drops.erase(g_drops.begin() + (long)i);
            Send(A_TAKEN, [&](Writer& w) {
                w.U8(id);
                w.U8(by);
            });
            ApplyDrop(d, by);
            return;
        }
}
void UpdateDrops(uint64_t now) {
    CPlayerPed* me = FindPlayerPed(0);
    for (size_t i = 0; i < g_drops.size();) {
        Drop& d = g_drops[i];
        if (now > d.until) {
            DelObj(d.handle);
            g_drops.erase(g_drops.begin() + (long)i);
            continue;
        }
        if (d.handle >= 0 && GetObject(d.handle)) {
            CVector at = d.pos + CVector{0, 0, 0.15f * std::sin((float)(now % 3000) / 3000.0f * 6.2831853f)};
            Cmd(0x01BC).I(d.handle).F(at.x).F(at.y).F(at.z).Run();
            Cmd(0x0177).I(d.handle).F((float)(now % 3000) * 0.12f).Run();
        }
        bool ownJustNow = d.type == D_GUN && d.owner == Net().MyId() && now - d.born < 2000;
        bool askFirst = d.type == D_GUN && SlotTaken(me, d.weapon);  // another weapon of its kind in hand: E decides
        if (me && me->Health() > 0.0f && !d.asked && !ownJustNow && !askFirst && (me->GetPosition() - d.pos).Length() < 1.5f) {
            d.asked = true;
            if (g_arena.host) {
                TakeDrop(d.id, Net().MyId());
                continue;
            }
            uint8_t id = d.id;
            Send(A_TAKE, [id](Writer& w) { w.U8(id); });
        }
        ++i;
    }
}
// J: the weapon in hand goes down on the floor in front of the player with all its ammo, for a team mate to take
void PutDownGun() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || me->Health() <= 0.0f || me->InVehicle()) return;
    CWeapon& w = me->ActiveWeapon();
    if (w.type <= 0 || w.type > 38 || w.type == 46) return;  // fists, a parachute
    uint8_t weapon = (uint8_t)w.type;
    uint16_t ammo = (uint16_t)std::clamp(w.ammoTotal, 1, 65000);
    CVector f = cam::Front();
    f.z = 0;
    float l = std::max(0.01f, f.Length());
    CVector at = me->GetPosition() + f * (1.2f / l) - CVector{0, 0, 0.6f};
    uint8_t special = weapon == 30 && g_hasUgl ? 1 : 0;
    if (special) g_hasUgl = false;  // the launcher goes with the AK
    Cmd(0x0555).I(GetPedRef(me)).I(weapon).Run();  // REMOVE_WEAPON_FROM_CHAR
    if (g_arena.host) SpawnDrop(D_GUN, at, weapon, ammo, Net().MyId(), special);
    else Send(A_GIVE, [&](Writer& wr) {
        wr.Vec({at.x, at.y, at.z});
        wr.U8(weapon);
        wr.U16(ammo);
        wr.U8(special);
    });
}
void ClearDrops() {
    for (Drop& d : g_drops) DelObj(d.handle);
    g_drops.clear();
}

// ---- barricades the players build: H with 300 (B is the voice) points puts one up in front of the player (12 in all at most)
constexpr int kBuildCost = 300, kMaxBars = 12;
void BuildBar(const CVector& at, float heading, uint8_t by) {  // host
    if ((int)g_bars.size() >= kMaxBars) return;
    Bar b;
    b.pos = at;
    b.spawn = at;
    b.heading = heading;
    b.built = true;
    g_bars.push_back(b);
    ShowBar(g_bars.back());
    SendBars();
    LOGI("Arena: player %d built a barricade", by);
}
bool TryBuild() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || me->Health() <= 0.0f || me->InVehicle() || me->AreaCode() != kPlaces[g_arena.place].area || g_arena.endAt) return false;
    if ((int)g_bars.size() >= kMaxBars) {
        ui::TheChat().AddError(TR("Баррикад уже слишком много.", "There are too many barricades already."));
        return true;
    }
    if (g_points < kBuildCost) {
        ui::TheChat().AddError(Format(TR("Нужно %d очков.", "You need %d points."), kBuildCost));
        return true;
    }
    CVector f = cam::Front();
    f.z = 0;
    float l = f.Length();
    f = l > 0.01f ? f * (1.0f / l) : CVector{0, 1, 0};
    CVector front = me->GetPosition() + f * 1.6f;
    float z = 0;
    if (!FloorBelow(front.x, front.y, front.z + 0.5f, z) || std::fabs(z - (me->GetPosition().z - 1.0f)) > 1.0f ||
        !Clear(me->GetPosition(), front)) {
        ui::TheChat().AddError(TR("Здесь баррикаду не поставить.", "No barricade can go here."));
        return true;
    }
    CVector at{front.x, front.y, z};
    float heading = std::atan2(-f.x, f.y) * 57.29578f;
    AddPoints(-kBuildCost);
    if (g_arena.host) BuildBar(at, heading, Net().MyId());
    else Send(A_BUILD, [&](Writer& w) {
        w.Vec({at.x, at.y, at.z});
        w.F32(heading);
    });
    return true;
}

// ---- the host's own spots for a place (coop\arena-spots.ini, a section a place): /arena ящик, /arena баррикада,
// /arena стена put the box, a barricade, a wall gun where the host stands; the next arenas there use them
std::wstring SpotsPath() { return Cfg().coopDir + L"arena-spots.ini"; }
std::string PlaceSection() { return kPlaces[g_arena.place].keys[0][1]; }  // its English word
bool ReadVec(const std::string& v, CVector& p, float& h, int* extra = nullptr) {
    int e = 0;
    int n = extra ? std::sscanf(v.c_str(), "%d %f %f %f %f", &e, &p.x, &p.y, &p.z, &h) - 1
                  : std::sscanf(v.c_str(), "%f %f %f %f", &p.x, &p.y, &p.z, &h);
    if (extra) *extra = e;
    return n == 4;
}
// host: the saved spots instead of the mod's own; true when there were some
bool LoadSpots() {
    Ini ini;
    if (!ini.Load(SpotsPath())) return false;
    std::string sec = PlaceSection();
    bool any = false;
    CVector p;
    float h = 0;
    if (ReadVec(ini.Get(sec, "box"), p, h)) {
        g_box.placed = true, g_box.pos = p, g_box.heading = h;
        ShowBox();
        SendBox();
        any = true;
    }
    std::vector<Bar> bars;
    for (int k = 1; k <= kMaxBars; ++k) {
        if (!ReadVec(ini.Get(sec, Format("bar%d", k)), p, h)) continue;
        Bar b;
        b.pos = p;
        b.heading = h;
        float r = h / 57.29578f;
        b.spawn = p - CVector{-std::sin(r), std::cos(r), 0.0f} * 2.5f;  // behind it, against the way it faces
        bars.push_back(b);
    }
    if (!bars.empty()) {
        ClearBars();
        g_bars = bars;
        for (Bar& b : g_bars) ShowBar(b);
        SendBars();
        any = true;
    }
    for (int k = 1; k <= 8; ++k) {
        int gun = 0;
        if (!ReadVec(ini.Get(sec, Format("wall%d", k)), p, h, &gun)) continue;
        Wall w;
        w.gun = std::clamp(gun, 0, kWallGunCount - 1);
        w.pos = p + CVector{0, 0, 1.3f};
        w.stand = p;
        float r = h / 57.29578f;
        w.pos = w.pos + CVector{-std::sin(r), std::cos(r), 0.0f} * 0.8f;  // a little ahead of where the host stood
        w.heading = h;
        g_walls.push_back(w);
        any = true;
    }
    return any;
}
void SaveSpot(const std::string& what) {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me) return;
    Ini ini;
    ini.Load(SpotsPath());
    std::string sec = PlaceSection();
    CVector p = me->GetPosition() - CVector{0, 0, 1.0f};
    float h = me->CurrentRotation() * 57.29578f;
    std::string line = Format("%.2f %.2f %.2f %.1f", p.x, p.y, p.z, h);
    if (what == "box") {
        ini.Set(sec, "box", line);
        g_box.placed = true, g_box.pos = p, g_box.heading = h;
        ShowBox();
        SendBox();
        ui::TheChat().AddSystem(TR("Ящик теперь здесь (сохранено для этого места).", "The box is here now (kept for this place)."));
    } else if (what == "bar") {
        int k = 1;
        while (k <= kMaxBars && !ini.Get(sec, Format("bar%d", k)).empty()) ++k;
        if (k > kMaxBars) {
            ui::TheChat().AddError(TR("Уже 12 баррикад для этого места.", "12 barricades for this place already."));
            return;
        }
        ini.Set(sec, Format("bar%d", k), line);
        ui::TheChat().AddSystem(Format(TR("Баррикада %d сохранена: враги придут из-за вашей спины. Появится со следующей арены.",
                                          "Barricade %d kept: the enemies come from behind your back. It is there from the next arena on."), k));
    } else if (what == "wall") {
        ui::TheChat().AddError(TR("Оружия на стенах больше нет.", "There are no wall guns any more."));
        return;
    } else if (what == "reset") {
        for (const char* key : {"box"}) ini.Set(sec, key, "");
        for (int k = 1; k <= kMaxBars; ++k) ini.Set(sec, Format("bar%d", k), "");
        for (int k = 1; k <= 8; ++k) ini.Set(sec, Format("wall%d", k), "");
        ui::TheChat().AddSystem(TR("Свои места для этого места забыты: мод снова расставит всё сам.",
                                   "The spots of this place are forgotten: the mod sets everything out itself again."));
    }
    ini.Save(SpotsPath());
}

// ---- E near a barricade or the box
enum NearKind : uint8_t { N_NONE, N_BAR, N_BOX, N_WALL, N_WARD, N_GUN };
NearKind g_nearKind = N_NONE;
int g_nearIdx = -1;
void FindNear() {
    g_nearKind = N_NONE;
    g_nearIdx = -1;
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || me->Health() <= 0.0f || me->InVehicle() || me->AreaCode() != kPlaces[g_arena.place].area || g_arena.endAt) return;
    CVector at = me->GetPosition() - CVector{0, 0, 1.0f};
    float best = 2.4f;
    for (size_t i = 0; i < g_bars.size(); ++i) {
        CVector d = g_bars[i].pos - at;
        if (g_bars[i].boards >= kBoards || std::fabs(d.z) > 2.0f) continue;
        if (d.Length2D() < best) best = d.Length2D(), g_nearKind = N_BAR, g_nearIdx = (int)i;
    }
    for (size_t i = 0; i < g_walls.size(); ++i) {
        CVector d = g_walls[i].pos - me->GetPosition();
        if (d.Length2D() < 1.8f && std::fabs(d.z) < 1.5f && d.Length2D() < best) best = d.Length2D(), g_nearKind = N_WALL, g_nearIdx = (int)i;
    }
    if (g_box.placed && g_box.handle >= 0 && !g_box.moveAt) {
        CVector d = g_box.pos - at;
        if (d.Length2D() < 2.8f && std::fabs(d.z) < 2.0f && d.Length2D() < best) best = d.Length2D(), g_nearKind = N_BOX, g_nearIdx = 0;
    }
    if (g_ward.handle >= 0) {
        CVector d = g_ward.pos - at;
        if (d.Length2D() < 2.2f && std::fabs(d.z) < 2.0f && d.Length2D() < best) best = d.Length2D(), g_nearKind = N_WARD, g_nearIdx = 0;
    }
    for (size_t i = 0; i < g_drops.size(); ++i) {  // a team mate's weapon that would take the place of one this player holds
        const Drop& dr = g_drops[i];
        if (dr.type != D_GUN || !SlotTaken(me, dr.weapon)) continue;
        CVector d = dr.pos - me->GetPosition();
        if (d.Length() < 1.6f && d.Length() < best) best = d.Length(), g_nearKind = N_GUN, g_nearIdx = (int)i;
    }
}

// back in the fight at a new wave: a player who died meanwhile (the hospital) comes back with a pistol
uint64_t g_aliveSince = 0;  // this player on its feet since (after a death: the hospital's respawn done)
int g_deadWave = 0;         // the wave it died in: it comes back at a later one
// ---- the spectator: one who fell in the arena (no partner to revive it, or it bled out) does not die and go to a
// hospital: it stays in the place unseen (invisible, no collision, every proof, nobody targets it, frozen, no control),
// its camera on a team mate (the left mouse button: the next one), and at the next wave it is back by a team mate's
// side with a pistol and full health. Its game keeps its own people there all the while (the host's enemies too).
uint64_t g_specClickMs = 0, g_specMoveMs = 0;
CPed* g_specCamOn = nullptr;
void Spectate() {
    CPlayerPed* me = FindPlayerPed(0);
    if (g_spectating || !g_arena.taken || !me) return;
    int h = GetPedRef(me);
    g_spectating = true;
    g_specTarget = 0xFF;
    g_specCamOn = nullptr;
    Cmd(0x0792).I(h).Run();  // CLEAR_CHAR_TASKS_IMMEDIATELY
    me->Health() = std::max(me->Health(), 100.0f);
    me->SetProofs(true, true, true, true, true);
    me->PedFlags2() |= 1u << 28;          // bNeverEverTargetThisPed
    Cmd(0x0337).I(h).I(0).Run();          // SET_CHAR_VISIBLE off
    Cmd(0x0619).I(h).I(0).Run();          // SET_CHAR_COLLISION off
    Cmd(0x04D7).I(h).I(1).Run();          // FREEZE_CHAR_POSITION
    Cmd(0x01B4).I(0).I(0).Run();          // SET_PLAYER_CONTROL off
    g_hasUgl = g_uglLoaded = false;
    ++g_falls;
    AddPoints(-g_points / 10);
    g_spectators.insert(Net().MyId());
    Send(A_SPEC, [](Writer& w) { w.U8(1); });
    ui::TheChat().AddSystem(TR("Вы выбыли до следующей волны. [ЛКМ] Другой игрок.", "Out till the next wave. [LMB] Next player."));
    LOGI("Arena: spectating");
}
void EndSpectate(bool back) {
    if (!g_spectating) return;
    g_spectating = false;
    g_spectators.erase(Net().MyId());
    Send(A_SPEC, [](Writer& w) { w.U8(0); });
    CPlayerPed* me = FindPlayerPed(0);
    Cmd(0x02EB).Run();  // RESTORE_CAMERA_JUMPCUT
    g_specCamOn = nullptr;
    if (!me) return;
    int h = GetPedRef(me);
    Cmd(0x04D7).I(h).I(0).Run();
    Cmd(0x0619).I(h).I(1).Run();
    Cmd(0x0337).I(h).I(1).Run();
    Cmd(0x01B4).I(0).I(1).Run();
    me->SetProofs(false, false, false, false, false);
    me->PedFlags2() &= ~(1u << 28);
    if (!back) return;
    me->Health() = std::max<float>(PlayerInfos()[0].maxHealth, 100.0f);
    std::vector<CVector> spots = PlayerSpots();
    const Place& pl = kPlaces[g_arena.place];
    CVector at = spots.empty() ? pl.pos : spots[0] + CVector{0.8f, 0.6f, 0};
    Teleport(pl.area, at, Rand(0.0f, 360.0f));
    Give(h, WEAPON_PISTOL, 68);
    Cmd(0x01B9).I(h).I(WEAPON_PISTOL).Run();
    g_dead = false;
    ui::TheChat().AddSystem(TR("Вы снова в бою.", "You are back in the fight."));
}
// every frame while spectating: the camera on a live team mate (the next one on a click), the hidden body with it
void SpectateFrame(uint64_t now) {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me) return;
    std::vector<std::pair<uint8_t, CPed*>> live;
    int area = kPlaces[g_arena.place].area;
    for (auto& [id, rp] : Players::Instance().All())
        if (rp.ped && rp.hasState && !Spectator(id) && !(rp.state.flags & OnFootState::F_DEAD) && rp.state.area == area)
            live.push_back({id, rp.ped});
    if (live.empty()) return;
    bool click = IsForeground() && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) && now - g_specClickMs > 400;
    size_t cur = 0;
    for (size_t i = 0; i < live.size(); ++i)
        if (live[i].first == g_specTarget) cur = i;
    if (click) g_specClickMs = now, cur = (cur + 1) % live.size();
    g_specTarget = live[cur].first;
    CPed* t = live[cur].second;
    if (t != g_specCamOn) {
        g_specCamOn = t;
        Cmd(0x0159).I(GetPedRef(t)).I(4).I(2).Run();  // CAMERA_ON_PED: behind it, a jump cut
    }
    if (now - g_specMoveMs > 500) {  // the hidden body with it (the world streams round it, its game shares its people)
        g_specMoveMs = now;
        SetEntityPosition(me, t->GetPosition() - CVector{0, 0, 6.0f});  // under the floor: its copy in the other games is out of sight too
    }
}

void Rejoin() {
    if (g_spectating) {
        EndSpectate(true);
        return;
    }
    CPlayerPed* me = FindPlayerPed(0);
    if (!g_arena.taken || !me || me->Health() <= 0.0f || PlayerInfos()[0].playerState != 0) return;
    if (!g_aliveSince || NowMs() - g_aliveSince < 3000 || Ref<uint8_t>(0xB5F851) || cam::WideScreenOn()) return;  // not mid-respawn
    const Place& pl = kPlaces[g_arena.place];
    if (me->AreaCode() == pl.area) {
        g_dead = false;
        return;
    }
    std::vector<CVector> spots = PlayerSpots();
    CVector at = spots.empty() ? pl.pos : spots[0] + CVector{1.0f, 0, 0};
    Teleport(pl.area, at, Rand(0.0f, 360.0f));
    int h = GetPedRef(me);
    Give(h, WEAPON_PISTOL, 68);
    Cmd(0x01B9).I(h).I(WEAPON_PISTOL).Run();
    g_dead = false;
    ui::TheChat().AddSystem(TR("Вы вернулись в бой.", "You are back in the fight."));
}

void ResetCod() {
    ClearBars();
    g_box.placed = false;
    DelObj(g_box.handle);
    DelObj(g_box.handle2);
    DelObj(g_ward.handle);
    g_box = BoxState{};
    DelObj(g_spinHandle);
    g_spinModel = -1;
    ClearWalls();
    ClearDrops();
    g_points = kStartPoints;
    g_kills = g_falls = 0;
    g_scores.clear();
    g_popups.clear();
    g_hitMs.clear();
    g_killed.clear();
    g_placed = false;
    g_dead = false;
}

// ---- the Twins: Otto with two sawn-offs, Ernando with two "eagles" (two pistols at the pro skill: the game's own dual
// pistols; the model in his hands is the desert eagle's, put on him each frame: CPed::AddWeaponModel, m_nWeaponModelId
// +0x740), each shot as hard as an eagle's (IncomingDamage)
void EagleModel(CPed* p) {
    if (!p || !IsPedValid(p) || p->Health() <= 0.0f || p->ActiveWeapon().type != WEAPON_PISTOL) return;
    if (Field<int32_t>(p, 0x740) == 348) return;
    if (!IsModelLoaded(348) && !LoadModelNow(348)) return;
    CallMethod<0x5E5ED0>(p, 348);  // CPed::AddWeaponModel
}
const char* TwinName(int k) { return k == 0 ? TR("Отто", "Otto") : TR("Эрнандо", "Ernando"); }

// ---- the flamethrower man's body armour: the armour's model worn on its chest, every frame put on its upper torso's
// bone (CPed::m_apBones +0x488, the skin's world matrices: as CTaskSimpleHoldEntity holds a thing), upright and turned
// with it, so it moves with the body as Big Smoke's vest in the last mission. Every game puts it on its own ped or copy.
struct Vest {
    CPed* ped = nullptr;
    int obj = -1;
};
std::vector<Vest> g_vests;
std::vector<uint16_t> g_vestIds;  // a partner's: the flamethrower men whose copies are not here yet
uint64_t g_vestIdsUntil = 0;
void AddVest(CPed* p) {
    return;  // no vest to see any more (its armour is still there)
    for (const Vest& v : g_vests)
        if (v.ped == p) return;
    if (!IsObjectModel(1242) || !LoadModelNow(1242) || !HasRoom(ObjectPool(), kKeepObjects, "object")) return;
    int o = -1;
    CVector at = p->GetPosition();
    Cmd(0x0107).I(1242).F(at.x).F(at.y).F(at.z).Out(&o).Run();  // CREATE_OBJECT: the body armour
    if (o < 0) return;
    Cmd(0x0382).I(o).I(0).Run();  // SET_OBJECT_COLLISION off
    Cmd(0x0392).I(o).I(0).Run();
    Cmd(0x0566).I(o).I(kPlaces[g_arena.place].area).Run();
    g_vests.push_back({p, o});
}
void ClearVests() {
    for (Vest& v : g_vests)
        if (v.obj >= 0 && GetObject(v.obj)) Cmd(0x0108).I(v.obj).Run();
    g_vests.clear();
    g_vestIds.clear();
}
void UpdateVests() {
    for (auto it = g_vestIds.begin(); it != g_vestIds.end();) {
        int h = Peds::Instance().HandleOf(*it);
        if (h >= 0 && GetPed(h)) AddVest(GetPed(h)), it = g_vestIds.erase(it);
        else ++it;
    }
    if (NowMs() > g_vestIdsUntil) g_vestIds.clear();
    for (auto it = g_vests.begin(); it != g_vests.end();) {
        CObject* o = it->obj >= 0 ? GetObject(it->obj) : nullptr;
        CPed* p = it->ped;
        if (!o || !IsPedValid(p) || p->Health() <= 0.0f) {
            if (o) Cmd(0x0108).I(it->obj).Run();  // DELETE_OBJECT: the vest goes with its man
            it = g_vests.erase(it);
            continue;
        }
        auto* clump = (uint8_t*)p->RwObject();
        CMatrix *m = o->Matrix(), *pm = p->Matrix();
        void* node = Field<void*>(p, 0x488 + 1 * 4);  // PED_NODE_UPPER_TORSO
        void* hier = clump && clump[0] == 2 ? Call<0x734A40, void*>(clump) : nullptr;  // GetAnimHierarchyFromSkinClump
        int i = node && hier ? Call<0x7C51A0, int>(hier, Field<int32_t>(node, 0x14)) : -1;  // RpHAnimIDGetIndex
        auto* bones = hier ? Call<0x7C5120, uint8_t*>(hier) : nullptr;                     // RpHAnimHierarchyGetMatrixArray
        if (m && pm && i >= 0 && bones) {
            const CMatrix& bm = *(const CMatrix*)(bones + (size_t)i * 0x40);
            WorldRemove(o);
            m->right = pm->right;
            m->forward = pm->forward;
            m->up = pm->up;
            m->pos = bm.pos + pm->forward * 0.04f - pm->up * 0.12f;  // over the chest and the belly
            CallMethod<0x446F90>(o);  // CEntity::UpdateRW
            CallMethod<0x532B00>(o);  // CEntity::UpdateRwFrame
            WorldAdd(o);
        }
        ++it;
    }
}

// the new kinds of enemies (armed men of the gang or the force, as the others)
// A medic (Rifa, Ballas, the police, SWAT): heals by 20% every one of its own within 5 m, runs to the fallen and the
// hurt first, and gets up to 5 of the fallen on their feet again (CPR). The police's and SWAT's are paramedics.
void MakeMedic(int wave) {
    const Faction& f = kFactions[g_arena.faction];
    g_squads.push_back(Squad{});
    bool state = f.models[0] == 280 || f.models[0] == 285;
    int model = state ? 274 + RandInt(0, 2) : f.models[(size_t)RandInt(0, 3)];  // LAEMT1, LVEMT1, SFEMT1
    int weapon = state ? 29 : WEAPON_PISTOL;
    MakeEnemy(model, weapon, 100 + 15 * wave, ArmourFor(wave, 0), 45, SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 6.0f),
              (int)g_squads.size() - 1, R_MEDIC, false);
}
// The flamethrower man (Bikers, Vagos): 700 health and armour, slow, comes unannounced, a flamethrower close by and molotovs from afar
void MakeFlamer(int wave) {
    const Faction& f = kFactions[g_arena.faction];
    g_squads.push_back(Squad{});
    int model = f.models[3];
    int h = MakeEnemy(model, WEAPON_FLAMETHROWER, 700, 100, 60, SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 8.0f),
                      (int)g_squads.size() - 1, R_FLAMER, true);
    if (CPed* p = h >= 0 ? GetPed(h) : nullptr) {
        g_flamers.push_back(p);
        AddVest(p);
        if (NetPed* np = Peds::Instance().Find(p)) {  // the other games put one on their copy of it
            uint16_t id = np->netId;
            Send(A_FLAMER, [id](Writer& w) { w.U16(id); });
        }
    }
}
// The Twins, the mini-boss: two men of different looks, 250 health each; one with two sawn-offs, the other with two
// desert eagles (both held in both hands: the weapon skill "pro", TwinEagles)
void MakeTwins(int wave) {
    const Faction& f = kFactions[g_arena.faction];
    g_squads.push_back(Squad{});
    int squad = (int)g_squads.size() - 1;
    int m0 = f.models[0], m1 = f.models[0];
    for (int m : f.models)
        if (m != m0) m1 = m;
    if (m1 == m0) m1 = m0 == 287 ? 285 : 287;  // the army has one look: the other twin is SWAT-clad
    float dir = Rand(0.0f, 6.2831853f);
    CVector at = SpawnSpot(dir, 1.0f, 8.0f);
    int h0 = MakeEnemy(m0, WEAPON_SAWNOFF, 250, 0, 75, at, squad, R_TWIN, false);
    int h1 = MakeEnemy(m1, WEAPON_PISTOL, 250, 0, 80, MemberSpot(at, 1), squad, R_TWIN, false);  // eagles (TwinEagles)
    g_twinPeds[0] = h0 >= 0 ? GetPed(h0) : nullptr;
    g_twinPeds[1] = h1 >= 0 ? GetPed(h1) : nullptr;
    // the other games' copies hold them the same way: their weapon skill there too
    Send(A_TWINS, [](Writer& w) {
        for (CPed* p : g_twinPeds) {
            NetPed* np = p ? Peds::Instance().Find(p) : nullptr;
            w.U16(np ? np->netId : 0);
        }
    });
    ui::TheChat().AddSystem(TR("Мини-босс: Близнецы!", "A mini-boss: the Twins!"));
}

void SendTag(int kind, CPed* p) {  // the other games: which copy is the Butcher (0) or the Officer (1)
    NetPed* np = p ? Peds::Instance().Find(p) : nullptr;
    if (!np) return;
    uint16_t id = np->netId;
    Send(A_BOSSNAME, [&](Writer& w) {
        w.U8((uint8_t)kind);
        w.U16(id);
    });
}
// The Butcher: a chainsaw, 700 health; winds it up, then charges straight on at great speed till a wall stops him
// (stunned 4 s there); whoever he runs into falls wounded, and he goes on through them. With 100 health or less he
// bounces from wall to wall instead (Dead by Daylight's Blight), up to 8 times a charge.
void MakeSaw(int wave) {
    const Faction& f = kFactions[g_arena.faction];
    g_squads.push_back(Squad{});
    int h = MakeEnemy(f.models[2], 9 /* chainsaw */, 700, 0, 70, SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 8.0f), (int)g_squads.size() - 1,
                      R_SAW, true);
    g_sawPed = h >= 0 ? GetPed(h) : nullptr;
    if (g_sawPed) {
        g_info[g_sawPed].nextChargeMs = NowMs() + 4000;
        SendTag(0, g_sawPed);
    }
    ui::TheChat().AddSystem(TR("Мини-босс: Мясник!", "A mini-boss: the Butcher!"));
}
// The Officer: a katana, 600 health and armour; keeps to the far back of the place (the players must go to him), calls 4
// men to him every 30 s, falls back when hurt and a player is on him, and heals while nobody hurts him
void MakeOfficer(int wave) {
    const Faction& f = kFactions[g_arena.faction];
    g_squads.push_back(Squad{});
    std::vector<CVector> players = PlayerSpots();
    CVector at;
    float best = -1;
    if (GridUsable())
        for (int k : StandCells()) {
            CVector c = g_cells[(size_t)k].p + CVector{0, 0, 1.0f};
            float dp = DistToPlayers(c, players) + Rand(0.0f, 3.0f);
            if (dp > best) best = dp, at = c;
        }
    if (best < 0) at = SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 8.0f);
    int h = MakeEnemy(f.models[0], 8 /* katana */, 600, 100, 80, at, (int)g_squads.size() - 1, R_OFFICER, false);
    g_officerPed = h >= 0 ? GetPed(h) : nullptr;
    if (g_officerPed) {
        Enemy& e = g_info[g_officerPed];
        e.home = at - CVector{0, 0, 1.0f};
        e.nextCallMs = NowMs() + 10000;
        e.lastHealth = 600;
        e.maxHealth = 600;
        Cmd(0x0350).I(h).I(1).Run();  // SET_CHAR_STAY_IN_SAME_PLACE: he waits there
        SendTag(1, g_officerPed);
    }
    ui::TheChat().AddSystem(TR("Мини-босс: Офицер в тылу!", "A mini-boss: the Officer, at the back!"));
}

void SpawnWave() {
    ReleaseDead();
    ++g_arena.wave;
    g_bossPed = nullptr;
    g_twinPeds[0] = g_twinPeds[1] = nullptr;
    g_flamers.clear();
    g_killed.clear();
    g_lastSpawns = g_spawnsNow;
    g_spawnsNow.clear();
    Rejoin();
    OutfitWaveStart();
    SendBars();
    int w = g_arena.wave;
    const Faction& fac = kFactions[g_arena.faction];
    g_sawPed = g_officerPed = nullptr;
    // the 10% event (from the 2nd wave, not with a boss or a mini-boss): four flamethrower men, nobody else
    g_fireWave = w >= 2 && !BossWave(w) && !MiniBoss(w) && RandInt(0, 99) < 10;
    if (g_fireWave) {
        for (int k = 0; k < 4; ++k) MakeFlamer(w);
        g_arena.waveMs = NowMs();
        int n = (int)g_enemies.size(), wave = g_arena.wave;
        Send(A_WAVE, [&](Writer& wr) {
            wr.U8((uint8_t)wave);
            wr.U8((uint8_t)n);
        });
        LOGI("Arena: wave %d, the fire event: %d flamethrower men", wave, n);
        return;
    }
    int count = std::min(14, 2 + w * 2 + PlayersIn());
    // squads of 3-4, spread evenly round the players; the first comes straight on, the others go round; the knifemen
    // (1 from 6 enemies, 2 from 11) alone, from elsewhere
    int knives = count >= 11 ? 2 : count >= 6 ? 1 : 0;
    count -= knives;
    int squads = std::max(1, (count + 3) / 4);
    float base = Rand(0.0f, 6.2831853f), half = 3.14159265f / (float)squads;
    const float around[] = {180.0f, 135.0f, 225.0f, 90.0f, 270.0f};
    for (int i = 0; i < squads; ++i) {
        int size = count / squads + (i < count % squads ? 1 : 0);  // 3-4 each (fewer only in a small first wave)
        float dir = base + 6.2831853f * (float)i / (float)squads;
        MakeSquad(dir, half, i > 0, around[(size_t)(i - 1 + 5) % 5], size, w, false);
    }
    for (int k = 0; k < knives; ++k) MakeKnifeman(w);
    // medics from the 2nd wave (two with many enemies), the flamethrower man from the 2nd (two in a big place later on)
    if (fac.medics && w >= 2) {
        int medics = count >= 10 ? 2 : 1;
        for (int k = 0; k < medics; ++k) MakeMedic(w);
    }
    if (fac.flamers && w >= 3 && RandInt(0, 99) < 35) MakeFlamer(w);  // now and then one, from the 3rd wave
    if (TwinsWave(w)) MakeTwins(w);
    if (MiniBoss(w) == 2) MakeSaw(w);
    if (MiniBoss(w) == 3) MakeOfficer(w);
    // snipers far off (1, 2 in a big place, one more with three players or more)
    if (SniperWave(w)) {
        int snipers = (kPlaces[g_arena.place].big ? 2 : 1) + (PlayersIn() >= 3 ? 1 : 0);
        for (int k = 0; k < snipers; ++k) MakeSniper(w);
    }
    std::string bossLine;
    if (BossWave(w)) {
        int b = PickBoss(g_arena.faction);
        Send(A_BOSS, [b](Writer& wr) { wr.U8((uint8_t)b); });
        LoadBoss(b);
        bool special = IsModelLoaded(kBossModel);
        int model = special ? kBossModel : fac.models[0];
        g_squads.push_back(Squad{});
        int bossSquad = (int)g_squads.size() - 1;
        // as tough as the SWAT were in 0.4.1: 100 health, 100 armour, no one-shot headshot
        int h = MakeEnemy(model, kBosses[b].weapon, 100, 100, 85, SpawnSpot(base + 3.14159f, 1.0f, 8.0f), bossSquad, R_BOSS, false);
        if (CPed* p = h >= 0 ? GetPed(h) : nullptr) g_bossPed = p;
        // his men: once, unannounced, silent, round to the players' backs
        int men = std::min(6, 3 + PlayersIn() - 1);
        MakeSquad(base + 3.14159f + 1.2f, 0.8f, true, 150.0f, std::min(4, men), w, true);
        if (men > 4) MakeSquad(base + 3.14159f - 1.2f, 0.8f, true, 210.0f, men - 4, w, true);
        g_arena.boss = b;
        bossLine = BossName(b);
    }
    g_arena.waveMs = NowMs();
    int n = (int)g_enemies.size(), wave = g_arena.wave;
    Send(A_WAVE, [&](Writer& wr) {
        wr.U8((uint8_t)wave);
        wr.U8((uint8_t)n);
    });
    if (!bossLine.empty())
        ui::TheChat().AddSystem(Format(TR("Волна с боссом! Босс: %s.", "The boss wave! The boss: %s."), bossLine.c_str()));
    LOGI("Arena: wave %d, %d enemies in %d squads", wave, n, (int)g_squads.size());
}

// ---- grenades
// The game's people do not throw a grenade by themselves in a scripted fight, so the grenadier throws it here: he
// turns to the player, plays the throw (GRENADE.ifp, WEAPON_throw), and as the arm comes over the grenade leaves his
// hand (CProjectileInfo::AddProjectile in his name) on an arc that comes down where the player will be, leading a
// moving player, gravity as the game's (0.008 a frame squared: 20 m/s/s). He sees the arc first: of the flights
// 0.8-1.9 s (higher lobs for the longer ones) the first whose arc meets nothing on the way (a ceiling, a wall, a
// doorframe) till it comes down by the player is taken; none: no throw (it bounced back at his feet).
constexpr uintptr_t kProjectileInfos = 0xC891A8;  // gaProjectileInfo[32], 0x24 bytes, m_bActive at +0x10
constexpr uintptr_t kProjectiles = 0xC89110;      // CProjectileInfo::ms_apProjectile[32]
constexpr float kGravity = 20.0f;
uint32_t ActiveProjectiles() {
    uint32_t mask = 0;
    for (int i = 0; i < 32; ++i)
        if (Ref<uint8_t>(kProjectileInfos + (uintptr_t)i * 0x24 + 0x10)) mask |= 1u << i;
    return mask;
}

bool EnsureBlock(const char* block) {
    int idx = Call<0x4D3990, int>(block);  // CAnimManager::GetAnimationBlockIndex
    if (idx < 0) return false;
    uintptr_t b = 0xB5D4A0 + (uintptr_t)idx * 0x20;  // CAnimManager::ms_aAnimBlocks
    if (Ref<uint8_t>(b + 0x10)) return true;
    RequestModel(25575 + idx, 2);
    LoadAllRequestedModels(false);
    return Ref<uint8_t>(b + 0x10) != 0;
}

CVector ThrowFrom(CPed* p, const CVector& at) {  // the hand as the arm comes over, towards `at`
    CVector to = at - p->GetPosition();
    float len = std::max(0.1f, to.Length2D());
    return p->GetPosition() + CVector{to.x / len, to.y / len, 0.0f} * 0.5f + CVector{0, 0, 0.75f};
}
CVector ThrowVel(const CVector& from, const CVector& aim, float flight) {
    return {(aim.x - from.x) / flight, (aim.y - from.y) / flight, (aim.z - from.z + 0.5f * kGravity * flight * flight) / flight};
}
// the arc reaches the player: nothing in the way before it comes down within 3 m of it
bool ArcClear(const CVector& from, CVector vel, float flight, const CVector& aim) {
    CVector pos = from;
    constexpr float dt = 0.05f;
    for (float t = 0; t < flight + 0.2f; t += dt) {
        CVector prev = pos;
        vel.z -= kGravity * dt;
        pos = pos + vel * dt;
        CVector hit;
        if (Ray(prev, pos, hit)) return (hit - aim).Length() < 3.0f && t > flight * 0.55f;
    }
    return true;
}
// the flight whose arc is clear, 0 when none
float ClearFlight(const CVector& from, const CVector& aim) {
    float dist = (aim - from).Length2D();
    for (float f : {0.8f, 1.0f, 1.25f, 1.5f, 1.9f}) {
        if (dist / f > 22.0f) continue;  // a throw that hard is no throw
        if (ArcClear(from, ThrowVel(from, aim, f), f, aim)) return f;
    }
    return 0.0f;
}

bool StartThrow(CPed* p, Enemy& e, CPed* t) {
    CVector aim = t->GetPosition() - CVector{0, 0, 0.8f};
    if ((aim - p->GetPosition()).Length2D() < 8.0f) return false;  // not at his own feet
    float flight = ClearFlight(ThrowFrom(p, aim), aim);
    if (flight <= 0.0f) return false;
    int h = GetPedRef(p);
    CVector to = t->GetPosition() - p->GetPosition();
    Cmd(0x0173).I(h).F(std::atan2(-to.x, to.y) * 57.29578f).Run();  // SET_CHAR_HEADING: at the player
    // the wind-up first (WEAPON_start_throw, held 0.8 s: the arm drawn back, the thing in the hand), then the swing
    if (e.throwWeapon == WEAPON_MOLOTOV) Cmd(0x01B9).I(h).I(WEAPON_MOLOTOV).Run();  // SET_CURRENT_CHAR_WEAPON: the bottle in hand
    if (EnsureBlock("GRENADE")) Cmd(0x0605).I(h).S("WEAPON_start_throw").S("GRENADE").F(8.0f).I(0).I(0).I(0).I(1).I(-1).Run();  // TASK_PLAY_ANIM, held
    e.swingAt = NowMs() + 800;
    e.throwAt = e.swingAt + 380;
    e.throwTarget = t;
    e.throwFlight = flight;
    e.grenadeUntil = NowMs() + 2100;
    --e.grenades;
    return true;
}

void Release(CPed* p, Enemy& e) {
    e.throwAt = 0;
    CPed* t = e.throwTarget;
    int weapon = e.throwWeapon == WEAPON_MOLOTOV ? WEAPON_MOLOTOV : WEAPON_GRENADE;
    if (!t || !IsPedValid(t) || !IsPedValid(p) || p->Health() <= 0.0f || !LoadModelNow(weapon == WEAPON_MOLOTOV ? 344 : 342)) return;
    CVector aim = t->GetPosition() - CVector{0, 0, 0.8f};  // at its feet, where it will be
    aim = aim + t->MoveSpeed() * 50.0f * e.throwFlight * 0.8f;
    CVector from = ThrowFrom(p, aim);
    float flight = ArcClear(from, ThrowVel(from, aim, e.throwFlight), e.throwFlight, aim) ? e.throwFlight : ClearFlight(from, aim);
    if (flight <= 0.0f) return;  // the player stepped behind something meanwhile: he keeps it
    CVector vel = ThrowVel(from, aim, flight), dir = vel.Normalized();
    uint32_t before = ActiveProjectiles();
    game::challenge::SetNextThrow(true, from, vel * (1.0f / 50.0f));  // metres a frame; Fire sets it (and sends it)
    bool ok = Call<0x737C80, bool>((CEntity*)p, weapon, from, 1.0f, &dir, (CEntity*)nullptr);  // AddProjectile
    CVector f, sp;
    if (game::challenge::NextThrow(f, sp)) {  // nobody took it (no Fire hook): set here
        uint32_t added = ActiveProjectiles() & ~before;
        for (int i = 0; i < 32; ++i)
            if (added & (1u << i))
                if (auto* obj = Ref<CPhysical*>(kProjectiles + (uintptr_t)i * 4)) obj->SetPositionRaw(f), obj->MoveSpeed() = sp;
    }
    if (ok) LOGD("Arena: a grenade thrown %.0f m, %.2f s", (aim - from).Length2D(), flight);
}

void Throws() {  // every frame: the swings that start now, the grenades that leave the hand now
    uint64_t now = NowMs();
    for (auto& [p, e] : g_info) {
        if (e.swingAt && now >= e.swingAt) {
            e.swingAt = 0;
            if (IsPedValid(p) && p->Health() > 0.0f && EnsureBlock("GRENADE")) {
                int h = GetPedRef(p);
                if (e.throwTarget && IsPedValid(e.throwTarget)) {
                    CVector to = e.throwTarget->GetPosition() - p->GetPosition();
                    Cmd(0x0173).I(h).F(std::atan2(-to.x, to.y) * 57.29578f).Run();  // still at the player
                }
                Cmd(0x0605).I(h).S("WEAPON_throw").S("GRENADE").F(8.0f).I(0).I(0).I(0).I(0).I(-1).Run();  // the swing
            }
        }
        if (e.gasAt && now >= e.gasAt) {
            e.gasAt = 0;
            if (IsPedValid(p) && p->Health() > 0.0f) {
                CVector from = ThrowFrom(p, e.gasAim), vel = ThrowVel(from, e.gasAim, e.gasFlight);
                Send(A_HEALGAS, [&](Writer& w) {
                    w.Vec({from.x, from.y, from.z});
                    w.Vec({vel.x, vel.y, vel.z});
                });
                AddGas(from, vel);
            }
        }
        if (e.throwAt && now >= e.throwAt) {
            Release(p, e);
            if (e.throwWeapon == WEAPON_MOLOTOV && IsPedValid(p) && e.weapon)
                Cmd(0x01B9).I(GetPedRef(p)).I(e.weapon).Run();  // the flamethrower back in hand
        }
    }
}

// ---- how they fight
// Every 0.5 s each one. They always know where the players are (sense range over the place, the floor's cells):
// one that does not see its player goes to it along the cells (to the farthest of the next few cells it sees straight,
// running), not by the game's own way-finding, which in these interiors walked into walls; in sight it fights. The
// ones going round turn to the fight when they are there (or close, hurt, 14 s on). In the fight one with the player
// in sight within 28 m now and then runs to cover (a cell of the floor within 9 m where, crouched, nothing of it shows
// to the player and, standing, it sees the player) and holds it some 5-10 s: down, then up shooting for 2 s, down
// again; then it pushes on at the player for a while, and later looks for the next cover. A squad pushes all together
// every 18-26 s. The grenadiers throw when the arc is clear. One that has not moved for 4 s while the player is out
// of its sight is stuck: it is set one step on along the floor's cells towards the player. The knifemen steal up to a
// back out of the player's sight (Sneak), the snipers stay far off (Snipe).
struct Paths {
    std::vector<int> steps;  // steps from every cell to the player's (-1: no way)
    int cell = -1;
    uint64_t ms = 0;
};
std::map<CPed*, Paths> g_paths;

const std::vector<int>& StepsFor(CPed* t) {
    Paths& pa = g_paths[t];
    uint64_t now = NowMs();
    if (pa.steps.size() != g_cells.size() || now - pa.ms > 1500) {
        int cell = CellNear(t->GetPosition());
        if (cell >= 0 || pa.steps.size() != g_cells.size()) {
            pa.cell = cell;
            pa.steps = StepsTo(cell);
        }
        pa.ms = now;
    }
    return pa.steps;
}

// the cell it stands on, or the nearest within 6 m that leads somewhere
int CellOf(CPed* p, const std::vector<int>* steps = nullptr) {
    int here = CellNear(p->GetPosition());
    if (here >= 0 && (!steps || (*steps)[(size_t)here] >= 0)) return here;
    int best = -1;
    float bestD = 6.0f;
    for (size_t k = 0; k < g_cells.size(); ++k) {
        float d = (g_cells[k].p + CVector{0, 0, 1.0f} - p->GetPosition()).Length();
        if ((!steps || (*steps)[k] >= 0) && d < bestD) bestD = d, best = (int)k;
    }
    return best;
}

// straight from a body to a floor cell: nothing at the knees or the chest
bool Straight(const CVector& body, const CVector& floor) {
    return Clear(body - CVector{0, 0, 0.4f}, floor + CVector{0, 0, 0.6f}) && Clear(body + CVector{0, 0, 0.4f}, floor + CVector{0, 0, 1.4f});
}

// the next spot on the way to the player: of the next 6 cells down the steps the farthest it sees straight
bool Waypoint(CPed* p, CPed* t, CVector& out) {
    if (!GridUsable()) return false;
    const std::vector<int>& steps = StepsFor(t);
    int cur = CellOf(p, &steps);
    if (cur < 0) return false;
    CVector from = p->GetPosition();
    int best = -1;
    for (int s = 0; s < 6; ++s) {
        int next = cur;
        for (int n : g_cells[(size_t)cur].next)
            if (steps[(size_t)n] >= 0 && steps[(size_t)n] < steps[(size_t)next]) next = n;
        if (next == cur) break;
        cur = next;
        if (!Straight(from, g_cells[(size_t)cur].p)) break;
        best = cur;
    }
    if (best < 0) best = cur;  // round a corner: the next one at least
    out = g_cells[(size_t)best].p;
    return (out - (from - CVector{0, 0, 1.0f})).Length2D() > 0.4f;
}

// where the player looks: its camera (this game's own, or the one its state carries)
CVector ViewOf(CPed* t) {
    CVector f;
    if (t == FindPlayerPed(0)) f = cam::Front();
    else if (const RemotePlayer* rp = Players::Instance().FindByPed(t); rp && rp->hasState) f = rp->state.cam.front;
    else f = {-std::sin(t->GetHeading()), std::cos(t->GetHeading()), 0.0f};
    f.z = 0;
    float l = f.Length();
    return l > 0.01f ? f * (1.0f / l) : CVector{0, 1, 0};
}

// the player would see a body there: within 50 m, within some 70 degrees of where it looks, nothing between
bool InView(CPed* t, const CVector& view, const CVector& body) {
    CVector eye = t->GetPosition() + CVector{0, 0, 0.6f}, d = body - eye;
    float l = d.Length2D();
    if (l > 50.0f) return false;
    if (l > 0.5f && (d.x * view.x + d.y * view.y) < l * 0.34f) return false;
    return Clear(eye, body + CVector{0, 0, 0.3f});
}

// The knifeman: up to the player's back by the cells it would not be seen on (Dijkstra: a cell in the player's view
// costs as much as eight out of it), to a cell 1.2-4 m behind it; from there the last steps (TASK_GOTO_CHAR_OFFSET
// behind it) and the knife (StealthKnife). Out of sight with the next step in sight it waits for the player to look
// away (up to 6 s). Seen close by, it goes for the player at once.
void Sneak(CPed* p, Enemy& e, CPed* t, float d, uint64_t now) {
    int h = GetPedRef(p);
    CVector view = ViewOf(t), body = p->GetPosition(), rel = body - t->GetPosition();
    bool seen = InView(t, view, body);
    bool behind = rel.x * view.x + rel.y * view.y < -0.2f * std::max(0.1f, rel.Length2D());
    if (d < 4.5f && (!seen || d < 2.5f)) {  // the last steps
        if (e.routing || now - e.setOnMs > 3000) {
            e.routing = false;
            e.setOnMs = now;
            if (behind || !seen) Cmd(0x06A8).I(h).I(GetPedRef(t)).I(5000).F(0.8f).F(180.0f).Run();  // TASK_GOTO_CHAR_OFFSET: behind it
            else Cmd(0x05E2).I(h).I(GetPedRef(t)).Run();                                            // TASK_KILL_CHAR_ON_FOOT
        }
        return;
    }
    if (seen && d < 6.0f) {  // seen close by: at it
        if (e.routing || now - e.setOnMs > 3000) {
            e.routing = false;
            e.setOnMs = now;
            Cmd(0x05E2).I(h).I(GetPedRef(t)).Run();
        }
        return;
    }
    if (now < e.evalMs || !GridUsable()) return;
    e.evalMs = now + 600;
    int start = CellOf(p);
    if (start < 0) {
        if (!e.routing) Cmd(0x06A8).I(h).I(GetPedRef(t)).I(5000).F(1.0f).F(180.0f).Run(), e.routing = true, e.routeMs = now;
        return;
    }
    size_t n = g_cells.size();
    std::vector<int8_t> vis(n, -1);  // -1: not looked at yet
    auto visible = [&](int k) {
        if (vis[(size_t)k] < 0) vis[(size_t)k] = InView(t, view, g_cells[(size_t)k].p + CVector{0, 0, 1.0f}) ? 1 : 0;
        return vis[(size_t)k] == 1;
    };
    std::vector<float> cost(n, 1e9f);
    std::vector<int> from(n, -1);
    using Q = std::pair<float, int>;
    std::priority_queue<Q, std::vector<Q>, std::greater<Q>> q;
    cost[(size_t)start] = 0;
    q.push({0.0f, start});
    CVector tp = t->GetPosition();
    int goal = -1, expanded = 0;
    while (!q.empty() && expanded < 1500) {
        auto [c, k] = q.top();
        q.pop();
        if (c > cost[(size_t)k]) continue;
        ++expanded;
        CVector cp = g_cells[(size_t)k].p + CVector{0, 0, 1.0f}, r = cp - tp;
        float dist = r.Length2D();
        if (dist > 1.2f && dist < 4.0f && std::fabs(r.z) < 1.5f && r.x * view.x + r.y * view.y < -0.2f * dist && !visible(k)) {
            goal = k;
            break;
        }
        for (int m : g_cells[(size_t)k].next) {
            float nc = c + (visible(m) ? 8.0f : 1.0f);
            if (nc < cost[(size_t)m]) cost[(size_t)m] = nc, from[(size_t)m] = k, q.push({nc, m});
        }
    }
    if (goal < 0) {  // no way behind it now: round to its back the game's way
        if (!e.routing || now - e.routeMs > 4000) {
            Cmd(0x06A8).I(h).I(GetPedRef(t)).I(5000).F(1.0f).F(180.0f).Run();
            e.routing = true, e.routeMs = now, e.routeTo = tp;
        }
        return;
    }
    std::vector<int> path;  // start .. goal
    for (int k = goal; k >= 0; k = from[(size_t)k]) path.push_back(k);
    std::reverse(path.begin(), path.end());
    // the next spot: the farthest of the next 6 it sees straight
    int next = path.size() > 1 ? path[1] : path[0];
    for (size_t s = 1; s < path.size() && s <= 6; ++s) {
        if (!Straight(body, g_cells[(size_t)path[s]].p)) break;
        next = path[s];
    }
    if (!seen && visible(next) && (path.size() < 2 || visible(path[1]))) {  // one step on it would be seen: it waits
        if (!e.waitSince) e.waitSince = now;
        if (now - e.waitSince < 6000) {
            if (e.routing) Cmd(0x05BA).I(h).I(1000).Run(), e.routing = false;  // TASK_STAND_STILL
            return;
        }
    } else {
        e.waitSince = 0;
    }
    GoTo(p, e, g_cells[(size_t)next].p, 7);  // sprinting
}

// A sniper stays where it is and shoots at whom it sees; out of sight of the players for 5 s it moves on to a spot
// (within 25 m) far from its player that sees it
void Snipe(CPed* p, Enemy& e, CPed* t, bool sees, uint64_t now) {
    int h = GetPedRef(p);
    if (e.mode == M_RELOCATE) {
        if ((p->GetPosition() - e.routeTo - CVector{0, 0, 1.0f}).Length2D() < 1.3f || now > e.modeUntil) {
            e.mode = M_FIGHT;
            Cmd(0x0350).I(h).I(1).Run();  // SET_CHAR_STAY_IN_SAME_PLACE
            SetOn(p, e, t);
        }
        return;
    }
    if (sees) {
        e.blindSince = 0;
        if (t != e.after || now - e.setOnMs > 8000) SetOn(p, e, t);
        return;
    }
    if (!e.blindSince) e.blindSince = now;
    CVector to;
    if (now - e.blindSince > 5000 && GridUsable() && SniperSpot(p->GetPosition(), t, 25.0f, to)) {
        e.blindSince = 0;
        e.mode = M_RELOCATE;
        e.modeUntil = now + 9000;
        Cmd(0x0350).I(h).I(0).Run();
        GoTo(p, e, to - CVector{0, 0, 1.0f}, 6);
    }
}

bool FindCover(CPed* p, CPed* t, CVector& out) {
    if (!GridUsable()) return false;
    CVector eye = t->GetPosition() + CVector{0, 0, 0.6f}, here = p->GetPosition();
    float best = -1e9f;
    int tries = 0;
    for (int k : StandCells()) {
        const Cell& c = g_cells[(size_t)k];
        CVector d = c.p - here;
        if (d.Length2D() > 9.0f || std::fabs(d.z + 1.0f) > 2.0f) continue;
        float toT = (c.p - t->GetPosition()).Length();
        if (toT < 7.0f || toT > 26.0f) continue;
        bool taken = false;
        for (auto& [q, e] : g_info)
            if (q != p && (e.mode == M_COVER_GO || e.mode == M_COVER_HOLD) && (e.cover - c.p).Length() < 1.6f) taken = true;
        if (taken || ++tries > 60) continue;
        if (Clear(eye, c.p + CVector{0, 0, 0.6f}) || !Clear(eye, c.p + CVector{0, 0, 1.5f})) continue;  // hidden down, seeing up
        float score = -d.Length2D() - toT * 0.3f + Rand(0.0f, 2.0f);
        if (score > best) best = score, out = c.p;
    }
    return best > -1e8f;
}

void Unstick(CPed* p, CPed* t) {
    if (!GridUsable()) return;
    const std::vector<int>& steps = StepsFor(t);
    int here = CellOf(p, &steps);
    if (here < 0) return;
    if ((g_cells[(size_t)here].p + CVector{0, 0, 1.0f} - p->GetPosition()).Length() < 1.0f) {  // on it: two steps on
        for (int s = 0; s < 2; ++s) {
            int next = here;
            for (int n : g_cells[(size_t)here].next)
                if (steps[(size_t)n] >= 0 && steps[(size_t)n] < steps[(size_t)next] && (g_cells[(size_t)n].room || !g_cells[(size_t)next].room))
                    next = n;
            here = next;
        }
    }
    CVector to = g_cells[(size_t)here].p;
    Cmd(0x00A1).I(GetPedRef(p)).F(to.x).F(to.y).F(to.z).Run();  // SET_CHAR_COORDINATES (feet on the floor)
    LOGD("Arena: an enemy stuck for 4 s set on towards its player");
}

// ---- the new kinds' own ways
// At a barricade: up to it from behind, then tearing at its boards (a punch, again and again) till they are all off;
// then the fight. One that cannot get there in 30 s comes through all the same.
std::vector<std::pair<CPed*, CPed*>> g_toRaise;  // medic, the fallen one: up again after this round of tactics

void Breach(CPed* p, Enemy& e, CPed* t, uint64_t now) {
    int h = GetPedRef(p);
    Bar* b = e.barricade >= 0 && e.barricade < (int)g_bars.size() ? &g_bars[(size_t)e.barricade] : nullptr;
    if (!b || b->boards <= 0 || now - e.breachSince > 30000) {
        e.mode = M_FIGHT;
        e.tearing = false;
        e.routing = false;
        Cmd(0x0687).I(h).Run();  // CLEAR_CHAR_TASKS
        SetOn(p, e, t);
        return;
    }
    CVector way = b->built ? b->pos - (p->GetPosition() - CVector{0, 0, 1.0f}) : b->pos - b->spawn;  // built: from its own side
    float l = std::max(0.1f, way.Length2D());
    CVector spot = b->pos - CVector{way.x / l, way.y / l, 0.0f} * 0.9f;  // its own side of the boards
    if ((p->GetPosition() - CVector{0, 0, 1.0f} - spot).Length2D() > 1.2f) {
        e.tearing = false;
        GoTo(p, e, spot, 6);
        return;
    }
    if (!e.tearing || now - e.setOnMs > 2500) {
        e.tearing = true;
        e.routing = false;
        e.setOnMs = now;
        CVector to = b->pos - (p->GetPosition() - CVector{0, 0, 1.0f});
        Cmd(0x0173).I(h).F(std::atan2(-to.x, to.y) * 57.29578f).Run();  // SET_CHAR_HEADING: at the boards
        Cmd(0x0605).I(h).S("FightA_1").S("PED").F(6.0f).I(1).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM, looped
    }
}

// The medic: CPR on a fallen one it reached (3 s, then it is up again, 5 at most), else running to the nearest fallen or
// hurt one within 40 m (sprinting); nobody to see to: it fights as the others. Its healing (20% of the most health, every
// 2 s, all of its own within 5 m) goes on all the while.
bool Medic(CPed* p, Enemy& e, CPed* t, uint64_t now) {
    int h = GetPedRef(p);
    if (now - e.healMs > 2000) {
        e.healMs = now;
        for (auto& [q, qe] : g_info)
            if (IsPedValid(q) && q->Health() > 0.0f && (q->GetPosition() - p->GetPosition()).Length() < 5.0f && q->Health() < qe.maxHealth)
                q->Health() = std::min(qe.maxHealth, q->Health() + qe.maxHealth * 0.2f);
    }
    // the healing gas, once: at a hurt one of its own 4-18 m off when the arc to it is clear
    if (!e.healGas && !e.gasAt) {
        for (auto& [q, qe] : g_info) {
            if (q == p || !IsPedValid(q) || q->Health() <= 0.0f || q->Health() > qe.maxHealth * 0.65f) continue;
            float dq = (q->GetPosition() - p->GetPosition()).Length();
            if (dq < 4.0f || dq > 18.0f) continue;
            CVector aim = q->GetPosition() - CVector{0, 0, 0.9f};
            float flight = ClearFlight(ThrowFrom(p, aim), aim);
            if (flight <= 0.0f) continue;
            e.healGas = true;
            e.gasAim = aim;
            e.gasFlight = flight;
            e.gasAt = now + 400;
            CVector to = q->GetPosition() - p->GetPosition();
            Cmd(0x0173).I(h).F(std::atan2(-to.x, to.y) * 57.29578f).Run();
            if (EnsureBlock("GRENADE")) Cmd(0x0605).I(h).S("WEAPON_throw").S("GRENADE").F(8.0f).I(0).I(0).I(0).I(0).I(-1).Run();
            return true;
        }
    }
    if (e.gasAt) return true;  // the throw going on
    if (e.cprUntil) {
        auto it = e.cprTarget ? g_info.find(e.cprTarget) : g_info.end();
        if (it == g_info.end() || !IsPedValid(e.cprTarget)) {
            e.cprUntil = 0;
        } else if (now >= e.cprUntil) {
            e.cprUntil = 0;
            ++e.revives;
            g_toRaise.push_back({p, e.cprTarget});
        } else {
            return true;
        }
        e.cprTarget = nullptr;
        SetOn(p, e, t);
        return true;
    }
    CPed* best = nullptr;
    float bestD = 40.0f;
    bool dead = false;
    for (auto& [q, qe] : g_info) {
        if (q == p || !IsPedValid(q) || q->AreaCode() != p->AreaCode()) continue;
        bool down = q->Health() <= 0.0f;
        if (down && (e.revives >= 5 || (qe.revivedBy && qe.revivedBy != p) || qe.role == R_BOSS)) continue;
        if (!down && q->Health() >= qe.maxHealth * 0.7f) continue;
        float d = (q->GetPosition() - p->GetPosition()).Length() * (down ? 0.7f : 1.0f);  // the fallen first
        if (d < bestD) bestD = d, best = q, dead = down;
    }
    if (!best) return false;
    float d = (best->GetPosition() - p->GetPosition()).Length2D();
    if (dead && d < 1.4f && EnsureBlock("MEDIC")) {
        g_info[best].revivedBy = p;
        CVector to = best->GetPosition() - p->GetPosition();
        Cmd(0x0173).I(h).F(std::atan2(-to.x, to.y) * 57.29578f).Run();
        Cmd(0x0605).I(h).S("CPR").S("MEDIC").F(4.0f).I(1).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM, looped
        e.cprUntil = now + 3000;
        e.cprTarget = best;
        e.routing = false;
        return true;
    }
    if (d > (dead ? 1.2f : 3.5f)) {
        GoTo(p, e, best->GetPosition() - CVector{0, 0, 1.0f}, 7);  // sprinting
        return true;
    }
    return false;  // by a hurt one: the healing does it, meanwhile it fights
}

// the fallen one up again (a new man of its kind where it lay, 60% of its health)
void RaiseFallen() {
    for (auto& [medic, q] : g_toRaise) {
        auto it = g_info.find(q);
        if (it == g_info.end() || !IsPedValid(q) || q->Health() > 0.0f) continue;
        Enemy was = it->second;
        CVector at = q->GetPosition() + CVector{0, 0, 1.0f};
        Cmd(0x009B).I(GetPedRef(q)).Run();  // DELETE_CHAR: the body
        g_info.erase(it);
        g_enemies.erase(q);
        g_killed.erase(q);
        Role role = was.role;
        int h = MakeEnemy(was.model, was.weapon, std::max(30, (int)(was.maxHealth * 0.6f)), 0, was.accuracy, at, was.squad, role,
                          was.silent);
        if (CPed* np = h >= 0 ? GetPed(h) : nullptr) {
            g_info[np].maxHealth = was.maxHealth;
            for (CPed*& tw : g_twinPeds)
                if (tw == q) tw = np;
            for (CPed*& fl : g_flamers)
                if (fl == q) fl = np;
            LOGI("Arena: a medic got a fallen one up");
        }
    }
    g_toRaise.clear();
}

// The flamethrower man: walks (never runs) at its player; within 7 m it burns, from 9-22 m in sight it throws molotovs
bool Flame(CPed* p, Enemy& e, CPed* t, float d, bool sees, uint64_t now) {
    int h = GetPedRef(p);
    if (e.grenadeUntil) {
        if (now > e.grenadeUntil && !e.throwAt) e.grenadeUntil = 0, e.setOnMs = 0;
        return true;
    }
    if (sees && d > 11.0f && d < 22.0f && e.grenades > 0 && now >= e.nextThrowMs && StartThrow(p, e, t)) {
        e.nextThrowMs = now + (uint64_t)RandInt(8000, 11000);
        return true;
    }
    // walks up to some 8 m (no closer than it must) and burns from there: it stands and pours the fire at the player
    if (d > 9.0f || !sees) {
        CVector wp;
        if (!sees && Waypoint(p, t, wp)) GoTo(p, e, wp, 4);
        else GoTo(p, e, t->GetPosition() - CVector{0, 0, 1.0f}, 4);  // walking
        return true;
    }
    if (e.routing || t != e.after || now - e.setOnMs > 1800) {
        e.routing = false;
        e.after = t;
        e.setOnMs = now;
        CVector to = t->GetPosition() - p->GetPosition();
        Cmd(0x0173).I(h).F(std::atan2(-to.x, to.y) * 57.29578f).Run();  // SET_CHAR_HEADING
        Cmd(0x074D).I(h).I(GetPedRef(t)).I(2000).Run();                  // TASK_SHOOT_AT_CHAR: the flame from where it stands
    }
    return true;
}

// ---- the Butcher's charge (every frame, the host's game)
bool SawRay(const CVector& a, const CVector& b, CVector& at, CVector& normal) {  // buildings and objects
    struct {
        CVector point;
        float c;
        CVector normal;
        uint8_t rest[0x2C - 0x1C];
    } cp{};
    CEntity* hit = nullptr;
    bool struck = Call<0x56BA00, bool>(&a, &b, &cp, &hit, true, false, false, true, false, false, false, false);
    if (struck) at = cp.point, normal = cp.normal;
    return struck;
}
void PlayAnim(int h, const char* anim, const char* block, bool loop) {
    Cmd(0x0605).I(h).S(anim).S(block).F(6.0f).I(loop ? 1 : 0).I(1).I(1).I(0).I(-1).Run();  // TASK_PLAY_ANIM (in place)
}
void SendHit(uint8_t id) {
    Writer w(proto::G_ARENA);
    w.U8(A_HIT);
    Net().SendTo(id, proto::CH_RELIABLE, w);
}
void SawFrame(uint64_t now) {
    float dt = std::min(0.1f, TimeStep() / 50.0f);
    for (auto& [p, e] : g_info) {
        if (e.role != R_SAW || !IsPedValid(p) || p->Health() <= 0.0f) continue;
        int h = GetPedRef(p);
        if (e.sawStage == 1) {  // winding up: turned to its player all the while
            CPed* t = e.after && IsPedValid(e.after) ? e.after : NearestPlayer(p->GetPosition());
            if (t) {
                CVector d = t->GetPosition() - p->GetPosition();
                d.z = 0;
                float l = d.Length();
                if (l > 0.1f) e.sawDir = d * (1.0f / l);
                Cmd(0x0173).I(h).F(std::atan2(-e.sawDir.x, e.sawDir.y) * 57.29578f).Run();
            }
            if (now >= e.sawUntil) {
                e.sawStage = 2;
                e.sawUntil = now + 5000;
                e.bounces = 0;
                e.sawHit.clear();
                PlayAnim(h, "sprint_civi", "PED", true);
            }
            continue;
        }
        if (e.sawStage == 3) {
            if (now >= e.sawUntil) {
                e.sawStage = 0;
                e.nextChargeMs = now + 3000;
                Cmd(0x0687).I(h).Run();  // CLEAR_CHAR_TASKS
                SetOn(p, e, NearestPlayer(p->GetPosition()));
            }
            continue;
        }
        if (e.sawStage != 2) continue;
        CVector pos = p->GetPosition();
        float step = 17.0f * dt;
        CVector chest = pos + CVector{0, 0, 0.2f}, knee = pos - CVector{0, 0, 0.5f}, hit, n;
        bool wall = SawRay(chest, chest + e.sawDir * (step + 0.55f), hit, n) || SawRay(knee, knee + e.sawDir * (step + 0.55f), hit, n);
        CVector next = pos + e.sawDir * step;
        float z = 0;
        if (!wall && (!FloorBelow(next.x, next.y, next.z + 0.3f, z) || std::fabs(z + 1.0f - pos.z) > 1.2f)) wall = true, n = e.sawDir * -1.0f;  // an edge
        if (wall) {
            if (p->Health() <= 100.0f && e.bounces < 8) {  // the Blight's bounce off the wall
                CVector fn{n.x, n.y, 0.0f};
                float fl = std::max(0.01f, fn.Length());
                fn = fn * (1.0f / fl);
                float dot = e.sawDir.x * fn.x + e.sawDir.y * fn.y;
                e.sawDir = e.sawDir - fn * (2.0f * dot);
                e.sawDir.z = 0;
                float dl = std::max(0.01f, e.sawDir.Length());
                e.sawDir = e.sawDir * (1.0f / dl);
                ++e.bounces;
                e.sawUntil = std::max(e.sawUntil, now + 1500);
                Cmd(0x0173).I(h).F(std::atan2(-e.sawDir.x, e.sawDir.y) * 57.29578f).Run();
                continue;
            }
            e.sawStage = 3;  // into the wall: stunned 4 s
            e.sawUntil = now + 4000;
            Cmd(0x0792).I(h).Run();  // CLEAR_CHAR_TASKS_IMMEDIATELY
            PlayAnim(h, "IDLE_tired", "PED", true);
            continue;
        }
        next.z = z + 1.0f;
        p->MoveSpeed() = CVector{};
        SetEntityPosition(p, next);
        // whoever it runs into falls wounded; it does not stop
        auto struck = [&](CPed* q) {
            if (!q || e.sawHit.count(q) || q->Health() <= 0.0f) return false;
            CVector d = q->GetPosition() - next;
            if (d.Length2D() > 1.3f || std::fabs(d.z) > 1.6f) return false;
            e.sawHit.insert(q);
            return true;
        };
        if (struck(FindPlayerPed(0))) KnockDown();
        for (auto& [id, rp] : Players::Instance().All())
            if (rp.ped && struck(rp.ped)) SendHit(id);
        if (now >= e.sawUntil) {  // ran out of breath
            e.sawStage = 0;
            e.nextChargeMs = now + 2500;
            Cmd(0x0687).I(h).Run();
        }
    }
}
// the Butcher between the charges: after the player, the charge when it sees one 4-28 m off
void Saw(CPed* p, Enemy& e, CPed* t, float d, bool sees, uint64_t now) {
    if (e.sawStage != 0) return;
    int h = GetPedRef(p);
    if (sees && d > 4.0f && d < 28.0f && now >= e.nextChargeMs) {
        e.sawStage = 1;
        e.sawUntil = now + 1400;
        e.after = t;
        e.routing = false;
        Cmd(0x0687).I(h).Run();                 // CLEAR_CHAR_TASKS
        PlayAnim(h, "IDLE_armed", "PED", true);  // the saw up, roaring
        return;
    }
    CVector wp;
    if (!sees && Waypoint(p, t, wp)) {
        GoTo(p, e, wp, 6);
        return;
    }
    if (e.routing || t != e.after || now - e.setOnMs > 6000) SetOn(p, e, t);
}

// the Officer: stays at the back; at a player who comes close; falls back when hurt; heals; calls men
void Officer(CPed* p, Enemy& e, CPed* t, float d, bool sees, uint64_t now) {
    int h = GetPedRef(p);
    if (p->Health() < e.lastHealth - 0.5f) e.lastHurtMs = now;
    e.lastHealth = p->Health();
    if (now - e.lastHurtMs > 3000 && p->Health() < e.maxHealth)  // nobody hurts him: he heals (some 4% a second)
        p->Health() = std::min(e.maxHealth, p->Health() + e.maxHealth * 0.02f), e.lastHealth = p->Health();
    if (now >= e.nextCallMs) {  // 4 men to him every 30 s
        e.nextCallMs = now + 30000;
        const Faction& f = kFactions[g_arena.faction];
        g_squads.push_back(Squad{});
        int squad = (int)g_squads.size() - 1;
        int wave = std::max(1, g_arena.wave);
        int tier = std::clamp((int)std::lround(Progress(wave) * 3.0f), 0, 3);
        for (int k = 0; k < 4; ++k)
            MakeEnemy(f.models[(size_t)RandInt(0, 3)], f.weapons[tier][RandInt(0, 1)], 100 + 15 * wave, ArmourFor(wave, 0),
                      std::clamp(45 + 5 * wave, 45, 85), MemberSpot(p->GetPosition(), k), squad, k == 1 ? R_GRENADIER : R_GUN, false);
        ui::TheChat().AddSystem(TR("Офицер вызвал подкрепление!", "The Officer called for help!"));
    }
    if (e.retreating) {
        if ((p->GetPosition() - CVector{0, 0, 1.0f} - e.routeTo).Length2D() < 1.3f || now > e.modeUntil) {
            e.retreating = false;
            e.home = p->GetPosition() - CVector{0, 0, 1.0f};
            Cmd(0x0350).I(h).I(1).Run();
            Cmd(0x05BA).I(h).I(2000).Run();  // TASK_STAND_STILL
        }
        return;
    }
    // hurt and a player on him: back to the farthest spot from the players
    if (d < 9.0f && p->Health() < e.maxHealth * 0.6f && GridUsable() && now - e.modeUntil > 6000) {
        std::vector<CVector> players = PlayerSpots();
        CVector to;
        float best = -1;
        for (int k : StandCells()) {
            CVector c = g_cells[(size_t)k].p;
            if ((c - p->GetPosition()).Length() > 30.0f) continue;
            float dp = DistToPlayers(c + CVector{0, 0, 1.0f}, players);
            if (dp > best) best = dp, to = c;
        }
        if (best > d + 4.0f) {
            e.retreating = true;
            e.modeUntil = now + 9000;
            Cmd(0x0350).I(h).I(0).Run();
            e.routing = false;
            GoTo(p, e, to, 7);
            return;
        }
    }
    if (d < 14.0f && sees) {  // a player came: the katana
        Cmd(0x0350).I(h).I(0).Run();
        if (e.routing || t != e.after || now - e.setOnMs > 5000) SetOn(p, e, t);
        return;
    }
    // nobody near: back to his spot and waiting there
    if ((p->GetPosition() - CVector{0, 0, 1.0f} - e.home).Length2D() > 2.0f) {
        GoTo(p, e, e.home, 6);
        return;
    }
    if (e.routing || now - e.setOnMs > 3000) {
        e.routing = false;
        e.setOnMs = now;
        e.after = nullptr;
        Cmd(0x0350).I(h).I(1).Run();
        CVector to = t->GetPosition() - p->GetPosition();
        Cmd(0x0173).I(h).F(std::atan2(-to.x, to.y) * 57.29578f).Run();
        Cmd(0x05BA).I(h).I(3000).Run();  // TASK_STAND_STILL
    }
}

// ---- the knifemen run faster: a push on along where they run, every frame
void KnifeFrame() {
    float dt = std::min(0.1f, TimeStep() / 50.0f);
    for (auto& [p, e] : g_info) {
        if (e.role != R_KNIFE || !IsPedValid(p) || p->Health() <= 0.0f || p->InVehicle() || p->MoveState() < 6) continue;  // running, sprinting
        float hd = p->GetHeading();
        CVector f{-std::sin(hd), std::cos(hd), 0.0f};
        CVector pos = p->GetPosition();
        if (!Clear(pos, pos + f * 0.7f) || !Clear(pos - CVector{0, 0, 0.5f}, pos - CVector{0, 0, 0.5f} + f * 0.7f)) continue;
        p->SetPositionRaw(pos + f * (2.6f * dt));
    }
}

// ---- the medics' healing gas (one grenade each): a green cloud 8 s long that heals their own within 4 m
struct Gas {
    CVector pos, vel;
    int obj = -1;
    bool landed = false;
    uint64_t until = 0, smokeMs = 0, healMs = 0;
};
std::vector<Gas> g_gas;
void AddGas(const CVector& from, const CVector& vel) {
    Gas g;
    g.pos = from, g.vel = vel;
    if (IsObjectModel(343) && LoadModelNow(343)) g.obj = MakeFloating(343, from, 0.0f);  // the tear gas canister
    g.until = NowMs() + 4000;  // in the air at most
    g_gas.push_back(g);
}
void GreenSmoke(const CVector& at) {
    void* system = Ref<void*>(0xA9AE00 + 0x24);  // g_fx.m_SmokeII3expand
    if (!system) return;
    float mult[7] = {0.35f, 1.0f, 0.35f, 0.45f, 1.6f, 0.0f, 2.2f};  // FxPrtMult_c: green, see-through, big, 2.2 s
    for (int k = 0; k < 2; ++k) {
        CVector p = at + CVector{Rand(-2.2f, 2.2f), Rand(-2.2f, 2.2f), Rand(0.0f, 0.6f)};
        CVector v{Rand(-0.3f, 0.3f), Rand(-0.3f, 0.3f), Rand(0.2f, 0.6f)};
        CallMethod<0x4AA440>(system, &p, &v, 0.0f, (const void*)mult, -1.0f, 1.0f, 0.6f, false);  // FxSystem_c::AddParticle
    }
}
void UpdateGas(uint64_t now) {
    float dt = std::min(0.1f, TimeStep() / 50.0f);
    for (auto it = g_gas.begin(); it != g_gas.end();) {
        Gas& g = *it;
        if (now > g.until) {
            DelObj(g.obj);
            it = g_gas.erase(it);
            continue;
        }
        if (!g.landed) {
            g.vel.z -= kGravity * dt;
            CVector next = g.pos + g.vel * dt, hit;
            if (Ray(g.pos, next, hit)) {
                g.landed = true;
                g.pos = hit + CVector{0, 0, 0.1f};
                g.until = now + 8000;
            } else {
                g.pos = next;
            }
            if (g.obj >= 0 && GetObject(g.obj)) Cmd(0x01BC).I(g.obj).F(g.pos.x).F(g.pos.y).F(g.pos.z).Run();
        } else {
            if (now - g.smokeMs > 70) g.smokeMs = now, GreenSmoke(g.pos);
            CVector dir{0, 0, -1};
            Call<0x7000E0>((uint8_t)0, g.pos + CVector{0, 0, 1.0f}, dir, 6.0f, 0.1f, 0.9f, 0.2f, (uint8_t)0, false, (CEntity*)nullptr);  // a green light
            if (g_arena.host && now - g.healMs > 500) {
                g.healMs = now;
                for (auto& [q, qe] : g_info)
                    if (IsPedValid(q) && q->Health() > 0.0f && (q->GetPosition() - g.pos).Length() < 4.0f)
                        q->Health() = std::min(qe.maxHealth, q->Health() + qe.maxHealth * 0.04f);
            }
        }
        ++it;
    }
}
void ClearGas() {
    for (Gas& g : g_gas) DelObj(g.obj);
    g_gas.clear();
}

// ---- the grenade launcher's shells (T-Bone's, and a player's with his AK): fast and flat (30 m/s, half the
// grenade's fall), farther than a thrown grenade, and they go off the moment they hit anything (a wall, the floor, an
// object, a person). The game that fired one sets it off (CExplosion::AddExplosion in the shooter's name: Fire sends the
// blast to the others); the other games fly a copy to show it.
constexpr float kShellSpeed = 30.0f, kShellGravity = 9.8f;
struct Shell {
    CVector pos, vel;
    int obj = -1;
    CEntity* creator = nullptr;  // the shooter: set only in its own game (the blast is set off there)
    bool real = false;
    uint64_t until = 0;
};
std::vector<Shell> g_shells;
void AddShell(const CVector& from, const CVector& vel, CEntity* creator, bool real) {
    Shell sh;
    sh.pos = from, sh.vel = vel, sh.creator = creator, sh.real = real;
    sh.until = NowMs() + 5000;
    if (IsObjectModel(342) && LoadModelNow(342)) sh.obj = MakeFloating(342, from, 0.0f);
    g_shells.push_back(sh);
}
void FireShell(const CVector& from, const CVector& vel, CEntity* creator) {
    Send(A_SHELL, [&](Writer& w) {
        w.Vec({from.x, from.y, from.z});
        w.Vec({vel.x, vel.y, vel.z});
    });
    AddShell(from, vel, creator, true);
    CallMethod<0x506EA0>((void*)0xB6BC90, 42 /* AE_FRONTEND_FIRE_FAIL_ROCKET: a dull thump */, 0.0f, 0.7f);
}
// the velocity that brings a shell from `from` to `to` at about kShellSpeed
CVector ShellVel(const CVector& from, const CVector& to) {
    CVector d = to - from;
    float t = std::max(0.15f, d.Length() / kShellSpeed);
    return {d.x / t, d.y / t, (d.z + 0.5f * kShellGravity * t * t) / t};
}
void UpdateShells(uint64_t now) {
    float dt = std::min(0.1f, TimeStep() / 50.0f);
    for (auto it = g_shells.begin(); it != g_shells.end();) {
        Shell& sh = *it;
        CVector next = sh.pos + sh.vel * dt, hit, n;
        sh.vel.z -= kShellGravity * dt;
        bool boom = SawRay(sh.pos, next, hit, n);
        if (!boom) hit = next;
        // a person on the way (not the shooter)
        if (!boom && sh.real)
            if (CPoolHeader* pool = PedPool())
                for (int i = 0; i < pool->size && !boom; ++i) {
                    if (pool->byteMap[i] < 0) continue;
                    auto* q = (CPed*)(pool->objects + (size_t)i * kPedPoolElemSize);
                    if ((CEntity*)q == sh.creator || q->Health() <= 0.0f) continue;
                    if ((q->GetPosition() - next).Length() < 0.9f) boom = true, hit = next;
                }
        if (boom || now > sh.until) {
            DelObj(sh.obj);
            if (sh.real && boom)
                Call<0x736A50>((CEntity*)nullptr, sh.creator, 0 /* EXPLOSION_GRENADE */, hit, (uint32_t)0, (uint8_t)1, -1.0f, (uint8_t)0);  // AddExplosion
            it = g_shells.erase(it);
            continue;
        }
        sh.pos = next;
        if (sh.obj >= 0 && GetObject(sh.obj)) Cmd(0x01BC).I(sh.obj).F(sh.pos.x).F(sh.pos.y).F(sh.pos.z).Run();
        ++it;
    }
}
void ClearShells() {
    for (Shell& sh : g_shells) DelObj(sh.obj);
    g_shells.clear();
}

// T-Bone: fights with his AK; a player in sight 7-40 m off and the launcher loaded: a shell at him, then he runs to the
// back (the farthest spot from the players within 30 m), reloads it there for 4 s and comes back into the fight
bool TBone(CPed* p, Enemy& e, CPed* t, float d, bool sees, uint64_t now) {
    int h = GetPedRef(p);
    if (e.uglStage == 1) {
        if ((p->GetPosition() - CVector{0, 0, 1.0f} - e.routeTo).Length2D() < 1.3f || now > e.uglUntil) {
            e.uglStage = 2;
            e.uglUntil = now + 4000;
            e.routing = false;
            Cmd(0x0687).I(h).Run();
            if (EnsureBlock("RIFLE")) Cmd(0x0605).I(h).S("RIFLE_load").S("RIFLE").F(4.0f).I(1).I(0).I(0).I(0).I(4000).Run();  // reloading
        }
        return true;
    }
    if (e.uglStage == 2) {
        if (now >= e.uglUntil) {
            e.uglStage = 0;
            e.uglLoaded = true;
            e.nextUglMs = now + 3000;
            Cmd(0x0687).I(h).Run();
            SetOn(p, e, t);
        }
        return true;
    }
    if (!e.uglLoaded || !sees || d < 7.0f || d > 40.0f || now < e.nextUglMs) return false;
    CVector to = t->GetPosition() - p->GetPosition();
    Cmd(0x0173).I(h).F(std::atan2(-to.x, to.y) * 57.29578f).Run();  // at him
    float l = std::max(0.1f, to.Length2D());
    CVector from = p->GetPosition() + CVector{to.x / l, to.y / l, 0.0f} * 0.7f + CVector{0, 0, 0.45f};
    FireShell(from, ShellVel(from, t->GetPosition() - CVector{0, 0, 0.5f}), p);
    e.uglLoaded = false;
    // to the back
    std::vector<CVector> players = PlayerSpots();
    CVector back = p->GetPosition() - CVector{0, 0, 1.0f};
    float best = -1;
    if (GridUsable())
        for (int k : StandCells()) {
            CVector c = g_cells[(size_t)k].p;
            if ((c - p->GetPosition()).Length() > 30.0f) continue;
            float dp = DistToPlayers(c + CVector{0, 0, 1.0f}, players);
            if (dp > best) best = dp, back = c;
        }
    e.uglStage = 1;
    e.uglUntil = now + 9000;
    e.routing = false;
    GoTo(p, e, back, 7);
    return true;
}

// a player's launcher (N): a shell where the camera looks; empty, N loads a grenade into it (1.5 s)
int GrenadesHeld(CPlayerPed* me) {
    for (int k = 0; k < NUM_WEAPON_SLOTS; ++k)
        if (me->Weapons()[k].type == WEAPON_GRENADE) return me->Weapons()[k].ammoTotal;
    return 0;
}
void PlayerUgl() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || me->Health() <= 0.0f || me->InVehicle()) return;
    uint64_t now = NowMs();
    if (g_uglReadyMs && now >= g_uglReadyMs) g_uglReadyMs = 0, g_uglLoaded = true;
    if (g_uglReadyMs) return;  // loading
    if (me->ActiveWeapon().type != 30) {
        ui::TheChat().AddError(TR("Возьмите АК-47 в руки.", "Take the AK-47 in hand."));
        return;
    }
    if (g_uglLoaded) {
        CVector f = cam::Front();
        float l = std::max(0.01f, f.Length());
        f = f * (1.0f / l);
        CVector from = me->GetPosition() + f * 0.8f + CVector{0, 0, 0.5f};
        FireShell(from, f * kShellSpeed + CVector{0, 0, 1.5f}, me);
        g_uglLoaded = false;
        return;
    }
    for (int k = 0; k < NUM_WEAPON_SLOTS; ++k) {
        CWeapon& w = me->Weapons()[k];
        if (w.type != WEAPON_GRENADE || w.ammoTotal <= 0) continue;
        --w.ammoTotal;
        w.ammoInClip = std::min(w.ammoInClip, w.ammoTotal);
        if (w.ammoTotal <= 0) Cmd(0x0555).I(GetPedRef(me)).I(WEAPON_GRENADE).Run();  // the last one went in
        g_uglReadyMs = now + 1500;
        Cmd(0x01B9).I(GetPedRef(me)).I(30).Run();
        if (EnsureBlock("RIFLE")) Cmd(0x0A1A).I(GetPedRef(me)).S("RIFLE_load").S("RIFLE").F(4.0f).I(0).I(0).I(0).I(0).I(1500).Run();
        return;
    }
    ui::TheChat().AddError(TR("Для перезарядки подствольника нужна граната.", "A grenade is needed to reload the launcher."));
}

// /arena тибон (the host, an arena on): T-Bone comes now, some 12 m in front of the host, to try him out
void SpawnTBone() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me) return;
    int b = kTBone;
    Send(A_BOSS, [b](Writer& wr) { wr.U8((uint8_t)b); });
    LoadBoss(b);
    g_arena.boss = b;
    int model = IsModelLoaded(kBossModel) ? kBossModel : kFactions[g_arena.faction].models[0];
    CVector f = cam::Front();
    f.z = 0;
    float l = std::max(0.01f, f.Length());
    CVector want = me->GetPosition() + f * (12.0f / l), at = want;
    if (GridUsable()) {
        float best = 1e9f;
        for (int k : StandCells()) {
            float dd = (g_cells[(size_t)k].p + CVector{0, 0, 1.0f} - want).Length();
            if (dd < best) best = dd, at = g_cells[(size_t)k].p + CVector{0, 0, 1.0f};
        }
    }
    g_squads.push_back(Squad{});
    int h = MakeEnemy(model, 30, 100, 100, 85, at, (int)g_squads.size() - 1, R_BOSS, false);
    if (CPed* p = h >= 0 ? GetPed(h) : nullptr) g_bossPed = p;
    ui::TheChat().AddSystem(TR("Ти-Бон Мендез вызван (тест).", "T-Bone Mendez is here (a test)."));
}

void Tactics() {
    uint64_t now = NowMs();
    // a squad's push: all of it at the players together
    for (auto& [p, e] : g_info) {
        Squad& sq = g_squads[(size_t)e.squad];
        if (e.role == R_KNIFE || e.role == R_SNIPER) continue;
        if (sq.nextPushMs && now >= sq.nextPushMs && (e.mode == M_FIGHT || e.mode == M_COVER_HOLD || e.mode == M_COVER_GO)) {
            e.mode = M_PUSH;
            e.modeUntil = now + (uint64_t)RandInt(7000, 11000);
            e.after = nullptr;  // set on again below
        }
    }
    for (auto& sq : g_squads)
        if (sq.nextPushMs && now >= sq.nextPushMs) sq.nextPushMs = now + (uint64_t)RandInt(18000, 26000);
    for (auto it = g_paths.begin(); it != g_paths.end();) it = now - it->second.ms > 10000 ? g_paths.erase(it) : std::next(it);

    for (CPed* p : g_enemies) {
        if (!IsPedValid(p) || p->Health() <= 0.0f) continue;
        auto it = g_info.find(p);
        if (it == g_info.end()) continue;
        Enemy& e = it->second;
        int h = GetPedRef(p);
        // its player, or a nearer one by 5 m (no going back and forth between two)
        CPed* t = e.after && IsPedValid(e.after) && e.after->Health() > 0.0f && e.after->AreaCode() == p->AreaCode() ? e.after : nullptr;
        CPed* n = NearestPlayer(p->GetPosition());
        if (!t || (n && (n->GetPosition() - p->GetPosition()).Length() + 5.0f < (t->GetPosition() - p->GetPosition()).Length())) t = n;
        if (!t) continue;
        float d = (t->GetPosition() - p->GetPosition()).Length();
        CVector eye = p->GetPosition() + CVector{0, 0, 0.6f}, at = t->GetPosition() + CVector{0, 0, 0.4f};
        bool sees = Clear(eye, at);
        // stuck: no step on in 4 s, the player out of its sight and not next to it
        bool still = e.mode == M_COVER_HOLD || (e.role == R_SNIPER && e.mode != M_RELOCATE) || (e.role == R_KNIFE && e.waitSince) ||
                     (e.mode == M_BREACH && e.tearing) || e.cprUntil || e.role == R_OFFICER || (e.role == R_SAW && e.sawStage != 0) ||
                     e.uglStage == 2;
        if ((p->GetPosition() - e.lastPos).Length() > 0.7f || still || sees || d < 4.0f) {
            e.lastPos = p->GetPosition();
            e.lastMoveMs = now;
        } else if (now - e.lastMoveMs > 4000 && !e.grenadeUntil && !(e.role == R_BOSS && g_arena.boss == kTBone)) {  // T-Bone is never moved by hand
            Unstick(p, t);
            e.lastPos = p->GetPosition();
            e.lastMoveMs = now;
            e.routing = false;
            if (e.mode == M_COVER_GO || e.mode == M_COVER_HOLD) e.mode = M_FIGHT;
            if (e.role != R_KNIFE) SetOn(p, e, t);
            continue;
        }
        if (e.role == R_KNIFE) {
            e.after = t;
            Sneak(p, e, t, d, now);
            continue;
        }
        if (e.role == R_SNIPER) {
            Snipe(p, e, t, sees, now);
            continue;
        }
        if (e.role == R_SAW) {
            Saw(p, e, t, d, sees, now);
            continue;
        }
        if (e.role == R_BOSS && g_arena.boss == kTBone && TBone(p, e, t, d, sees, now)) continue;
        if (e.role == R_OFFICER) {
            Officer(p, e, t, d, sees, now);
            continue;
        }
        // a barricade in its way (one the players built, or one it came up against): it tears it down first
        if (e.mode != M_BREACH && !sees && !e.cprUntil && !e.grenadeUntil)
            for (size_t i = 0; i < g_bars.size(); ++i) {
                CVector bd = g_bars[i].pos - (p->GetPosition() - CVector{0, 0, 1.0f});
                if (g_bars[i].boards > 0 && bd.Length2D() < 2.2f && std::fabs(bd.z) < 2.0f) {
                    e.mode = M_BREACH;
                    e.barricade = (int)i;
                    e.breachSince = now;
                    e.tearing = false;
                    break;
                }
            }
        if (e.mode == M_BREACH) {
            Breach(p, e, t, now);
            continue;
        }
        if (e.role == R_FLAMER && Flame(p, e, t, d, sees, now)) continue;
        if (e.role == R_MEDIC && Medic(p, e, t, now)) continue;
        // the throw done: back to what it did
        if (e.grenadeUntil) {
            if (now > e.grenadeUntil && !e.throwAt) {
                e.grenadeUntil = 0;
                if (e.mode == M_COVER_HOLD) Cmd(0x05C5).I(h).I(1500).Run();  // TASK_DUCK
                else SetOn(p, e, t);
            }
            continue;
        }
        switch (e.mode) {
            case M_FLANK: {
                bool close = d < 9.0f;
                bool hurt = p->Health() < e.spawnHealth - 1.0f;
                if (close || hurt || now > e.modeUntil) {
                    e.mode = M_FIGHT;
                    SetOn(p, e, t);
                    continue;
                }
                break;
            }
            case M_FIGHT:
                if (sees && d < 28.0f && now >= e.nextCoverMs) {
                    e.nextCoverMs = now + (uint64_t)RandInt(6000, 12000);
                    if (RandInt(0, 99) < 60 && FindCover(p, t, e.cover)) {
                        e.mode = M_COVER_GO;
                        e.modeUntil = now + 6000;
                        e.routing = false;
                        Cmd(0x05D3).I(h).F(e.cover.x).F(e.cover.y).F(e.cover.z).I(6).I(6000).Run();  // TASK_GO_STRAIGHT_TO_COORD, running
                        continue;
                    }
                }
                break;
            case M_COVER_GO:
                if ((p->GetPosition() - e.cover).Length2D() < 1.3f || now > e.modeUntil) {
                    e.mode = M_COVER_HOLD;
                    e.modeUntil = now + (uint64_t)RandInt(5000, 10000);
                    e.popped = false;
                    e.popUntil = now + 1200;
                    Cmd(0x05C5).I(h).I(1500).Run();  // TASK_DUCK
                }
                continue;
            case M_COVER_HOLD:
                if (now > e.modeUntil || d < 6.0f) {  // waited enough, or the player is on it: at it
                    e.mode = M_PUSH;
                    e.modeUntil = now + (uint64_t)RandInt(6000, 10000);
                    SetOn(p, e, t);
                    continue;
                }
                if (now >= e.popUntil) {
                    e.popped = !e.popped;
                    if (e.popped) {
                        Cmd(0x074D).I(h).I(GetPedRef(t)).I(2000).Run();  // TASK_SHOOT_AT_CHAR: up and shooting
                        e.popUntil = now + 2000;
                    } else {
                        Cmd(0x05C5).I(h).I(1800).Run();  // TASK_DUCK: down again
                        e.popUntil = now + (uint64_t)RandInt(1200, 2200);
                    }
                }
                break;
            case M_PUSH:
                if (now > e.modeUntil) e.mode = M_FIGHT, e.nextCoverMs = now + (uint64_t)RandInt(3000, 6000);
                else if (sees && d > 12.0f) {  // far off: a sprint at the player, the fight once close
                    e.after = t;
                    GoTo(p, e, t->GetPosition() - CVector{0, 0, 1.0f}, 7);
                    continue;
                }
                break;
            default:
                break;
        }
        // the player out of sight: to it along the floor's cells; in sight: the fight (given again now and then, the
        // game's fight ends by itself when its player goes down)
        if ((e.mode == M_FIGHT || e.mode == M_PUSH) && !sees && d > 7.0f) {
            CVector wp;
            if (Waypoint(p, t, wp)) {
                e.after = t;
                GoTo(p, e, wp, d > 12.0f ? 7 : 6);  // far off: sprinting
                continue;
            }
        }
        if (e.mode != M_COVER_HOLD && (t != e.after || e.routing || ((e.mode == M_FIGHT || e.mode == M_PUSH) && now - e.setOnMs > 8000)))
            SetOn(p, e, t);
        // a grenade: 8-26 m, the player in sight, none of its own near the player, the arc clear (StartThrow)
        Squad& sq = g_squads[(size_t)e.squad];
        if (e.role == R_GRENADIER && e.mode != M_FLANK && e.mode != M_COVER_GO && now >= sq.nextGrenadeMs && d > 8.0f && d < 26.0f &&
            e.grenades > 0 && !p->InVehicle()) {
            bool ownNear = false;
            for (CPed* q : g_enemies)
                if (q != p && IsPedValid(q) && q->Health() > 0.0f && (q->GetPosition() - t->GetPosition()).Length() < 6.0f) ownNear = true;
            if (!ownNear && StartThrow(p, e, t)) sq.nextGrenadeMs = now + (uint64_t)RandInt(6000, 9000);
        }
    }
    // the boards come off: one every 1.6 s while someone tears at them, quicker with more at it
    for (size_t i = 0; i < g_bars.size(); ++i) {
        int at = 0;
        for (auto& [q, qe] : g_info)
            if (qe.mode == M_BREACH && qe.tearing && qe.barricade == (int)i && IsPedValid(q) && q->Health() > 0.0f) ++at;
        Bar& b = g_bars[i];
        uint64_t every = (uint64_t)std::max(600, 1600 - 300 * (at - 1));
        if (at && b.boards > 0 && now - b.tearMs > every) {
            b.tearMs = now;
            SetBoards((int)i, b.boards - 1, 0xFF);
        }
    }
    RaiseFallen();
}

int CountLeft() {
    int n = 0;
    for (CPed* p : g_enemies) n += IsPedValid(p) && p->Health() > 0.0f;
    return n;
}

// ---------------------------------------------------------------- the way to the last ones
// When nobody has fallen for 40 s the host marks where the enemies left are (A_MARK, once a second): every game shows
// them on the radar and on the screen - a red mark over each with its distance, seen through the walls, or at the edge
// of the screen an arrow towards it when it is behind or aside.
std::vector<CVector> g_marks;
uint64_t g_lastKillMs = 0;
void SetMarks(const std::vector<CVector>& at) {
    g_marks = at;
    for (int k = 0; k < 8; ++k) {
        if (k < (int)at.size()) radar::SetIcon(9700 + k, at[(size_t)k], 19 /* ENEMYATTACK */);
        else radar::RemoveIcon(9700 + k);
    }
}

void DrawMarks() { ui::DrawWorldMarks(g_marks, 0xE0FF3030); }

void Finish(bool won) {
    g_arena.won = won;
    g_arena.endAt = NowMs() + (won ? 6000 : 1500);
    if (!won && Endless() && g_arena.wave > 1) {  // no end: lost always, $500 a wave held
        int prize = 500 * (g_arena.wave - 1);
        PlayerInfos()[0].money += prize;
        ui::TheChat().AddSystem(Format(TR("Вы продержались %d волн. Награда $%d.", "You held out %d waves. Prize $%d."),
                                       g_arena.wave - 1, prize));
    }
    if (won) {
        int prize = 1000 * g_arena.waves;  // $1000 a wave
        PlayerInfos()[0].money += prize;
        ui::TheChat().AddSystem(Format(TR("Арена пройдена! +$%d", "Arena won! +$%d"), prize));
    } else {
        ui::TheChat().AddSystem(TR("Арена проиграна.", "The arena is lost."));
    }
}

void Clear() {
    if (g_arena.host) ReleaseDead();
    ClearGuards();
    TakeOffOutfit();
    g_enemies.clear();
    g_info.clear();
    g_squads.clear();
    g_bossPed = nullptr;
    g_twinPeds[0] = g_twinPeds[1] = nullptr;
    g_twinCopies[0] = g_twinCopies[1] = nullptr;
    g_twinIds.clear();
    g_flamers.clear();
    g_toRaise.clear();
    ClearVests();
    ClearGas();
    ClearShells();
    g_hasUgl = g_uglLoaded = false;
    g_uglReadyMs = 0;
    g_sawPed = g_officerPed = nullptr;
    g_tagCopies[0] = g_tagCopies[1] = nullptr;
    g_tagIds[0] = g_tagIds[1] = 0;
    RemoveItems();
    ResetCod();
    SetMarks({});
    if (g_arena.boss >= 0) UnloadBoss();
    g_knifeModel = -1;
    g_paths.clear();
    for (int k = 0; k < 16; ++k) radar::RemoveIcon(9700 + k);
}

// all the players in the place voted (M, between the waves): another place, the same wave, the weapons kept
void MapVote(uint8_t by) {  // host
    if (!g_break || !g_arena.nextWaveMs || kPlaceCount < 2) return;
    size_t before = g_mapVotes.size();
    g_mapVotes.insert(by);
    SendVotes();
    if (g_mapVotes.size() > before)
        ui::TheChat().AddSystem(Format(TR("Смена арены: %d из %d (/arena смена)", "Another arena: %d of %d (/arena change)"), g_mapVoteCount, g_voteNeed));
    if (g_mapVoteCount < g_voteNeed) return;
    int place = g_arena.place;
    while (place == g_arena.place) place = RandInt(0, kPlaceCount - 1);
    Send(A_MOVE, [place](Writer& w) { w.U8((uint8_t)place); });
    Relocate(place);
}
// every game: the battle goes on in another place (the wave and the weapons as they are)
void Relocate(int place) {
    uint64_t now = NowMs();
    ClearBars();
    ClearWalls();
    ClearDrops();
    ClearGas();
    RemoveItems();
    DelObj(g_spinHandle);
    g_spinModel = -1;
    DelObj(g_box.handle);
    DelObj(g_box.handle2);
    DelObj(g_ward.handle);
    g_box = BoxState{};
    ShutDoors(false);
    g_arena.place = place;
    CPlayerPed* me = FindPlayerPed(0);
    if (me && me->Health() > 0.0f && g_arena.taken) {
        const Place& pl = kPlaces[place];
        float a = (float)Net().MyId() * 1.25f;
        CVector at = Net().Online() ? pl.pos + CVector{std::cos(a) * 1.2f, std::sin(a) * 1.2f, 0.0f} : pl.pos;
        Teleport(pl.area, at, Rand(0.0f, 360.0f));
        GuardsTo(at, pl.area);
    }
    ShutDoors(true);
    g_votes.clear(), g_mapVotes.clear();
    g_voteCount = g_mapVoteCount = 0;
    g_iVoted = g_iMapVoted = false;
    g_arena.startMs = now;
    if (g_arena.host) {
        ReleaseDead();
        g_paths.clear();
        g_lastSpawns.clear(), g_spawnsNow.clear(), g_lastItemsAt.clear();
        StartGrid();
        g_placed = false;
        g_arena.itemsFor = g_arena.wave + 1;
        g_arena.nextWaveMs = now + PrepMs(true);
    }
    g_breakEndMs = now + PrepMs(true);
    ui::TheChat().AddSystem(Format(TR("Новая арена: %s.", "New arena: %s."),
                                   PlaceName(place).c_str()));
    LOGI("Arena: moved to place %d", place);
}
void HideSaves(bool hide);
void TagCopies() {
    for (int k = 0; k < 2; ++k) {
        if (g_tagIds[k]) {
            int h = Peds::Instance().HandleOf(g_tagIds[k]);
            if (h >= 0) g_tagCopies[k] = GetPed(h), g_tagIds[k] = 0;
            else if (NowMs() > g_tagIdsUntil) g_tagIds[k] = 0;
        }
        if (g_tagCopies[k] && !IsPedValid(g_tagCopies[k])) g_tagCopies[k] = nullptr;
    }
}
void End(bool tell) {
    if (!g_arena.on) return;
    if (tell && g_arena.host) Send(A_END, [](Writer& w) { w.U8(g_arena.won); });
    Clear();
    bool was = g_arena.taken;
    EndSpectate(false);
    g_spectators.clear();
    HideSaves(false);
    ShutDoors(false);
    TakeBack();
    if (CPlayerPed* me = FindPlayerPed(0); me && was) me->Armour() = 0.0f;  // the armour of the battle stays there
    game::challenge::End();
    if (g_modeOn)
        if (CPlayerPed* me2 = FindPlayerPed(0)) Cmd(0x048F).I(GetPedRef(me2)).Run();  // the mode: the save's weapons stay away
    LOGI("Arena over (%s)", g_arena.won ? "won" : "lost");
    g_arena = Arena{};
}

void Start(int faction, int place, int waves) {
    g_arena = Arena{};
    g_arena.on = true;
    g_arena.host = Hosting();
    g_arena.faction = faction;
    g_arena.place = place;
    g_arena.waves = waves;
    g_arena.startMs = NowMs();
    g_arena.nextWaveMs = NowMs() + PrepMs(true);
    ResetCod();
    g_break = false;
    g_barsSolid = true;
    SetBreak(true, g_arena.nextWaveMs);
    game::challenge::Begin();
    if (g_modeOn)
        if (CPlayerPed* me = FindPlayerPed(0)) {  // the mode: nothing of the save, a pistol and full health
            int h = GetPedRef(me);
            Cmd(0x048F).I(h).Run();  // REMOVE_ALL_CHAR_WEAPONS
            Give(h, WEAPON_PISTOL, 68);
            Cmd(0x01B9).I(h).I(WEAPON_PISTOL).Run();
            me->Health() = std::max(me->Health(), 100.0f);
            me->Armour() = 0.0f;
        }
    TakeIn();
    g_lastSpawns.clear(), g_spawnsNow.clear(), g_lastItemsAt.clear();
    if (g_arena.host) {
        StartGrid();  // the floor's cells, over the next frames (the scene's collision is in by then)
        g_arena.itemsFor = 1;
        LoadKnifeModel();
    }
    std::string wavesText = waves ? Format("%d", waves) : std::string(TR("без конца", "no end"));
    ui::TheChat().AddSystem(Format(TR("Арена: %s, %s, волн: %s.", "Arena: %s, %s, %s waves."), FactionName(faction).c_str(),
                                   PlaceName(place).c_str(), wavesText.c_str()));
    ui::TheChat().AddSystem(TR("[E] Ящик, гардероб, доски.  [H] Баррикада.  [Tab] Счёт.", "[E] Box, wardrobe, boards.  [H] Barricade.  [Tab] Score."));
}

void Skip(uint8_t by);
// The arena's game mode: everyone waits in CJ's house (area 3) before the arena takes them in, and comes back there
// after it (a lost one's lobby too); each a little apart from the others
void Stage();
// the arena's game mode, no arena on: whoever is not in CJ's house (back from the hospital after a lost arena) is put
// there once it has stood on its feet for 2 s
uint64_t g_upSince = 0;
void KeepInHouse() {
    CPlayerPed* me = FindPlayerPed(0);
    bool mode = g_modeOn || (Net().Online() && Net().IsHost() && Cfg().gameMode == 2);
    bool up = me && me->Health() > 0.0f && PlayerInfos()[0].playerState == 0 && !me->InVehicle() && !cam::WideScreenOn();
    if (!up) {
        g_upSince = 0;
        return;
    }
    if (!g_upSince) g_upSince = NowMs();
    if (!mode || g_arena.on || g_arena.endAt || NowMs() - g_upSince < 2000) return;
    const CVector kHouse{2496.05f, -1695.24f, 1014.74f};
    if (me->AreaCode() == 3 && (me->GetPosition() - kHouse).Length() < 30.0f) return;
    Stage();
    LOGI("Arena mode: back in CJ's house");
}
void Stage() {
    CPlayerPed* me = FindPlayerPed(0);
    if (g_arena.on || !me || me->Health() <= 0.0f || PlayerInfos()[0].playerState != 0) return;
    const CVector kHouse{2496.05f, -1695.24f, 1014.74f};
    float a = (float)Net().MyId() * 1.1f;
    Teleport(3, kHouse + CVector{std::cos(a) * 0.8f, std::sin(a) * 0.8f, 0.0f}, Rand(0.0f, 360.0f));
}
// a partner's game: the Twins' copies, to be given their weapon skill once they are here (they come a moment later)
void TwinCopies() {
    for (size_t k = 0; k < g_twinIds.size() && k < 2; ++k) {
        if (!g_twinIds[k]) continue;
        int h = Peds::Instance().HandleOf(g_twinIds[k]);
        if (h < 0) {
            if (NowMs() > g_twinIdsUntil) g_twinIds[k] = 0;
            continue;
        }
        Cmd(0x081A).I(h).I(2).Run();  // SET_CHAR_WEAPON_SKILL pro: its copy holds two guns too
        g_twinCopies[k] = GetPed(h);
        g_twinIds[k] = 0;
    }
    for (CPed*& p : g_twinCopies)
        if (p && !IsPedValid(p)) p = nullptr;
    EagleModel(g_arena.host ? g_twinPeds[1] : g_twinCopies[1]);
}

void Handle(uint8_t from, Reader& r) {
    uint8_t op = r.U8();
    if (r.Ok() && op == A_SKIP) {  // a player asks the host to start the next wave now
        if (g_arena.on && g_arena.host) Skip(from);
        return;
    }
    // from any player: its points, and what it asks the host for
    if (r.Ok() && op == A_SCORE) {
        Score sc;
        sc.cash = r.I32();
        sc.kills = r.U16();
        sc.falls = r.U16();
        if (r.Ok()) g_scores[from] = sc;
        return;
    }
    if (r.Ok() && op == A_SHELL && from != Net().HostId()) {  // a partner's own launcher
        Vec3 f = r.Vec(), v = r.Vec();
        if (r.Ok() && g_arena.on) AddShell({f.x, f.y, f.z}, {v.x, v.y, v.z}, nullptr, false);
        return;
    }
    if (r.Ok() && op == A_BOXTAKEN) {
        g_box.offered = false;
        return;
    }
    if (r.Ok() && op == A_SPEC) {
        uint8_t on = r.U8();
        if (r.Ok()) on ? (void)g_spectators.insert(from) : (void)g_spectators.erase(from);
        return;
    }
    if (r.Ok() && op == A_VOTE) {
        if (g_arena.on && g_arena.host) Vote(from);
        return;
    }
    if (r.Ok() && op == A_MAPVOTE) {
        if (g_arena.on && g_arena.host) MapVote(from);
        return;
    }
    if (r.Ok() && op == A_GIVE) {
        Vec3 v = r.Vec();
        uint8_t weapon = r.U8();
        uint16_t ammo = r.U16();
        uint8_t special = r.U8();
        if (r.Ok() && g_arena.on && g_arena.host && weapon > 0 && weapon <= 38) SpawnDrop(D_GUN, {v.x, v.y, v.z}, weapon, ammo, from, special == 1 ? 1 : 0);
        return;
    }
    if (r.Ok() && op == A_TAKE) {
        uint8_t id = r.U8();
        if (r.Ok() && g_arena.on && g_arena.host) TakeDrop(id, from);
        return;
    }
    if (r.Ok() && op == A_BUILD) {
        Vec3 v = r.Vec();
        float heading = r.F32();
        if (r.Ok() && g_arena.on && g_arena.host) BuildBar({v.x, v.y, v.z}, heading, from);
        return;
    }
    if (r.Ok() && (op == A_REPAIR || op == A_BOXUSE)) {
        uint8_t idx = op == A_REPAIR ? r.U8() : 0;
        if (!r.Ok() || !g_arena.on || !g_arena.host) return;
        if (op == A_REPAIR && idx < g_bars.size() && g_bars[idx].boards < kBoards) SetBoards(idx, g_bars[idx].boards + 1, from);
        if (op == A_BOXUSE) UseBox(from);
        return;
    }
    if (!r.Ok() || !core::IsPlayerReady() || from != Net().HostId()) return;
    // the arena's game mode: these come while no arena is on
    if (op == A_STAGE) {
        g_modeOn = true;
        g_lobby = false;
        Stage();
        WearChosen();
        return;
    }
    if (op == A_LOBBY) {
        uint8_t open = r.U8();
        if (r.Ok() && open) OpenLobby(true);
        return;
    }
    if (op == A_START) {
        uint8_t f = r.U8(), p = r.U8(), w = r.U8();
        if (!r.Ok() || f >= kFactionCount || p >= kPlaceCount || w > 10) return;  // 0: no end
        if (g_arena.on) End(false);
        if (g_lobby) OpenLobby(false);
        Start(f, p, w);
    } else if (!g_arena.on) {
        return;
    } else if (op == A_WAVE) {
        uint8_t w = r.U8(), n = r.U8();
        if (!r.Ok()) return;
        g_arena.wave = w;
        g_arena.left = n;
        g_killed.clear();
        SetBreak(false, 0);
        Rejoin();
        OutfitWaveStart();
    } else if (op == A_LEFT) {
        uint8_t n = r.U8();
        if (r.Ok()) g_arena.left = n;
    } else if (op == A_BOSS) {
        uint8_t b = r.U8();
        if (!r.Ok() || b >= kBossCount) return;
        g_arena.boss = b;
        LoadBoss(b);  // its copy here has its look
        ui::TheChat().AddSystem(Format(TR("Волна с боссом! Босс: %s.", "The boss wave! The boss: %s."), BossName(b).c_str()));
    } else if (op == A_ITEMS) {
        uint8_t n = r.U8();
        std::vector<Item> items;
        for (int k = 0; k < n; ++k) {
            Item it;
            it.model = r.U16();
            it.ammo = r.U16();
            Vec3 v = r.Vec();
            it.pos = {v.x, v.y, v.z};
            if (it.model == MODEL_ARMOUR || (it.model >= 321 && it.model <= 372)) items.push_back(it);
        }
        if (r.Ok()) MakeItems(items);
    } else if (op == A_MARK) {
        uint8_t n = r.U8();
        std::vector<CVector> at;
        for (int k = 0; k < n && k < 8; ++k) {
            Vec3 v = r.Vec();
            at.push_back({v.x, v.y, v.z});
        }
        if (r.Ok()) SetMarks(at);
    } else if (op == A_PREP) {
        uint8_t secs = r.U8();
        if (r.Ok()) {
            SetBreak(true, NowMs() + (uint64_t)secs * 1000);
        }
    } else if (op == A_BARS) {
        uint8_t n = r.U8();
        std::vector<Bar> bars;
        for (int k = 0; k < n && k < 16; ++k) {
            Bar b;
            Vec3 v = r.Vec(), sp = r.Vec();
            b.pos = {v.x, v.y, v.z};
            b.spawn = {sp.x, sp.y, sp.z};
            b.heading = r.F32();
            b.boards = std::min<int>(r.U8(), kBoards);
            b.built = r.U8() != 0;
            bars.push_back(b);
        }
        if (!r.Ok()) return;
        bool same = bars.size() == g_bars.size();
        for (size_t i = 0; same && i < bars.size(); ++i) same = (bars[i].pos - g_bars[i].pos).Length() < 0.1f;
        if (!same) {
            ClearBars();
            g_bars = bars;
        }
        for (size_t i = 0; i < g_bars.size(); ++i) g_bars[i].boards = bars[i].boards, ShowBar(g_bars[i]);
    } else if (op == A_VOTES) {
        uint8_t c = r.U8(), n = r.U8(), m = r.U8();
        if (!r.Ok()) return;
        if (m > g_mapVoteCount)
            ui::TheChat().AddSystem(Format(TR("Смена арены: %d из %d (/arena смена)", "Another arena: %d of %d (/arena change)"), (int)m, std::max<int>(1, n)));
        g_voteCount = c, g_voteNeed = std::max<int>(1, n), g_mapVoteCount = m;
    } else if (op == A_HIT) {
        KnockDown();  // the Butcher's charge ran into this player
    } else if (op == A_SHELL) {
        Vec3 f = r.Vec(), v = r.Vec();
        if (r.Ok()) AddShell({f.x, f.y, f.z}, {v.x, v.y, v.z}, nullptr, false);  // a copy to see: the blast comes from its game
    } else if (op == A_HEALGAS) {
        Vec3 f = r.Vec(), v = r.Vec();
        if (r.Ok()) AddGas({f.x, f.y, f.z}, {v.x, v.y, v.z});
    } else if (op == A_BOSSNAME) {
        uint8_t kind = r.U8();
        uint16_t id = r.U16();
        if (r.Ok() && kind < 2) g_tagIds[kind] = id, g_tagCopies[kind] = nullptr, g_tagIdsUntil = NowMs() + 15000;
    } else if (op == A_MOVE) {
        uint8_t place = r.U8();
        if (r.Ok() && place < kPlaceCount) Relocate(place);
    } else if (op == A_FLAMER) {
        uint16_t id = r.U16();
        if (r.Ok()) g_vestIds.push_back(id), g_vestIdsUntil = NowMs() + 15000;
    } else if (op == A_WALLS) {
        uint8_t n = r.U8();
        std::vector<Wall> walls;
        for (int k = 0; k < n && k < 16; ++k) {
            Wall w;
            w.gun = std::min<int>(r.U8(), kWallGunCount - 1);
            Vec3 v = r.Vec(), st = r.Vec();
            w.pos = {v.x, v.y, v.z};
            w.stand = {st.x, st.y, st.z};
            w.heading = r.F32();
            walls.push_back(w);
        }
        if (!r.Ok()) return;
        ClearWalls();
        g_walls = walls;
        for (Wall& w : g_walls) ShowWall(w);
    } else if (op == A_DROP) {
        Drop d;
        d.id = r.U8();
        d.type = r.U8();
        Vec3 v = r.Vec();
        d.pos = {v.x, v.y, v.z};
        d.weapon = r.U8();
        d.ammo = r.U16();
        d.owner = r.U8();
        d.special = r.U8();
        if (r.Ok() && d.type <= D_CASH && d.weapon <= 46) AddDrop(d);
    } else if (op == A_TAKEN) {
        uint8_t id = r.U8(), by = r.U8();
        if (!r.Ok()) return;
        for (size_t i = 0; i < g_drops.size(); ++i)
            if (g_drops[i].id == id) {
                Drop d = g_drops[i];
                DelObj(g_drops[i].handle);
                g_drops.erase(g_drops.begin() + (long)i);
                ApplyDrop(d, by);
                break;
            }
    } else if (op == A_BOARD) {
        uint8_t i = r.U8(), boards = r.U8(), by = r.U8();
        if (r.Ok()) SetBoards(i, boards, by);
    } else if (op == A_BOX) {
        uint8_t placed = r.U8();
        Vec3 v = r.Vec();
        float heading = r.F32();
        if (!r.Ok()) return;
        g_box.placed = placed != 0;
        g_box.pos = {v.x, v.y, v.z};
        g_box.heading = heading;
        g_box.rolling = false;
        ShowBox();
    } else if (op == A_BOXROLL) {
        uint8_t by = r.U8(), gun = r.U8(), teddy = r.U8();
        if (r.Ok() && gun < sizeof(kBoxGuns) / sizeof(kBoxGuns[0])) StartRoll(by, gun, teddy != 0);
    } else if (op == A_TWINS) {
        g_twinIds.clear();
        g_twinCopies[0] = g_twinCopies[1] = nullptr;
        for (int k = 0; k < 2; ++k) {
            uint16_t id = r.U16();
            g_twinIds.push_back(r.Ok() ? id : 0);
        }
        g_twinIdsUntil = NowMs() + 15000;
    } else if (op == A_END) {
        uint8_t won = r.U8();
        if (!r.Ok()) return;
        Finish(won != 0);
    }
}

// The fires left on the floor (molotovs, the flamethrower) go out faster in the arena: 2.5 s at most from now, and they
// spread no further (CFire, 0x28 bytes in gFireManager: m_pEntityTarget +0x10, m_nTimeToBurn +0x18, generations +0x20)
void ShortFires() {
    uint32_t now = TimeMs();
    for (int i = 0; i < 60; ++i) {
        uintptr_t f = 0xB71F80 + (uintptr_t)i * 0x28;
        if (!(Ref<uint8_t>(f) & 1) || Ref<void*>(f + 0x10)) continue;  // out, or one on a body (it goes with it)
        uint32_t& until = Ref<uint32_t>(f + 0x18);
        if (until > now + 2500) until = now + 2500;
        Ref<int8_t>(f + 0x20) = 0;
    }
}

// The houses' save disks (their pickups) are gone while an arena is on: no 3D disk, no saving in the middle of a
// battle. CPickups::aPickUps (620 x 0x20): m_pObject +0x04, m_nModelIndex +0x18, m_nPickupType +0x1C; the disk's model
// is MI_PICKUP_SAVEGAME. Hidden ones get PICKUP_NONE and an invisible object, put back as they were afterwards.
void HideSaves(bool hide) {
    constexpr uintptr_t kPickups = 0x9788C0;
    int16_t saveModel = Ref<int16_t>(0x8CD5E4);
    if (!hide) {
        for (auto& [i, type] : g_hiddenSaves) {
            uintptr_t pk = kPickups + (uintptr_t)i * 0x20;
            Ref<uint8_t>(pk + 0x1C) = type;
            if (auto* o = Ref<CEntity*>(pk + 0x04)) o->Flags() |= 0x80;  // bIsVisible
        }
        g_hiddenSaves.clear();
        return;
    }
    if (saveModel <= 0) return;
    for (int i = 0; i < 620; ++i) {
        uintptr_t pk = kPickups + (uintptr_t)i * 0x20;
        if (Ref<int16_t>(pk + 0x18) != saveModel || Ref<uint8_t>(pk + 0x1C) == 0) continue;
        g_hiddenSaves.push_back({i, Ref<uint8_t>(pk + 0x1C)});
        Ref<uint8_t>(pk + 0x1C) = 0;  // PICKUP_NONE
        if (auto* o = Ref<CEntity*>(pk + 0x04)) o->Flags() &= ~0x80u;
    }
}

// the barricades solid in a wave, open between the waves; the vote shown and counted
int Voters() {  // the live players in the place
    int n = 0, area = kPlaces[g_arena.place].area;
    if (CPlayerPed* me = FindPlayerPed(0); me && !g_spectating && me->Health() > 0.0f && me->AreaCode() == area) ++n;
    for (auto& [id, rp] : Players::Instance().All())
        if (rp.hasState && !Spectator(id) && !(rp.state.flags & OnFootState::F_DEAD) && rp.state.area == area) ++n;
    return std::max(1, n);
}
void SendVotes() {
    g_voteCount = (int)g_votes.size();
    g_mapVoteCount = (int)g_mapVotes.size();
    g_voteNeed = Voters();
    Send(A_VOTES, [](Writer& w) {
        w.U8((uint8_t)g_voteCount);
        w.U8((uint8_t)g_voteNeed);
        w.U8((uint8_t)g_mapVoteCount);
    });
}
void Vote(uint8_t by) {  // host
    if (!g_break || !g_arena.nextWaveMs) return;
    g_votes.insert(by);
    SendVotes();
    if (g_voteCount >= g_voteNeed) Skip(by);
}
void SetBreak(bool on, uint64_t endMs) {
    if (on && !g_break) g_votes.clear(), g_mapVotes.clear(), g_voteCount = g_mapVoteCount = 0, g_iVoted = g_iMapVoted = false;
    g_break = on;
    if (on) g_breakEndMs = endMs;
    if (g_barsSolid == !on) return;
    g_barsSolid = !on;
    for (Bar& b : g_bars)
        if (b.handle >= 0 && GetObject(b.handle)) Cmd(0x0382).I(b.handle).I(g_barsSolid ? 1 : 0).Run();  // SET_OBJECT_COLLISION
}

void Update() {
    uint64_t now = NowMs();
    ShortFires();
    UpdateVests();
    if (g_arena.host) {
        bool brk = g_arena.wave == 0 || g_arena.nextWaveMs != 0;
        if (brk != g_break) SetBreak(brk, g_arena.nextWaveMs);
        if (brk && g_arena.nextWaveMs) g_breakEndMs = g_arena.nextWaveMs;
        static uint64_t votersMs = 0;
        if (brk && now - votersMs > 2000 && Voters() != g_voteNeed) votersMs = now, SendVotes();  // someone came or fell
    }
    static uint64_t savesMs = 0;
    if (now - savesMs > 1000) savesMs = now, HideSaves(true);
    game::challenge::Tick();
    if (g_arena.endAt) {
        if (now >= g_arena.endAt) End(false);
        return;
    }
    // every game: the box, what is near this player, its death (back at the next wave, 10% of its points gone)
    UpdateBox(now);
    SpinBox(now);
    UpdateDrops(now);
    UpdateGas(now);
    UpdateShells(now);
    static uint64_t regenMs = 0;  // the medic's outfit: 2 health a second
    if (g_outfit >= 0 && kOutfits[g_outfit].kind == O_REGEN && now - regenMs > 1000 && !g_spectating) {
        regenMs = now;
        CPlayerPed* me = FindPlayerPed(0);
        float top = std::max<float>(PlayerInfos()[0].maxHealth, 100.0f);
        if (me && me->Health() > 0.0f && me->Health() < top) me->Health() = std::min(top, me->Health() + 2.0f);
    }
    if (g_spectating) SpectateFrame(now);
    if (g_uglReadyMs && now >= g_uglReadyMs) g_uglReadyMs = 0, g_uglLoaded = true;
    TagCopies();
    if (g_arena.host) {
        SawFrame(now);
        KnifeFrame();
    }
    CheckSmashed();
    FindNear();
    TwinCopies();
    for (auto it = g_popups.begin(); it != g_popups.end();) it = now - it->at > 1500 ? g_popups.erase(it) : std::next(it);
    if (CPlayerPed* me = FindPlayerPed(0); me && g_arena.taken) {
        bool alive = me->Health() > 0.0f && PlayerInfos()[0].playerState == 0;
        if (!alive) g_aliveSince = 0;
        else if (!g_aliveSince) g_aliveSince = now;
        if (g_dead && g_arena.wave > g_deadWave) Rejoin();  // a new wave since its death: back in
        if (me->Health() <= 0.0f && !g_dead) {
            g_dead = true;
            g_hasUgl = g_uglLoaded = false;  // the AK goes with the death
            g_uglReadyMs = 0;
            g_deadWave = g_arena.wave;
            AddPoints(-g_points / 10);
        }
    }
    if (!g_arena.host) return;
    if (!g_gridDone && now - g_arena.startMs > 1500) GridStep();
    if (!g_placed && (g_gridDone || now - g_arena.startMs > 6000)) {
        g_placed = true;
        ClearBars();  // no barricades of the mod's own: the players build them (H), the host keeps its own (/arena баррикада)
        PlaceBox();
        bool own = LoadSpots();  // the host's own spots for this place (/arena ящик, баррикада)
        ClearWalls();  // no weapons on the walls any more
        if (own) LOGI("Arena: the host's own spots used");
    }
    // the special ones dropped what they had: ammo or health for everyone (the boss both)
    for (auto& [q, qe] : g_info) {
        if (qe.counted || !IsPedValid(q) || q->Health() > 0.0f) continue;
        qe.counted = true;
        DropVest(qe);
        CVector at = q->GetPosition() + CVector{0, 0, -0.3f};
        // its money falls where it lies: more for the special ones
        int cash = qe.role == R_BOSS ? 500 : qe.role == R_SAW || qe.role == R_OFFICER ? 300 : qe.role == R_TWIN ? 250
                 : qe.role == R_FLAMER ? 200 : qe.role == R_SNIPER ? 100 : qe.role == R_KNIFE || qe.role == R_MEDIC ? 80 : 50;
        cash += RandInt(0, 4) * 5;
        SpawnDrop(D_CASH, at + CVector{Rand(-0.4f, 0.4f), Rand(-0.4f, 0.4f), 0}, 0, (uint16_t)cash);
        bool special = qe.role == R_FLAMER || qe.role == R_TWIN || qe.role == R_BOSS || qe.role == R_SAW || qe.role == R_OFFICER;
        if (qe.role == R_MEDIC) special = RandInt(0, 99) < 35;  // a medic: now and then
        if (!special) {
            if (RandInt(0, 99) < 5) SpawnDrop(D_SMALL, at);  // a common enemy: now and then a few clips, never its gun
            continue;
        }
        if (qe.role == R_BOSS && g_arena.boss == kTBone) SpawnDrop(D_GUN, at + CVector{0, 0.7f, 0}, 30, 300, 0xFF, 1);  // his AK
        if (qe.role == R_BOSS) {
            SpawnDrop(D_AMMO, at + CVector{0.6f, 0, 0});
            SpawnDrop(D_HEALTH, at - CVector{0.6f, 0, 0});
        } else {
            SpawnDrop(RandInt(0, 1) ? D_AMMO : D_HEALTH, at);
        }
    }
    if (g_arena.itemsFor && (g_gridDone || now - g_arena.startMs > 6000)) {
        PlaceItems(g_arena.itemsFor);
        g_arena.itemsFor = 0;
    }
    int left = CountLeft();
    if (left != g_arena.left) {
        if (left < g_arena.left) g_lastKillMs = now;
        g_arena.left = left;
        Send(A_LEFT, [left](Writer& w) { w.U8((uint8_t)std::min(left, 255)); });
    }
    Throws();
    static uint64_t tacticsMs = 0;
    if (now - tacticsMs > 500) {
        tacticsMs = now;
        Tactics();
    }
    // nobody down for 40 s (the ones left are hiding, or stuck behind a wall): everyone is shown where they are
    static uint64_t markMs = 0;
    bool hint = g_arena.wave && left > 0 && !g_arena.nextWaveMs && now - std::max(g_arena.waveMs, g_lastKillMs) > 40000;
    if (hint && now - markMs > 1000) {
        markMs = now;
        std::vector<CVector> at;
        for (CPed* p : g_enemies)
            if (at.size() < 8 && IsPedValid(p) && p->Health() > 0.0f) at.push_back(p->GetPosition());
        SetMarks(at);
        Send(A_MARK, [&](Writer& w) {
            w.U8((uint8_t)at.size());
            for (const CVector& v : at) w.Vec({v.x, v.y, v.z});
        });
    } else if (!hint && !g_marks.empty()) {
        SetMarks({});
        Send(A_MARK, [](Writer& w) { w.U8(0); });
    }
    // everyone dead or out of the place: lost
    int area = kPlaces[g_arena.place].area;
    bool anyIn = false;
    if (CPlayerPed* me = FindPlayerPed(0)) anyIn |= !g_spectating && me->Health() > 0.0f && me->AreaCode() == area;
    for (auto& [id, rp] : Players::Instance().All())
        if (rp.hasState && !Spectator(id))
            anyIn |= !(rp.state.flags & OnFootState::F_DEAD) && rp.state.area == area && now - rp.stateRecvMs < 5000;
    if (!anyIn && now - g_arena.startMs > 8000) {
        if (!g_arena.outSince) g_arena.outSince = now;
        if (now - g_arena.outSince > 4000) {
            Finish(false);
            Send(A_END, [](Writer& w) { w.U8(0); });
            g_arena.endAt = now;  // ends next frame
            return;
        }
    } else {
        g_arena.outSince = 0;
    }
    if (g_arena.wave == 0 || left == 0) {
        if (!Endless() && g_arena.wave >= g_arena.waves) {
            Finish(true);
            Send(A_END, [](Writer& w) { w.U8(1); });
            return;
        }
        if (!g_arena.nextWaveMs) {
            int secs = (int)(PrepMs(false) / 1000);
            g_arena.nextWaveMs = now + PrepMs(false);
            g_arena.itemsFor = g_arena.wave + 1;
            Send(A_PREP, [secs](Writer& w) { w.U8((uint8_t)secs); });
        }
        if (now >= g_arena.nextWaveMs) {
            g_arena.nextWaveMs = 0;
            SpawnWave();
        }
    }
}

// /skip: the wait before the next wave ends now (the host's game decides; another player asks it)
void Skip(uint8_t by) {
    if (!g_arena.on || g_arena.endAt || !g_arena.nextWaveMs || NowMs() >= g_arena.nextWaveMs) return;
    g_arena.nextWaveMs = NowMs();
    std::string who = by == Net().MyId() || !Net().Online() ? std::string(TR("Вы", "You")) : Net().PeerName(by);
    std::string line = Format(TR("%s: ожидание пропущено, волна начинается.", "%s skipped the wait: the wave begins."), who.c_str());
    ui::TheChat().AddSystem(line);
    LOGI("Arena: the wait skipped by player %d", by);
}

void List() {
    std::string f, p;
    for (int i = 0; i < kFactionCount; ++i) f += (i ? ", " : "") + std::string(kFactions[i].keys[0][Lang()]);
    for (int i = 0; i < kPlaceCount; ++i) p += (i ? ", " : "") + std::string(kPlaces[i].keys[0][Lang()]);
    ui::TheChat().AddSystem(TR("/arena <враги> <место|рандом> <волны 1-10|бесконечно>, например: /arena якудза рандом 5",
                               "/arena <enemies> <place|random> <waves 1-10|endless>, e.g. /arena yakuza random 5"));
    ui::TheChat().AddSystem(Format(TR("Враги: %s", "Enemies: %s"), f.c_str()));
    ui::TheChat().AddSystem(Format(TR("Места: %s", "Places: %s"), p.c_str()));
}

// the points (this player's bigger, the others' under it, bottom right), the pop-ups of what was gained, and over the
// barricades their boards, over the box its price or what it turns over
void Label(const CVector& at, const std::string& text, uint32_t color, float maxDist = 25.0f, bool throughWalls = false) {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || (me->GetPosition() - at).Length() > maxDist) return;
    if (!throughWalls && !Clear(cam::Source(), at)) return;  // only what the player sees
    float x, y, depth;
    if (!WorldToScreen(at, x, y, depth)) return;
    ui::Gfx().Text(ui::FONT_SMALL, x, y, text, color, ui::TEXT_CENTER | ui::TEXT_OUTLINE);
}
// Tab held: everyone's money, kills and falls
void DrawScoreboard() {
    if (!IsForeground() || !(GetAsyncKeyState(VK_TAB) & 0x8000) || ui::TheChat().IsOpen()) return;
    auto& g = ui::Gfx();
    float s = g.Scale();
    struct Row {
        std::string name;
        Score sc;
        bool me, out;
    };
    std::vector<Row> rows;
    rows.push_back({Net().Online() ? Cfg().nickname : std::string(TR("Вы", "You")), {g_points, g_kills, g_falls}, true, g_spectating});
    for (auto& [id, sc] : g_scores)
        if (Net().Online() && Net().Peers().count(id)) rows.push_back({Net().PeerName(id), sc, false, Spectator(id)});
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.sc.kills > b.sc.kills; });
    float lh = g.LineHeight(ui::FONT_NORMAL) + 8 * s;
    float w = 620 * s, h = lh * (float)(rows.size() + 1) + 24 * s;
    float x = (g.Width() - w) * 0.5f, y = g.Height() * 0.25f;
    g.Rect(x, y, w, h, 0xD0000000);
    float c1 = x + 20 * s, c2 = x + w * 0.55f, c3 = x + w * 0.72f, c4 = x + w * 0.88f;
    float ty = y + 12 * s;
    uint32_t head = 0xFFB0B0B0;
    g.Text(ui::FONT_SMALL, c1, ty, TR("Игрок", "Player"), head, ui::TEXT_SHADOW);
    g.Text(ui::FONT_SMALL, c2, ty, TR("Деньги", "Money"), head, ui::TEXT_SHADOW);
    g.Text(ui::FONT_SMALL, c3, ty, TR("Убийства", "Kills"), head, ui::TEXT_SHADOW);
    g.Text(ui::FONT_SMALL, c4, ty, TR("Выбыл", "Down"), head, ui::TEXT_SHADOW);
    ty += lh;
    for (const Row& r : rows) {
        uint32_t c = r.out ? 0xFF808080 : r.me ? 0xFFFFE060 : 0xFFFFFFFF;
        g.Text(ui::FONT_NORMAL, c1, ty, r.name, c, ui::TEXT_SHADOW);
        g.Text(ui::FONT_NORMAL, c2, ty, Format("$%d", r.sc.cash), c, ui::TEXT_SHADOW);
        g.Text(ui::FONT_NORMAL, c3, ty, Format("%d", r.sc.kills), c, ui::TEXT_SHADOW);
        g.Text(ui::FONT_NORMAL, c4, ty, Format("%d", r.sc.falls), c, ui::TEXT_SHADOW);
        ty += lh;
    }
}

// The screen: the money (bottom right) and what it just got, the break's seconds and votes, the launcher when the AK is
// in hand; over the world only the Officer's mark and, near the box, its price or what it turns over
void DrawCod() {
    auto& g = ui::Gfx();
    float s = g.Scale();
    uint64_t now = NowMs();
    if (g_break && !g_arena.endAt && g_breakEndMs > now) {  // the break: one line on the right
        std::string t = Format(TR("Пауза %d с   [Y] пропустить %d/%d", "Break %d s   [Y] skip %d/%d"), (int)((g_breakEndMs - now + 999) / 1000),
                               g_voteCount, g_voteNeed);
        float tw = g.TextWidth(ui::FONT_NORMAL, t) + 28 * s;
        float px = g.Width() - tw - 24 * s, py = g.Height() * 0.36f;
        g.Rect(px, py, tw, g.LineHeight(ui::FONT_NORMAL) + 16 * s, 0xA0000000);
        g.Text(ui::FONT_NORMAL, px + 14 * s, py + 8 * s, t, 0xFFFFFFFF, ui::TEXT_SHADOW);
    }
    float x = g.Width() - 40 * s, y = g.Height() * 0.62f;
    g.Text(ui::FONT_BIG, x, y, Format("$%d", g_points), 0xFF70E070, ui::TEXT_RIGHT | ui::TEXT_OUTLINE);
    for (const Popup& p : g_popups) {
        float t = (float)(now - p.at) / 1500.0f;
        g.Text(ui::FONT_NORMAL, x - 160 * s, y - t * 40 * s, p.text, ui::WithAlpha(p.color, 1.0f - t), ui::TEXT_RIGHT | ui::TEXT_OUTLINE);
    }
    y += g.LineHeight(ui::FONT_BIG);
    if (CPlayerPed* me = FindPlayerPed(0); g_hasUgl && me && me->ActiveWeapon().type == 30) {  // only with the AK in hand
        std::string t = g_uglReadyMs  ? std::string(TR("Подствольник: заряжается", "Launcher: loading"))
                        : g_uglLoaded ? std::string(TR("Подствольник [N]", "Launcher [N]"))
                                      : Format(TR("Подствольник пуст [N] гранат %d", "Launcher empty [N] grenades %d"), GrenadesHeld(me));
        g.Text(ui::FONT_SMALL, x, y, t, g_uglLoaded ? 0xFF80FF80 : 0xFFFFB060, ui::TEXT_RIGHT | ui::TEXT_OUTLINE);
    }
    CPed* officer = g_arena.host ? g_officerPed : g_tagCopies[1];
    if (officer && IsPedValid(officer) && officer->Health() > 0.0f) ui::DrawWorldMarks({officer->GetPosition()}, 0xE0FFB040);  // where he hides
    if (g_box.placed && g_box.handle >= 0) {
        if (g_box.rolling || g_box.offered) {
            int n = (int)(sizeof(kBoxGuns) / sizeof(kBoxGuns[0]));
            int shown = g_box.offered || now + 400 >= g_box.rollEnd ? g_box.gun : (int)((now / 120) % (uint64_t)n);
            std::string t = g_box.teddy && !g_box.rolling ? std::string() : std::string(BoxGunName(shown));
            if (!t.empty()) Label(g_box.pos + CVector{0, 0, g_box.height + 0.9f}, t, 0xFF60D0FF, 20.0f, true);
        } else {
            Label(g_box.pos + CVector{0, 0, g_box.height + 0.5f}, Format("$%d", kBoxCost), 0xFF60D0FF, 8.0f);
        }
    }
    if (g_ward.handle >= 0) Label(g_ward.pos + CVector{0, 0, 1.2f}, Format("$%d", kWardCost), 0xFFE0A0FF, 8.0f);
    DrawScoreboard();
}

// the host starts one (the command, or the arena's game mode): every game hears of it
void Launch(int faction, int place, int waves) {
    if (g_lobby) OpenLobby(false);
    Send(A_START, [&](Writer& w) {
        w.U8((uint8_t)faction);
        w.U8((uint8_t)place);
        w.U8((uint8_t)waves);
    });
    Start(faction, place, waves);
    LOGI("Arena started: faction %d, place %d, %d waves", faction, place, waves);
}

// The arena's game mode (F9: "Режим: Арена"): once the host's game is up (out of the lobby), an arena with the chosen
// enemies, place and waves starts by itself after 15 s, and the next one 20 s after each ends. The host plays the
// character it picked in the lobby too (the partners get theirs as always).
uint64_t g_modeReadyMs = 0, g_modeNextMs = 0;
// After a lost arena in this mode everyone is back in the lobby (the F9 window opens with it): the host picks the
// enemies, the place and the waves again and starts the next one, each player may pick another character there
void WearPicked() {
    if (!skin::Picked()) return;
    int count = 0;
    const skin::Choice* list = skin::Choices(count);
    int k = std::clamp(skin::ChosenIndex(), 0, count - 1);
    skin::Wear(list[k].model);
}
// After a lost arena in this mode everyone is back in the co-op lobby (the F9 window, as before the game): ready / the
// host's start, the characters, the next arena's choices; the host's start goes on in the same game (flow::ReturnToLobby)
void OpenLobby(bool open) {
    g_lobby = open;
    if (open) {
        Stage();  // whoever is not there (back from the hospital) waits in CJ's house
        if (Net().IsHost()) flow::ReturnToLobby();
        ui::TheChat().AddSystem(TR("Арена проиграна.", "The arena is lost."));
    }
}
// this player becomes the character it picked in the lobby (not the one of the save or the profile)
void WearChosen() {
    if (g_modeOn || (Net().Online() && Net().IsHost() && Cfg().gameMode == 2)) {  // the arena mode: CJ, the wardrobe dresses him
        if (g_outfit < 0) skin::Wear(0);
        return;
    }
    int count = 0;
    const skin::Choice* list = skin::Choices(count);
    int k = std::clamp(skin::ChosenIndex(), 0, count - 1);
    skin::Wear(list[k].model);
}
// The arena mode always plays a new game: its prologue ("intro", a mission) ends as it begins, everything it took given
// back, its people gone, and the partners get the world then (as the gang war does)
bool EndPrologue() {
    bool found = false;
    for (void* sc = Ref<void*>(0xA8B42C); sc; sc = Field<void*>(sc, 0x00))  // CTheScripts::pActiveScripts
        if (Field<uint8_t>(sc, 0xDC) && !_strnicmp((const char*)sc + 0x08, "intro", 8)) found = true;  // m_bIsMission, m_szName
    if (!found) return false;
    Cmd(0x0459).S8("intro").Run();  // TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME
    CPoolHeader* pool = PedPool();
    for (int i = 0; pool && i < pool->size; ++i) {
        if (pool->byteMap[i] < 0) continue;
        auto* q = (CPed*)(pool->objects + (size_t)i * kPedPoolElemSize);
        if (q->IsPlayer() || q->CreatedBy() != 2 /* PED_MISSION */ || Players::Instance().FindByPed(q) || Peds::Instance().Find(q)) continue;
        Cmd(0x009B).I(GetPedRef(q)).Run();  // DELETE_CHAR: the prologue's people
    }
    for (int slot = 1; slot <= 4; ++slot) Cmd(0x0296).I(slot).Run();  // UNLOAD_SPECIAL_CHARACTER
    Ref<int32_t>(0xA49960 + (uintptr_t)Ref<int32_t>(0xA476AC)) = 0;  // the script's ONMISSION
    Cmd(0x01B4).I(0).I(1).Run();    // SET_PLAYER_CONTROL
    Cmd(0x02A3).I(0).Run();         // SWITCH_WIDESCREEN off
    Cmd(0x02EB).Run();              // RESTORE_CAMERA_JUMPCUT
    Cmd(0x016A).I(500).I(1).Run();  // DO_FADE in
    Cmd(0x0581).I(1).Run();         // DISPLAY_RADAR
    Cmd(0x0826).I(1).Run();         // DISPLAY_HUD
    flow::RequestFreshWorld();      // the partners waited for the prologue's end
    LOGI("Arena mode: the new game's prologue left out");
    return true;
}

void GameMode() {
    if (Cfg().gameMode != 2 || !Net().Online() || !Net().IsHost() || !core::IsPlayerReady() || flow::InLobby()) {
        g_modeReadyMs = 0;
        return;
    }
    uint64_t now = NowMs();
    if (EndPrologue()) {
        g_modeReadyMs = 0;
        return;
    }
    if (!g_modeReadyMs) {
        g_modeReadyMs = now;
        g_lobby = false;
        g_modeOn = true;
        Send(A_STAGE);  // everyone into CJ's house first, as the character picked in the lobby
        Stage();
        WearChosen();
        g_modeNextMs = now + 15000;
        ui::TheChat().AddSystem(TR("Арена через 15 с", "The arena in 15 s"));
    }
    if (g_arena.on) {
        g_modeNextMs = now + 20000;  // the next one 20 s after this one, won or lost (everyone waits in CJ's house)
        g_modeLost = g_arena.endAt && !g_arena.won;
        return;
    }
    if (g_modeLost) {
        g_modeLost = false;
        Send(A_STAGE);
        Stage();
        ui::TheChat().AddSystem(TR("Новая арена через 20 с", "A new arena in 20 s"));
    }
    CPlayerPed* me = FindPlayerPed(0);
    if (now < g_modeNextMs || !me || me->Health() <= 0.0f || PlayerInfos()[0].playerState != 0) return;
    int faction = std::clamp(Cfg().arenaFaction, 0, kFactionCount - 1);
    int place = Cfg().arenaPlace >= 0 && Cfg().arenaPlace < kPlaceCount ? Cfg().arenaPlace : RandInt(0, kPlaceCount - 1);
    Launch(faction, place, std::clamp(Cfg().arenaWaves, 0, 10));
}

// An empty gun stays in the arena: the weapon wheel skips a gun without ammo (CWeapon::HasWeaponAmmoToBeUsed), so
// the player lost it from the hand for good. For this player's guns in the arena it counts as usable: it stays in its
// slot and can be taken up, empty, till ammo for it is found (a drop, the box, a wall, a team mate's gun).
using HasAmmoFn = bool(__fastcall*)(CWeapon*, void*);
HasAmmoFn g_origHasAmmo = nullptr;
bool __fastcall HookHasAmmo(CWeapon* w, void* edx) {
    if (g_arena.on && g_arena.taken && w->type >= 22 && w->type <= 38) {
        CPlayerPed* me = FindPlayerPed(0);
        if (me && w >= me->Weapons() && w < me->Weapons() + NUM_WEAPON_SLOTS) return true;
    }
    return g_origHasAmmo(w, edx);
}

// ---- CJ's house in the arena mode (where everyone waits): no markers in it (the scripts' arrows and cylinders, the
// wardrobe's, the doors' yellow cones), its doors shut (nobody walks out of it) and its save disk gone
const CVector kHouse{2496.05f, -1695.24f, 1014.74f};
bool InHouse() {
    if (!(g_modeOn || (Net().Online() && Net().IsHost() && Cfg().gameMode == 2)) || g_arena.on) return false;
    CPlayerPed* me = core::IsPlayerReady() ? FindPlayerPed(0) : nullptr;
    return me && me->AreaCode() == 3 && (me->GetPosition() - kHouse).Length() < 60.0f;
}
using PlaceMarkerFn = void*(__cdecl*)(uint32_t, uint32_t, CVector*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, float, int32_t,
                                      CVector, uint32_t);
PlaceMarkerFn g_origPlaceMarker = nullptr;
void* __cdecl HookPlaceMarker(uint32_t id, uint32_t type, CVector* pos, float size, uint32_t r, uint32_t g, uint32_t b, uint32_t a, uint32_t pulse,
                              float frac, int32_t rot, CVector normal, uint32_t zCheck) {
    if (InHouse()) return nullptr;
    return g_origPlaceMarker(id, type, pos, size, r, g, b, a, pulse, frac, rot, normal, zCheck);
}
using PlaceConeFn = void(__cdecl*)(uint32_t, CVector*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, float, int32_t, uint32_t);
PlaceConeFn g_origPlaceCone = nullptr;
void __cdecl HookPlaceCone(uint32_t id, CVector* pos, float size, uint32_t r, uint32_t g, uint32_t b, uint32_t a, uint32_t pulse, float frac,
                           int32_t rot, uint32_t coll) {
    if (InHouse()) return;
    g_origPlaceCone(id, pos, size, r, g, b, a, pulse, frac, rot, coll);
}
std::vector<std::pair<int, uint16_t>> g_houseDoors;  // the house's doors shut: pool index, its flags before
void ShutHouse(bool shut) {
    auto* pool = Ref<CPoolHeader*>(0x96A7D8);  // CEntryExitManager::mp_poolEntryExits
    if (!pool) return;
    if (!shut) {
        for (auto& [i, flags] : g_houseDoors)
            if (i < pool->size && pool->byteMap[i] >= 0) Ref<uint16_t>((uintptr_t)pool->objects + (uintptr_t)i * 0x3C + 0x30) = flags;
        g_houseDoors.clear();
        return;
    }
    if (!g_houseDoors.empty()) return;
    for (int i = 0; i < pool->size; ++i) {
        if (pool->byteMap[i] < 0) continue;
        uintptr_t e = (uintptr_t)pool->objects + (uintptr_t)i * 0x3C;
        if (Ref<int8_t>(e + 0x32) != 3 || (DoorSpot((void*)e) - kHouse).Length2D() > 40.0f) continue;
        g_houseDoors.push_back({i, Ref<uint16_t>(e + 0x30)});
        Ref<uint16_t>(e + 0x30) &= (uint16_t)~0x4000;  // bEnableAccess off: no marker, no way out
    }
}
void HouseFrame() {
    bool in = InHouse();
    ShutHouse(in);
    static bool hid = false;
    if (in) HideSaves(true), hid = true;
    else if (hid && !g_arena.on) HideSaves(false), hid = false;
}

// Moving while aiming and while shooting (left, right, forward, back) with every gun at every skill: the weapon's
// flags bMoveAim and bMoveFire (CWeaponInfo::m_nFlags +0x18, bits 4 and 5); not the heavy ones nor the scoped ones
void MoveWhileShooting() {
    for (int type = 22; type <= 33; ++type)
        for (int skill = 0; skill <= 3; ++skill)
            if (auto* info = Call<0x743C60, uint8_t*>(type, (uint8_t)skill)) Ref<uint32_t>((uintptr_t)info + 0x18) |= 0x30;
}

class ArenaModule : public Module {
public:
    const char* Name() const override { return "Arena"; }
    void OnInstallHooks() override {
        Net().SetHandler(proto::G_ARENA, &Handle);
        mem::HookFn(0x73B2A0, &HookHasAmmo, &g_origHasAmmo, "CWeapon::HasWeaponAmmoToBeUsed");
        mem::HookFn(0x725120, &HookPlaceMarker, &g_origPlaceMarker, "C3dMarkers::PlaceMarker");
        mem::HookFn(0x726D40, &HookPlaceCone, &g_origPlaceCone, "C3dMarkers::PlaceMarkerCone");
    }
    void OnGameShutdown() override {
        if (g_arena.on) game::challenge::End();
        g_info.clear();
        g_squads.clear();
        g_enemies.clear();
        g_bossPed = nullptr;
        g_twinPeds[0] = g_twinPeds[1] = nullptr;
        g_flamers.clear();
        g_toRaise.clear();
        g_bars.clear();  // their objects went with the game
        g_walls.clear();
        g_drops.clear();
        g_hiddenSaves.clear();
        g_vests.clear();
        g_vestIds.clear();
        g_gas.clear();
        g_shells.clear();
        g_hasUgl = g_uglLoaded = false;
        g_sawPed = g_officerPed = nullptr;
        g_tagCopies[0] = g_tagCopies[1] = nullptr;
        g_spinHandle = -1;
        g_box = BoxState{};
        g_ward.handle = -1;
        g_guards.clear();
        g_outfit = -1, g_modelBefore = -1, g_maxHealthBefore = 0;
            g_arena = Arena{};
    }
    void OnSessionEnd() override {
        g_lobby = g_modeLost = g_modeOn = false;
        ShutHouse(false);
        g_modeReadyMs = 0;
        if (g_arena.on) End(false);
    }
    void OnGameStarted() override { MoveWhileShooting(); }
    void OnPostProcess() override {
        if (g_arena.on && core::IsPlayerReady()) Update();
        GameMode();
        if (core::IsPlayerReady()) HouseFrame(), KeepInHouse();
    }
    void OnRender() override {
        if (!g_arena.on || MenuActive()) return;
        DrawMarks();
        auto& g = ui::Gfx();
        float s = g.Scale();
        // the top: the wave and the enemies left, and the boss of a boss wave; the rest is on Tab (DrawScoreboard)
        std::string line;
        if (g_arena.endAt) line = g_arena.won ? TR("АРЕНА ПРОЙДЕНА", "ARENA WON") : TR("АРЕНА ПРОИГРАНА", "ARENA LOST");
        else if (!g_arena.wave) line = TR("ПРИГОТОВЬТЕСЬ", "GET READY");
        else if (Endless()) line = Format(TR("ВОЛНА %d   ВРАГИ %d", "WAVE %d   ENEMIES %d"), g_arena.wave, g_arena.left);
        else line = Format(TR("ВОЛНА %d/%d   ВРАГИ %d", "WAVE %d/%d   ENEMIES %d"), g_arena.wave, g_arena.waves, g_arena.left);
        g.Text(ui::FONT_NORMAL, g.Width() * 0.5f, 14 * s, line, 0xFFFFD060, ui::TEXT_CENTER | ui::TEXT_OUTLINE);
        float y = 14 * s + g.LineHeight(ui::FONT_NORMAL);
        if (BossWave(g_arena.wave) && g_arena.boss >= 0 && !g_arena.endAt && g_bossPed && IsPedValid(g_bossPed) && g_bossPed->Health() > 0.0f) {
            g.Text(ui::FONT_SMALL, g.Width() * 0.5f, y, Format("%s  %.0f", BossName(g_arena.boss).c_str(), g_bossPed->Health()), 0xFFFF7060,
                   ui::TEXT_CENTER | ui::TEXT_OUTLINE);
            y += g.LineHeight(ui::FONT_SMALL);
        }
        DrawCod();
    }
    bool OnCommand(const std::string& cmd, const std::vector<std::string>& args) override {
        std::string c = LowerRu(cmd);
        bool skipCmd = Matches(c, kWordSkip);  // /skip alone too
        if (!Matches(c, kWordArena) && !skipCmd) return false;
        std::string a0 = skipCmd ? std::string("skip") : args.empty() ? "" : LowerRu(args[0]);
        if (a0.empty() || Matches(a0, kWordList) || a0 == "help") {
            List();
            return true;
        }
        if (Matches(a0, kWordSkip)) {
            if (!g_arena.on) ui::TheChat().AddError(TR("Арена не идёт.", "No arena is on."));
            else if (Hosting()) Skip(Net().MyId());
            else Send(A_SKIP);
            return true;
        }
        if (a0 == "смена" || a0 == "сменить" || a0 == "change") {  // the vote for another arena, between the waves
            if (!g_arena.on || !g_break || !g_arena.taken) {
                ui::TheChat().AddError(TR("Сменить арену можно только между волнами.", "The arena can be changed between the waves only."));
            } else if (!g_iMapVoted) {
                g_iMapVoted = true;
                ui::TheChat().AddSystem(TR("Голос за смену арены.", "Voted for another arena."));
                if (g_arena.host) MapVote(Net().MyId());
                else Send(A_MAPVOTE);
            }
            return true;
        }
        if (!Hosting()) {
            ui::TheChat().AddError(TR("Арену начинает хост.", "The host starts an arena."));
            return true;
        }
        if (Matches(a0, kWordStop)) {
            if (!g_arena.on) ui::TheChat().AddError(TR("Арена не идёт.", "No arena is on."));
            else {
                g_arena.won = false;
                End(true);
                ui::TheChat().AddSystem(TR("Арена остановлена.", "The arena is stopped."));
            }
            return true;
        }
        if (a0 == "тибон" || a0 == "тибоун" || a0 == "tbone") {  // a test: T-Bone now
            if (!g_arena.on || !g_arena.taken) ui::TheChat().AddError(TR("Сначала начните арену.", "Start an arena first."));
            else SpawnTBone();
            return true;
        }
        // the host's own spots for this place: where it stands now
        const char* spot = a0 == "ящик" || a0 == "box"               ? "box"
                           : a0 == "баррикада" || a0 == "barricade" ? "bar"
                           : a0 == "стена" || a0 == "wall"          ? "wall"
                           : a0 == "сброс" || a0 == "reset"         ? "reset"
                                                                    : nullptr;
        if (spot) {
            if (!g_arena.on || !g_arena.taken) ui::TheChat().AddError(TR("Сначала начните арену в этом месте.", "Start an arena at that place first."));
            else SaveSpot(spot);
            return true;
        }
        if (g_arena.on) {
            ui::TheChat().AddError(TR("Арена уже идёт. Закончить: /arena stop", "An arena is on already. End it: /arena stop"));
            return true;
        }
        CPlayerPed* me = FindPlayerPed(0);
        if (!me || me->Health() <= 0.0f) return true;
        // the enemies, the place and the waves in any order: /arena 3 притон байкеры, /arena притон байкеры бесконечно
        int faction = -1, place = -1, waves = 5;
        for (const std::string& raw : args) {
            std::string a = LowerRu(raw);
            int f = -1, pl = -1;
            for (int i = 0; i < kFactionCount; ++i)
                if (Matches(a, kFactions[i].keys)) f = i;
            for (int i = 0; i < kPlaceCount; ++i)
                if (Matches(a, kPlaces[i].keys)) pl = i;
            bool number = !a.empty() && std::all_of(a.begin(), a.end(), [](char c) { return c >= '0' && c <= '9'; });
            if (f >= 0 && faction < 0) faction = f;  // "полиция" is both: the enemies first, then the place
            else if (pl >= 0) place = pl;
            else if (Matches(a, kWordRandom) || Matches(a, kWordAny)) place = -1;
            else if (Matches(a, kWordEndless)) waves = 0;
            else if (number) {
                waves = std::atoi(a.c_str());
                if (waves < 0 || waves > 10) {
                    ui::TheChat().AddError(TR("Волн: от 1 до 10 или «бесконечно».", "Waves: 1 to 10 or \"endless\"."));
                    return true;
                }
            } else {
                ui::TheChat().AddError(Format(TR("Непонятно: %s (/arena list).", "Not understood: %s (/arena list)."), raw.c_str()));
                return true;
            }
        }
        if (faction < 0) {
            ui::TheChat().AddError(TR("Укажите врагов (/arena list).", "Name the enemies (/arena list)."));
            return true;
        }
        if (place < 0) place = RandInt(0, kPlaceCount - 1);
        Launch(faction, place, waves);
        return true;
    }
};

ArenaModule g_module;
struct Reg {
    Reg() { core::RegisterModule(&g_module); }
} g_reg;
}  // namespace

namespace arena {

bool On() {
    CPlayerPed* me = core::IsPlayerReady() ? FindPlayerPed(0) : nullptr;
    return g_arena.on && g_arena.taken && me && me->AreaCode() == kPlaces[g_arena.place].area;
}
bool UnlimitedRevives() { return g_arena.on && g_arena.taken; }
bool Spectating() { return g_spectating; }
void Fall() { Spectate(); }
bool QuickRevive() { return false; }
void UseQuickRevive() {}
float ReloadFactor() { return 1.0f; }
bool IsEnemy(CPed* p);
int IncomingDamage(CEntity* creator, int weapon, int damage) {
    if (!On()) return damage;
    if (weapon == WEAPON_FLAMETHROWER) return std::max(1, damage * 35 / 100);  // the flamethrower's and the fire's damage
    // the Twin with two "eagles": two pistols at the pro skill, each shot as hard as an eagle's (70 to the pistol's 25)
    if (weapon == WEAPON_PISTOL && creator && creator->IsPed() && IsEnemy((CPed*)creator) && ((CPed*)creator)->WeaponSkill() == 2)
        return damage * 14 / 5;
    return damage;
}

// is it an enemy of the arena: the host's own, or a copy of one (an armed man of the arena's place, no player)
bool IsEnemy(CPed* p) {
    if (!g_arena.on || !p || p->IsPlayer() || Players::Instance().FindByPed(p)) return false;
    if (g_arena.host) return g_enemies.count(p) != 0;
    return p->AreaCode() == kPlaces[g_arena.place].area;
}

void OnLocalHit(CPed* victim, int weapon, int damage, int piece) {}  // money now falls off the dead (D_CASH)

// Weapon skills grow with kills in the arena: the game's own skill stats (the pistol's, the AK's...: poor, gangster,
// hitman at 1000), +40 a kill, +80 a headshot; a step up shows
int SkillStat(int weapon) {
    switch (weapon) {
        case 22: return 69;
        case 23: return 70;
        case 24: return 71;
        case 25: return 72;
        case 26: return 73;
        case 27: return 74;
        case 28: case 32: return 75;
        case 29: return 76;
        case 30: return 77;
        case 31: return 78;
        case 34: return 79;
        default: return -1;
    }
}
const char* SkillName(float v) { return v >= 999.0f ? TR("мастер", "hitman") : v >= 200.0f ? TR("опытный", "gangster") : TR("новичок", "poor"); }
void GrowSkill(int weapon, bool headshot) {
    int stat = SkillStat(weapon);
    if (stat < 0) return;
    float was = Call<0x558E40, float>((uint16_t)stat);  // CStats::GetStatValue
    float now = std::min(1000.0f, was + (headshot ? 80.0f : 40.0f));
    Call<0x55C180>((uint16_t)stat, now);                 // CStats::SetStatValue
    if ((was < 200.0f && now >= 200.0f) || (was < 999.0f && now >= 999.0f))
        g_popups.push_back({Format("%s: %s", WeaponName(weapon), SkillName(now)), NowMs(), 0xFF80C0FF});
}

void OnLocalKill(CPed* victim, int weapon, bool headshot) {
    if (!On() || !IsEnemy(victim) || g_killed.count(victim)) return;
    g_killed.insert(victim);
    ++g_kills;
    GrowSkill(weapon, headshot);
    SendScore();
}

bool InLobby() { return false; }  // the arena mode keeps everyone in the game: no lobby after a lost arena
bool Running() { return g_arena.on; }
void StartNow() {  // the host's button (F9): the next arena now
    if (!Net().IsHost() || g_arena.on) return;
    g_modeNextMs = 0;
    int faction = std::clamp(Cfg().arenaFaction, 0, kFactionCount - 1);
    int place = Cfg().arenaPlace >= 0 && Cfg().arenaPlace < kPlaceCount ? Cfg().arenaPlace : RandInt(0, kPlaceCount - 1);
    Launch(faction, place, std::clamp(Cfg().arenaWaves, 0, 10));
}
void LobbyStart() {
    if (!g_lobby || !Net().IsHost()) return;
    int faction = std::clamp(Cfg().arenaFaction, 0, kFactionCount - 1);
    int place = Cfg().arenaPlace >= 0 && Cfg().arenaPlace < kPlaceCount ? Cfg().arenaPlace : RandInt(0, kPlaceCount - 1);
    flow::SaveConnectSettings();
    Launch(faction, place, std::clamp(Cfg().arenaWaves, 0, 10));
}
int FactionCount() { return kFactionCount; }
std::string FactionTitle(int i) { return i >= 0 && i < kFactionCount ? FactionName(i) : std::string("?"); }
int PlaceCount() { return kPlaceCount; }
std::string PlaceTitle(int i) { return i >= 0 && i < kPlaceCount ? PlaceName(i) : std::string(TR("любое", "any")); }

bool KeyDown(int vk, bool repeat) {
    std::string hint;
    if (!On() || !ReviveText(hint).empty()) return false;
    if (vk == 'H') return !repeat && TryBuild();
    if (vk == 'J') return !repeat && (PutDownGun(), true);
    if (vk == 'N') {  // T-Bone's launcher; without it N goes on (a side activity's "no")
        if (!g_hasUgl) return false;
        if (!repeat) PlayerUgl();
        return true;
    }

    if (vk == 'Y') {  // the vote to skip the wait between the waves
        if (!g_break || repeat) return g_break;
        if (!g_iVoted) {
            g_iVoted = true;
            ui::TheChat().AddSystem(TR("Голос за пропуск.", "Voted to skip."));
            if (g_arena.host) Vote(Net().MyId());
            else Send(A_VOTE);
        }
        return true;
    }
    if (g_nearKind == N_NONE) return false;
    uint64_t now = NowMs();
    if (g_nearKind == N_BAR) {
        if (now - g_lastRepairMs < 650) return true;  // held: one board at a time
        g_lastRepairMs = now;
        if (g_points < kRepairCost) {
            ui::TheChat().AddError(Format(TR("Нужно $%d.", "You need $%d."), kRepairCost));
            return true;
        }
        if (g_arena.host) SetBoards(g_nearIdx, g_bars[(size_t)g_nearIdx].boards + 1, Net().MyId());
        else Send(A_REPAIR, [](Writer& w) { w.U8((uint8_t)g_nearIdx); });
        return true;
    }
    if (repeat) return true;
    if (g_nearKind == N_WALL) {
        BuyWall(g_nearIdx);
        return true;
    }
    if (g_nearKind == N_WARD) {
        BuyOutfit();
        return true;
    }
    if (g_nearKind == N_GUN) {  // a team mate's weapon for the one in hand of its kind
        uint8_t id = g_drops[(size_t)g_nearIdx].id;
        g_drops[(size_t)g_nearIdx].asked = true;
        if (g_arena.host) TakeDrop(id, Net().MyId());
        else Send(A_TAKE, [id](Writer& w) { w.U8(id); });
        return true;
    }
    if (g_nearKind == N_BOX) {
        if (g_box.offered) {
            TakeOffer();
            return true;
        }
        if (g_box.rolling) return true;
        if (g_points < kBoxCost) {
            ui::TheChat().AddError(Format(TR("Нужно $%d.", "You need $%d."), kBoxCost));
            return true;
        }
        if (g_arena.host) UseBox(Net().MyId());
        else Send(A_BOXUSE);
        return true;
    }
    return false;
}

// What can be done here, in a few plain words: a title and the key ("[E] Открыть  $950")
std::string PromptText(std::string& hint) {
    if (!On() || g_nearKind == N_NONE || g_spectating) return {};
    if (g_nearKind == N_BAR) {
        hint = Format(TR("[E] Прибить доску  $%d", "[E] Nail a board  $%d"), kRepairCost);
        return TR("Баррикада", "Barricade");
    }
    if (g_nearKind == N_WARD) {
        hint = Format(TR("[E] Случайная одежда  $%d", "[E] Random outfit  $%d"), kWardCost);
        return TR("Гардероб", "Wardrobe");
    }
    if (g_nearKind == N_GUN) {
        const Drop& d = g_drops[(size_t)g_nearIdx];
        int held = SlotTaken(FindPlayerPed(0), d.weapon);
        hint = Format(TR("[E] Взять вместо: %s", "[E] Take instead of: %s"), WeaponName(held));
        return WeaponName(d.weapon);
    }
    if (g_nearKind == N_WALL) return {};
    // the box
    bool mine = g_box.by == Net().MyId() || !Net().Online();
    if (g_box.offered) {
        if (!mine) return {};
        const BoxGun& g = kBoxGuns[(size_t)g_box.gun % (sizeof(kBoxGuns) / sizeof(kBoxGuns[0]))];
        int held = SlotTaken(FindPlayerPed(0), g.weapon);
        hint = held ? Format(TR("[E] Взять вместо: %s", "[E] Take instead of: %s"), WeaponName(held)) : std::string(TR("[E] Взять", "[E] Take"));
        return BoxGunName(g_box.gun);
    }
    if (g_box.rolling) return {};
    hint = Format(TR("[E] Открыть  $%d", "[E] Open  $%d"), kBoxCost);
    return TR("Ящик с оружием", "Weapon box");
}

}  // namespace arena

}  // namespace coop
