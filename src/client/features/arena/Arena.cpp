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
#include "entities/vehicle/Vehicles.h"
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
#include "shared/ui/input/Input.h"
#include "shared/ui/widgets/Widgets.h"

namespace coop {

namespace arena {
bool IsEnemy(game::CPed* p);
}

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
    CVector box{};          // the weapon box's own spot (the floor; 0: the mod picks one), its heading below
    float boxHeading = 0;   // the host's own (/arena ящик, coop\arena-spots.ini) still comes first
};
const Place kPlaces[] = {
    {{TRP("притон", "crack"), TRP("смоук", "crack")}, TRP("Крэк-дворец Биг Смоука", "Big Smoke's crack palace"), 2, {2543.46f, -1308.38f, 1026.73f}, 14.0f, false, false, {2564.73f, -1297.03f, 1030.42f}, 91.0f},
    {{TRP("особняк", "mansion"), TRP("мэддогг", "mansion")}, TRP("Особняк Мэдд Догга", "Madd Dogg's mansion"), 5, {1267.66f, -781.32f, 1091.91f}, 18.0f, true, false, {1270.81f, -838.83f, 1084.63f}, 0.8f},
    {{TRP("мясокомбинат", "meat"), TRP("бойня", "meat")}, TRP("Мясокомбинат", "The meat factory"), 1, {963.42f, 2108.29f, 1011.03f}, 48.0f, true, false, {932.65f, 2143.48f, 1010.02f}, -90.7f},
    {{TRP("мотель", "motel"), TRP("джефферсон", "motel")}, TRP("Мотель «Джефферсон»", "The Jefferson motel"), 15, {2215.45f, -1147.48f, 1025.80f}, 20.0f, true, true, {2238.94f, -1184.78f, 1032.80f}, -89.4f},
    {{TRP("либерти", "liberty"), TRP("бистро", "liberty")}, TRP("Либерти-Сити (бистро Марко)", "Liberty City (Marco's bistro)"), 1, {-794.81f, 497.74f, 1376.20f}, 14.0f, false, false, {-786.55f, 500.08f, 1370.74f}, 1.3f},
    {{TRP("полиция", "police"), TRP("участок", "police")}, TRP("Полицейский участок", "The police station"), 6, {246.78f, 63.90f, 1003.64f}, 14.0f, false, true, {227.23f, 79.96f, 1004.04f}, -90.0f},
    {{TRP("дамба", "dam"), TRP("шерман", "dam")}, TRP("Дамба Шерман", "The Sherman dam"), 17, {-959.56f, 1848.58f, 9.00f}, 30.0f, true, false, {-960.25f, 1882.85f, 8.00f}, -90.5f},
    {{TRP("кикстарт", "kickstart"), TRP("кикстарт", "kickstart")}, TRP("Стадион «Кикстарт»", "The Kickstart stadium"), 14, {-1465.27f, 1557.87f, 1052.53f}, 35.0f, true, true, {-1422.02f, 1569.40f, 1051.53f}, -0.7f},
    // the big ones added later (at the end: the places' numbers in coop.ini stay as they were)
    {{TRP("полигон", "rc"), TRP("зеро", "zero")}, TRP("RC-полигон Зеро", "Zero's RC battlefield"), 10, {-975.58f, 1061.13f, 1346.2f}, 32.0f, true, false, {-968.28f, 1071.33f, 1344.03f}, 89.1f},
    {{TRP("бладбоул", "bloodbowl"), TRP("боул", "bowl")}, TRP("Стадион «Блад Боул»", "The Blood Bowl stadium"), 15, {-1394.20f, 987.62f, 1024.4f}, 40.0f, true, false},
    {{TRP("мэрия", "planning"), TRP("планирование", "townhall")}, TRP("Департамент планирования", "The planning department"), 3, {374.67f, 173.81f, 1008.8f}, 26.0f, true, false, {354.44f, 169.33f, 1013.19f}, 1.4f},
    {{TRP("аэропорт", "airport"), TRP("фрэнсис", "francis")}, TRP("Аэропорт Сан-Фиерро", "The San Fierro airport"), 14, {-1860.82f, 59.74f, 1062.3f}, 34.0f, true, false},
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
    A_HEALGAS, A_BOSSNAME, A_HIT, A_SHELL, A_SPEC, A_BOXTAKEN, A_SPOT, A_DRONEHIT, A_ALERT /* not used any more */, A_GUARDNAME, A_GUARDREQ, A_ZONE, A_EBAR, A_SAWHIT, A_HALVE, A_PROPS, A_EBARHIT, A_EBARGONE, A_OPERATOR /* not used any more */, A_MVP, A_MAGNET, A_EVENT, A_NEXTBOSS, A_SAWWIND, A_GEAR /* not used any more */, A_IGNITE /* not used any more */, A_MERCHANT, A_BAYONET
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
int g_nextBoss = -1;  // the next boss wave's boss (the host picks it ahead; every game shows it at the wave bar's end)
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
struct HiddenSave {
    int index;
    uint32_t regen;  // its m_nRegenerationTime
    uint8_t flags;   // its m_nFlags
};
std::vector<HiddenSave> g_hiddenSaves;  // the save disks hidden meanwhile (HideSaves)
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
bool BossWave(int w) { return Endless() ? w % 10 == 0 : w == g_arena.waves; }  // every 10th (endless)
// the hard one in the middle of the ten: more of them, harder to kill (the 5th, 15th...; in a set number of waves its middle)
bool HardWave(int w) { return Endless() ? w % 10 == 5 : g_arena.waves >= 6 && w == g_arena.waves / 2; }
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
// CJ's body and clothes made again (CClothes::RebuildPlayer) not at once but once he stands on his feet for a second,
// out of a car, the game in front, with the streaming's unused models let go first: done in the middle of a teleport,
// an arena's start or end, it crashed in the clothes' textures (crash.log: 0x7F3737 from CClothesBuilder)
uint64_t g_rebuildAt = 0;
void RequestRebuild() { g_rebuildAt = NowMs() + 1000; }
void RebuildWhenSafe() {
    if (!g_rebuildAt || NowMs() < g_rebuildAt) return;
    game::CPlayerPed* me = core::IsPlayerReady() ? game::FindPlayerPed(0) : nullptr;
    if (!me || me->Health() <= 0.0f || me->InVehicle() || game::MenuActive() || game::cam::WideScreenOn() || me->ModelIndex() != 0 ||
        !me->ClothesDesc())
        return;  // later
    if (NowMs() - game::LastClothesRebuildMs() < 2000) return;  // another ped's rebuild just now (a partner's look): a little later
    g_rebuildAt = 0;
    game::Call<0x5A82C0>(me, false);     // CClothes::RebuildPlayer
}
void StopOwnMoves();
void SawKill(game::CPed* p);
void DashFrame(uint64_t now);
void HoloFrame(uint64_t now);
extern uint64_t g_fewSince;
void ShowStatBox();
void HoldWorld(bool on);
extern int g_dashCharges;
// ======================================================================= block 3: the merchant, his armours, gadgets, mods
// After the 5th wave, then the 15th, 25th...: as the break ends nobody comes ("Но никто не пришел."), a man walks in
// to the boxes and sells (E by him): one armour kind, one gadget (X), weapon mods (N). The next wave once the minute
// is over or everyone voted (Y). What a player buys is its own (its game keeps it for the arena).
constexpr int MODEL_MERCHANT = 179;  // the Ammu-Nation clerk
struct MerchantState {
    bool on = false;       // every game: the trade's minute
    uint16_t id = 0;       // his net id (the partners find their copy by it)
    CPed* ped = nullptr;   // host: the man; partners: their copy
    uint64_t textUntil = 0, walkUntil = 0;
    int wave = 0;          // host: the wave he came after
    game::CVector from;    // where he came in (every game: the cut-scene looks there)
    game::CVector to;      // host: where he goes
    bool walking = false;  // host: sent on his way (once the words are gone)
    uint64_t cutFrom = 0, cutUntil = 0;  // the cut-scene: after the words, 5 s of him walking in
    bool cut = false;
} g_merchant;
bool g_hasMerchantTo = false;  // host: where he walks to at this place (/торговец цель), else beside the box
game::CVector g_merchantTo;
bool g_hasMerchantSpot = false;  // host: where he comes in at this place (/торговец), else an enemies' spot
game::CVector g_merchantSpot;
bool MerchantWave(int w) { return w == 5 || (w > 5 && (w - 5) % 10 == 0); }
// what this player bought: the armour's kind (0 none, 1 the Juggernaut's, 2 the Firestarter's, 3 the Soldier's), the
// gadget (0 none, 1 the cop's visor, 2 the rage dose, 3 the Shahed's remote), the mods
int g_armourKind = 0, g_gadget = 0;
bool g_modUgl = false, g_modBayonet = false, g_modSpark = false;
float g_visorCharge = 100.0f;
bool g_visorOn = false;
int g_rageReadyWave = 0;  // the rage dose: once in 2 waves
uint64_t g_rageStunUntil = 0, g_rageUntil = 0, g_rageTextUntil = 0;
int g_shahedCharges = 0, g_scrap = 0;
uint64_t g_shahedSince = 0;
bool g_shopOpen = false;
struct ShopItem {
    int kind;  // 0 armour, 1 gadget, 2 mod
    int which;
    int price;
    const char* name[2];
    const char* what[2];
};
const ShopItem kShop[] = {
    {0, 1, 1200, TRP("Броня Джаггернаута", "The Juggernaut's armour"), TRP("броня держит на 50% больше, обоймы больше, чуть медленнее", "armour takes 50% more, bigger clips, a little slower")},
    {0, 2, 1300, TRP("Броня Поджигателя", "The Firestarter's armour"), TRP("огонь не жжёт, +2 молотова и +200 к огнемёту каждую волну, медленнее", "fire does not burn, +2 molotovs and +200 flamethrower ammo a wave, slower")},
    {0, 3, 1000, TRP("Броня Солдата", "The Soldier's armour"), TRP("быстрее, оружие точнее и дальше, броня держит на 25% меньше", "faster, guns more accurate and farther, armour takes 25% less")},
    {1, 1, 3000, TRP("Визор копа", "The cop's visor"), TRP("[X] враги видны сквозь стены, все пули — в голову ближайшего; заряд 3%/с, +3% за убийство", "[X] enemies seen through walls, every bullet into the nearest one's head; 3% charge a second, +3% a kill")},
    {1, 2, 1300, TRP("Рейдж-доза", "The rage dose"), TRP("[X] укол, 3 с судорог, потом RAMPAGE: 200 HP, быстрее, точнее, сильнее; раз в 2 волны, и лёжа раненым", "[X] a shot, 3 s of fits, then RAMPAGE: 200 HP, faster, more accurate, harder; once in 2 waves, wounded too")},
    {1, 3, 700, TRP("Пульт «Shahed»", "The Shahed's remote"), TRP("[X] свой вертолётик-камикадзе; новый заряд из 2 груд хлама со сбитых дронов", "[X] your own kamikaze helicopter; a new charge from 2 heaps of scrap off downed drones")},
    {2, 1, 500, TRP("Подствольный гранатомёт", "An underbarrel launcher"), TRP("[N] на любом оружии, заряжается гранатой", "[N] on any gun, loads with a grenade")},
    {2, 2, 300, TRP("Штык-нож", "A bayonet"), TRP("[N] удар вперёд: урон и враг на полу", "[N] a thrust: damage and the enemy down")},
    {2, 3, 150, TRP("Магазины «Спарка»", "Twin mags"), TRP("каждая вторая перезарядка быстрая (первая тоже)", "every second reload fast (the first too)")},
};
constexpr int kShopCount = (int)(sizeof(kShop) / sizeof(kShop[0]));
bool Owned(const ShopItem& it) {
    return it.kind == 0 ? g_armourKind == it.which : it.kind == 1 ? g_gadget == it.which
           : it.which == 1 ? g_modUgl : it.which == 2 ? g_modBayonet : g_modSpark;
}
void ResetShop() {  // a new arena: nothing bought
    g_armourKind = g_gadget = 0;
    g_modUgl = g_modBayonet = g_modSpark = false;
    g_visorCharge = 100.0f, g_visorOn = false;
    g_rageReadyWave = 0, g_rageStunUntil = g_rageUntil = g_rageTextUntil = 0;
    g_shahedCharges = g_scrap = 0, g_shahedSince = 0;
    if (g_shopOpen) ui::In().SetKeyboardCapture(false), ui::In().SetMouseCapture(false);
    g_shopOpen = false;
}
void PlayerGuns(bool bigClips, bool sharp);
void BayonetHit(game::CPed* p);
void SetShopOpen(bool on);
void Label(const game::CVector& at, const std::string& text, uint32_t color, float maxDist, bool throughWalls);
int MountQuad(int h);
bool RcPlace() { return std::string(kPlaces[g_arena.place].keys[0][1]) == "rc"; }
int g_dronesLeft = 0;  // host: the RC helicopters still to come this wave
uint64_t g_nextDroneMs = 0;
// the host's own points of a place (coop\arena-spots.ini): where the players come in, where the enemies come from
bool g_hasOwnSpawn = false;
game::CVector g_ownSpawn;
std::vector<game::CVector> g_enemyPts;
// the crack palace's zones: each wave the enemies go for the next one (every game knows the wave's, A_ZONE)
std::vector<game::CVector> g_zones;  // host
struct BuildSpot {  // the host's own spots for the builders' barricades (/arena стройка): the fence there, so turned
    game::CVector pos;
    float heading = 0;
};
std::vector<BuildSpot> g_buildSpots;  // host
constexpr int kMaxBuildSpots = 16;
int g_zoneNow = -1;                  // every game: the wave's zone (its number from 0), -1: none
game::CVector g_zoneAt;
// host: this arena's boss carries the AK-47 with the grenade launcher (T-Bone always, the others half the time) or a
// katana (spins at the players, the bullets bounce off him meanwhile)
bool g_bossUgl = false, g_bossKatana = false;
void ZoneWave(int wave);
bool CrackPlace() { return std::string(kPlaces[g_arena.place].keys[0][1]) == "crack"; }
int BoxCost();
int RepairCost();
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

CVector EntrySpot();
void LoadKnifeModel() { g_knifeModel = -1; }  // the knifemen are men of their own force

// no passers-by while the battle is on (the casinos' gamblers, the stadiums' people): the game's ped density 0, back
// as it was afterwards
float g_densityBefore = -1.0f;
void NoPassersBy(bool on) {
    float& density = Ref<float>(0x8D2530);  // CPopulation::PedDensityMultiplier
    if (on) {
        if (g_densityBefore < 0.0f) g_densityBefore = density;
        Cmd(0x03DE).F(0.0f).Run();  // SET_PED_DENSITY_MULTIPLIER
    } else if (g_densityBefore >= 0.0f) {
        Cmd(0x03DE).F(g_densityBefore).Run();
        g_densityBefore = -1.0f;
    }
}

// into the place, a little apart from the others
void TakeIn() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || g_arena.taken) return;
    NoPassersBy(true);
    g_arena.backArea = me->AreaCode();
    g_arena.back = me->VehicleIfIn() ? me->VehicleIfIn()->GetPosition() + CVector{0, 0, 1.0f} : me->GetPosition();
    g_arena.backHeading = me->CurrentRotation() * 57.29578f;
    const Place& pl = kPlaces[g_arena.place];
    Teleport(pl.area, EntrySpot(), Rand(0.0f, 360.0f));
    g_arena.taken = true;
    ShutDoors(true);
}

void TakeBack() {
    NoPassersBy(false);
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

// What some places need of the floor's cells: the RC battlefield's ground is small hills and toy houses (finer cells,
// no step higher than a shin, nothing at the knees in the way); the planning department is played on its upper
// floors only (the lowest storey above the lobby is found first, the cells start there and the stairs down are shut)
struct Tune {
    float step = 0;       // the cells' spacing (0: by the place's size)
    float climb = 1.0f;   // the most a step goes up or down
    bool knees = false;   // nothing in the way at the knees between two cells
    bool upper = false;   // from the storey over the lobby up only
};
Tune TuneOf(int place) {
    std::string key = kPlaces[place].keys[0][1];
    Tune t;
    if (key == "rc") t.step = 1.25f, t.climb = 0.6f, t.knees = true;
    if (key == "planning") t.upper = true;
    return t;
}
float g_minZ = -1e9f;                                  // the upper floors' place: no cell under it
std::vector<std::pair<CVector, CVector>> g_frontier;   // host: a cell at the top of stairs down, the way down
bool g_probing = false;                                // host: the storeys are being looked for
int g_probeAt = 0;
float g_probeFloor = 0;
std::vector<CVector> g_probeHits;                      // floors with room over them, over the lobby
void OnGridDone();
void SeedGrid(const CVector& start) {
    const Place& pl = kPlaces[g_arena.place];
    g_cells.push_back({start, {}, Roomy(start)});
    int i = (int)std::lround((start.x - pl.pos.x) / g_step), j = (int)std::lround((start.y - pl.pos.y) / g_step);
    g_cellAt[CellKey(i, j, start.z)] = 0;
    g_open.push_back(0);
}
void LoadOwnPoints();
void StartGrid() {
    LoadOwnPoints();
    g_cells.clear();
    g_cellAt.clear();
    g_open.clear();
    g_roomy.clear();
    g_frontier.clear();
    g_probeHits.clear();
    g_gridDone = false;
    g_minZ = -1e9f;
    const Place& pl = kPlaces[g_arena.place];
    Tune tune = TuneOf(g_arena.place);
    g_step = tune.step > 0 ? tune.step : pl.reach > 30.0f ? 2.0f : 1.5f;
    g_reachCells = (int)(pl.reach * 1.2f / g_step) + 1;
    float z = 0;
    if (!FloorBelow(pl.pos.x, pl.pos.y, pl.pos.z + 1.0f, z)) z = pl.pos.z - 1.0f;
    if (tune.upper) {  // the storeys first (ProbeStep), then the cells from the one over the lobby
        g_probing = true;
        g_probeAt = 0;
        g_probeFloor = z;
        return;
    }
    SeedGrid({pl.pos.x, pl.pos.y, z});
}
// Over the lobby, a column every 2.5 m: rays down from 16 m up, one floor after another (a floor with 1.9 m free over
// it and room round it). The lowest storey that many columns share is the first upper floor: the cells start on it.
constexpr int kProbeSide = 21;
void ProbeStep() {
    const Place& pl = kPlaces[g_arena.place];
    for (int budget = 25; budget > 0 && g_probeAt < kProbeSide * kProbeSide; --budget, ++g_probeAt) {
        float x = pl.pos.x + (float)(g_probeAt % kProbeSide - kProbeSide / 2) * 2.5f;
        float y = pl.pos.y + (float)(g_probeAt / kProbeSide - kProbeSide / 2) * 2.5f;
        float from = g_probeFloor + 16.0f;
        for (int k = 0; k < 10 && from > g_probeFloor + 2.0f; ++k) {
            CVector hit;
            if (!Ray({x, y, from}, {x, y, g_probeFloor + 1.5f}, hit)) break;
            if (hit.z > g_probeFloor + 2.5f && Clear(hit + CVector{0, 0, 0.15f}, hit + CVector{0, 0, 1.9f}) && Roomy(hit))
                g_probeHits.push_back(hit);
            from = hit.z - 0.25f;
        }
    }
    if (g_probeAt < kProbeSide * kProbeSide) return;
    g_probing = false;
    // the storeys: the floors' heights in groups (0.8 m apart at most), the lowest with 6 columns or more
    std::vector<CVector> hits = g_probeHits;
    std::sort(hits.begin(), hits.end(), [](const CVector& a, const CVector& b) { return a.z < b.z; });
    size_t from = 0;
    float level = 0;
    bool found = false;
    for (size_t i = 1; i <= hits.size() && !found; ++i) {
        if (i < hits.size() && hits[i].z - hits[i - 1].z < 0.8f) continue;
        if (i - from >= 6) level = hits[(from + i) / 2].z, found = true;
        else from = i;
    }
    // the box's own spot is on a floor that is played for sure: the upper floors start there at the highest
    bool boxUp = (pl.box.x != 0.0f || pl.box.y != 0.0f) && pl.box.z > g_probeFloor + 2.5f;
    if (boxUp && (!found || level > pl.box.z + 0.5f)) {
        g_minZ = pl.box.z - 0.6f;
        LOGI("Arena: the upper floors from the box's floor, %.1f m (%d floors seen)", pl.box.z, (int)hits.size());
        SeedGrid(pl.box);
        return;
    }
    if (!found) {
        LOGI("Arena: no upper floor found (%d floors seen): the whole place", (int)hits.size());
        SeedGrid({pl.pos.x, pl.pos.y, g_probeFloor});
        return;
    }
    CVector mid{};
    int n = 0;
    for (const CVector& h : hits)
        if (std::fabs(h.z - level) < 0.8f) mid = mid + h, ++n;
    mid = mid * (1.0f / (float)n);
    CVector seed = mid;
    float best = 1e9f;
    for (const CVector& h : hits)
        if (std::fabs(h.z - level) < 0.8f && (h - mid).Length2D() < best) best = (h - mid).Length2D(), seed = h;
    g_minZ = level - 0.6f;
    LOGI("Arena: the upper floors from %.1f m (the lobby's floor %.1f m), %d floors seen", level, g_probeFloor, (int)hits.size());
    SeedGrid(seed);
}

// A step to a neighbour square: its middle first, and when that does not go (a doorway narrower than the grid, a
// pillar on the way) four spots off the middle of the square; the cell keeps the spot that went.
void GridStep() {  // a part of it each frame
    if (g_probing) return ProbeStep();
    const Place& pl = kPlaces[g_arena.place];
    const Tune tune = TuneOf(g_arena.place);
    const float o = g_step * 0.3f;
    const float offs[5][2] = {{0, 0}, {o, 0}, {-o, 0}, {0, o}, {0, -o}};
    for (int budget = 25; budget > 0 && !g_open.empty() && g_cells.size() < 4000; --budget) {
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
                    if (!FloorBelow(x, y, c.z + 1.3f, z) || std::fabs(z - c.z) > tune.climb) continue;
                    if (z < g_minZ) {  // stairs down to the lobby: shut at the top
                        CVector way{x - c.x, y - c.y, 0.0f};
                        if (Clear(c + CVector{0, 0, 0.9f}, CVector{x, y, z + 0.9f})) g_frontier.push_back({c, way});
                        break;
                    }
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
                        !Clear(n + CVector{0, 0, 0.2f}, n + CVector{0, 0, 1.85f}) ||
                        (tune.knees && !Clear(c + CVector{0, 0, 0.35f}, n + CVector{0, 0, 0.35f})))
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
    if (g_open.empty() || g_cells.size() >= 4000) {
        g_gridDone = true;
        for (size_t k = 0; k < g_cells.size(); ++k)
            if (g_cells[k].room) g_roomy.push_back((int)k);
        LOGI("Arena: %d cells of floor found in %s (%d with room round them)", (int)g_cells.size(), kPlaces[g_arena.place].name[1],
             (int)g_roomy.size());
        OnGridDone();
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
enum Role : uint8_t { R_GUN, R_GRENADIER, R_KNIFE, R_BOSS, R_SNIPER, R_FLAMER, R_MEDIC, R_TWIN, R_SAW, R_OFFICER, R_BUILDER };
// what one does now: fights as the game has it, goes round to a back, runs to cover, holds it (down, up and shooting,
// down), pushes on at the players, (a knifeman) steals up to a back out of the player's sight, (a sniper) moves to
// another spot it sees the players from
// M_BREACH: it came at a barricade and tears its boards off before it gets through (the zombies' windows)
enum Mode : uint8_t { M_FIGHT, M_FLANK, M_COVER_GO, M_COVER_HOLD, M_PUSH, M_SNEAK, M_RELOCATE, M_BREACH };
struct Enemy {
    int kStage = 0;          // the katana boss: 0 coming, 1 winding up, 2 spinning, 3 getting his breath, 4 stunned
    uint64_t kUntil = 0, kNextMs = 0;
    std::set<CPed*> kHit;
    uint64_t zoneUntil = 0;  // the crack palace: on its way to the wave's zone till then (or till there, or a player near)
    CVector zoneSpot;
    CVector buildSpot;       // a builder's: where it puts the next barricade up
    bool hasBuildSpot = false;
    int buildIdx = -1;       // the host's own builder spot it goes to (/arena стройка), -1: one of its own
    uint64_t buildAt = 0;
    int builds = 0;
    uint64_t getUpUntil = 0;  // raised by a medic: getting up off the floor till then
    uint64_t gasUntil = 0;  // a medic's healing gas on the floor: it stands in it till then (or till it is well)
    CVector gasSpot;
    int quad = -1;                         // the quad bike it rides in on (the RC battlefield's mini-bosses), -1: on foot
    uint64_t rideSince = 0, rideStillSince = 0;
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
    bool rusher = false;  // the motel's bat event: a bat, a mad run at the players and the barricades
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
// the RC battlefield's "base": round the players' spot to come in at (20 m), where no enemy comes in
bool NearBase(const CVector& p) {
    if (!RcPlace()) return false;
    return (p - kPlaces[g_arena.place].pos).Length2D() < 20.0f;
}
CVector MemberSpot(const CVector& anchor, int k);
void AutoZones();
CVector SpawnSpot(float angle, float halfSector, float minApart) {
    const Place& pl = kPlaces[g_arena.place];
    std::vector<CVector> players = PlayerSpots();
    // the crack palace: they come in at the zone under attack (the one the players are told of)
    std::vector<CVector> zoneOnly;
    // zone 1: they come in at the very start of the place (its door in) and run to it; the other zones: at the zone
    if (CrackPlace() && g_zoneNow == 0) zoneOnly.push_back(kPlaces[g_arena.place].pos - CVector{0, 0, 1.0f});
    else if (CrackPlace() && g_zoneNow >= 0) zoneOnly.push_back(g_zoneAt);
    const std::vector<CVector>& pts = !zoneOnly.empty() ? zoneOnly : g_enemyPts;
    if (!pts.empty()) {  // the host's own points (or the zones): one of them far from the players, beside it
        CVector best = pts[0];
        float score = -1e9f;
        for (const CVector& q : pts) {
            float sc = DistToPlayers(q, players) + Rand(0.0f, 8.0f);
            if (sc > score) score = sc, best = q;
        }
        CVector p = GridUsable() ? MemberSpot(best + CVector{0, 0, 1.0f}, RandInt(0, 3)) : best + CVector{0, 0, 1.0f};
        g_spawnsNow.push_back(p);
        return p;
    }
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
                if (dp < safe || NearBase(c)) continue;
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

bool EnsureBlock(const char* block);
// a melee weapon's animations (its IFP) in memory and held before a man gets it: a katana, a bat or a chainsaw in a
// man's hands without them, his first blow crashed (0x4D3FBC, CAnimManager::BlendAnimation with no animations there)
void MeleeAnims(int weapon) {
    const char* block = weapon == 8 ? "SWORD" : weapon == 9 ? "CHAINSAW" : weapon == 4 ? "KNIFE"
                      : (weapon >= 2 && weapon <= 7) ? "BASEBALL" : nullptr;
    if (block && !EnsureBlock(block)) LOGI("Arena: the %s animations not in memory for weapon %d", block, weapon);
}
void Give(int h, int weapon, int ammo) {
    MeleeAnims(weapon);
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
    health += armour, armour = 0;  // no armour bar on enemies: what was armour is health now
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
    } else if (role == R_FLAMER) {
        Give(h, WEAPON_MOLOTOV, 2);  // the bottles it throws (in hand only for the throw)
        Give(h, WEAPON_FLAMETHROWER, 5000);
        weapon = GunFor(WEAPON_FLAMETHROWER);
        Cmd(0x0245).I(h).S8("fatman").Run();                       // SET_ANIM_GROUP_FOR_CHAR: heavy and slow
        Cmd(0x02AB).I(h).I(0).I(1).I(0).I(0).I(0).Run();            // SET_CHAR_PROOFS: its own fire does not burn it
        Cmd(0x0946).I(h).I(1).Run();  // its actions not cut by hits: no stagger, no knocking back
    } else if (weapon > 0) {
        Give(h, weapon, role == R_SNIPER ? 80 : 500);
        weapon = GunFor(weapon);
    }
    if (role == R_TWIN) {
        Cmd(0x081A).I(h).I(2).Run();  // SET_CHAR_WEAPON_SKILL pro: a gun in each hand
        Cmd(0x0946).I(h).I(1).Run();  // no stagger, no knocking back
    }
    if (role == R_SAW || role == R_OFFICER) {
        Cmd(0x0946).I(h).I(1).Run();  // no stagger
        if (role == R_SAW) Cmd(0x0245).I(h).S8("player").Run();
    }
    if (role == R_SNIPER) Cmd(0x0350).I(h).I(1).Run();  // SET_CHAR_STAY_IN_SAME_PLACE: it stays where it is
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
    e.grenades = role == R_GRENADIER ? 3 : role == R_FLAMER ? 2 : 0;  // 3 grenades a man, the flamethrower man's 2 molotovs
    e.throwWeapon = role == R_FLAMER ? WEAPON_MOLOTOV : WEAPON_GRENADE;
    e.nextThrowMs = NowMs() + (uint64_t)(role == R_FLAMER ? RandInt(2000, 4000) : RandInt(5000, 8000));
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
    return a;  // the SWAT as the others (their own toughness, Difficulty's, is not for the arena)
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
    // which way each live player looks: no knifeman is put at its back
    std::vector<std::pair<CVector, CVector>> looks;  // where, its facing
    int area = kPlaces[g_arena.place].area;
    auto facing = [](float r) { return CVector{-std::sin(r), std::cos(r), 0.0f}; };
    if (CPlayerPed* me = FindPlayerPed(0); me && !g_spectating && me->Health() > 0.0f && me->AreaCode() == area)
        looks.push_back({me->GetPosition(), facing(me->CurrentRotation())});
    for (auto& [id, rp] : Players::Instance().All())
        if (rp.hasState && !Spectator(id) && !(rp.state.flags & OnFootState::F_DEAD) && rp.state.area == area)
            looks.push_back({rp.state.pos, facing(rp.state.rotation)});
    auto behind = [&](const CVector& p) {  // within 25 m of a player and behind it (more than some 100 degrees off its look)
        for (const auto& [q, f] : looks) {
            CVector d = p - q;
            d.z = 0;
            float len = d.Length2D();
            if (len < 25.0f && len > 0.1f && (d.x * f.x + d.y * f.y) / len < -0.2f) return true;
        }
        return false;
    };
    CVector at;
    bool found = false;
    // with the others: by one of this wave's squads (the one farthest from the players), not at anyone's back
    if (!g_spawnsNow.empty()) {
        float most = -1;
        for (const CVector& sp : g_spawnsNow) {
            float d = DistToPlayers(sp, players) + Rand(0.0f, 5.0f);
            if (d > most) most = d, at = sp;
        }
        CVector beside = MemberSpot(at, RandInt(0, 3));
        if (DistToPlayers(beside, players) >= 9.0f && !behind(beside) && !NearBase(beside)) at = beside;
        found = DistToPlayers(at, players) >= 9.0f;
    }
    if (found) {
        g_spawnsNow.push_back(at);
        int model = g_knifeModel >= 0 && IsModelLoaded(g_knifeModel) ? g_knifeModel : f.models[(size_t)RandInt(0, 3)];
        MakeEnemy(model, WEAPON_KNIFE, 100 + 15 * wave, ArmourFor(wave, 0), 60, at, (int)g_squads.size() - 1, R_KNIFE, true);
        return;
    }
    // the far side of the place from the squads (from the middle of where they came from), never close to a player
    // and never at a player's back: 15 m off at the least (12 m, then 9 m when the place has no such spot)
    CVector squadsMid{};
    for (const CVector& sp : g_spawnsNow) squadsMid = squadsMid + sp;
    if (!g_spawnsNow.empty()) squadsMid = squadsMid * (1.0f / (float)g_spawnsNow.size());
    if (GridUsable() && !found) {
        const float minDs[] = {15.0f, 12.0f, 9.0f, 9.0f};  // the last pass: anywhere 9 m off, a tiny place
        for (int pass = 0; pass < 4 && !found; ++pass) {
            float best = -1e9f;
            for (int k : StandCells()) {
                CVector p = g_cells[(size_t)k].p + CVector{0, 0, 1.0f};
                float dp = DistToPlayers(p, players);
                if (dp < minDs[pass] || (pass < 3 && behind(p)) || NearBase(p)) continue;
                float fromSquads = 1e9f;
                for (const CVector& s : g_spawnsNow) fromSquads = std::min(fromSquads, (s - p).Length());
                float away = g_spawnsNow.empty() ? std::min(fromSquads, 40.0f) : (p - squadsMid).Length();
                float score = away * 2.0f + std::min(dp, 25.0f) * 0.5f + Rand(0.0f, 4.0f) - (fromSquads < 8.0f ? 20.0f : 0.0f);
                if (score + 6.0f <= best) continue;
                if (!PlayersSee(p, players)) score += 6.0f;  // out of their sight is better
                if (score > best) best = score, at = p, found = true;
            }
        }
        if (!found) {  // a tiny place: the cell farthest from every player
            float most = -1;
            for (int k : StandCells()) {
                CVector p = g_cells[(size_t)k].p + CVector{0, 0, 1.0f};
                float dp = DistToPlayers(p, players);
                if (dp > most) most = dp, at = p, found = true;
            }
        }
    }
    if (!found) {
        at = SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 10.0f);
        for (int t = 0; t < 6 && (DistToPlayers(at, players) < 12.0f || behind(at)); ++t) at = SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 10.0f);
    }
    g_spawnsNow.push_back(at);
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
        if (dp < want * 0.5f || NearBase(body)) continue;
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
constexpr int kLootN = 6;
// only the plain pistol, the silenced one and a bat (no desert eagle), a little more ammo as the waves go on
const Loot kLoot[3][kLootN] = {
    {{22, 346, 34}, {23, 347, 34}, {5, 336, 1}, {22, 346, 34}, {23, 347, 34}, {5, 336, 1}},
    {{22, 346, 51}, {23, 347, 51}, {5, 336, 1}, {22, 346, 51}, {23, 347, 51}, {5, 336, 1}},
    {{22, 346, 68}, {23, 347, 68}, {5, 336, 1}, {22, 346, 68}, {23, 347, 68}, {5, 336, 1}},
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
    int guns = 1 + (forWave >= 4 ? 1 : 0) + (pl.big && forWave >= 2 ? 1 : 0);  // free ones: pistols and bats only, a few
    int armour = soda ? (forWave % 2 == 0 ? 1 : 0) + (pl.big ? 1 : 0) : 1 + forWave / 3 + (pl.big ? 1 : 0);
    guns = std::min(guns, 2 + (pl.big ? 1 : 0)), armour = std::min(armour, 4);
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
// the arena's money is the game's own (CPlayerInfo::m_nMoney): the HUD shows it, the enemies drop it, the box takes it
int32_t& Cash() { return PlayerInfos()[0].money; }
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
        w.I32(Cash());
        w.U16((uint16_t)std::min(g_kills, 65000));
        w.U16((uint16_t)std::min(g_falls, 65000));
    });
}
// The wave's best player (Titanfall 2's "top player"): the host counts everyone's kills of the wave as it ends; the
// card on the right of every screen, and that player gets a quarter of its health back and $100
std::map<uint8_t, std::pair<int, int>> g_waveStart;  // host: everyone's kills and falls as the wave began
struct Mvp {
    std::string name;
    int kills = 0;
    bool me = false;
    uint64_t at = 0;
} g_mvp;
constexpr uint64_t kMvpMs = 6500;
void MarkWaveStart() {  // host, as a wave begins
    g_waveStart.clear();
    g_waveStart[Net().Online() ? Net().MyId() : 0] = {g_kills, g_falls};
    for (auto& [id, sc] : g_scores) g_waveStart[id] = {sc.kills, sc.falls};
}
constexpr uint32_t kGtaMoney = 0xFF36682C;  // the game's own money colour (its HUD's green)
void AddPoints(int n, uint32_t color = kGtaMoney);
float MaxHealth();
void ShowMvp(uint8_t id, int kills) {  // every game
    bool me = !Net().Online() || id == Net().MyId();
    g_mvp.name = me ? (Net().Online() ? Cfg().nickname : std::string(TR("Вы", "You"))) : Net().PeerName(id);
    g_mvp.kills = kills, g_mvp.me = me, g_mvp.at = NowMs();
    (void)me;  // the title alone, no health nor money any more
}
void PickMvp() {  // host, the wave over
    struct Cand {
        uint8_t id;
        int kills, falls;
    };
    std::vector<Cand> c;
    uint8_t myId = Net().Online() ? Net().MyId() : 0;
    auto add = [&](uint8_t id, int kills, int falls) {
        auto it = g_waveStart.find(id);
        int k0 = it != g_waveStart.end() ? it->second.first : 0, f0 = it != g_waveStart.end() ? it->second.second : 0;
        c.push_back({id, kills - k0, falls - f0});
    };
    add(myId, g_kills, g_falls);
    if (Net().Online())
        for (auto& [id, peer] : Net().Peers())
            if (id != myId)
                if (auto it = g_scores.find(id); it != g_scores.end()) add(id, it->second.kills, it->second.falls);
    const Cand* best = nullptr;
    for (const Cand& x : c)
        if (x.kills > 0 && (!best || x.kills > best->kills || (x.kills == best->kills && x.falls < best->falls))) best = &x;
    if (!best) return;
    uint8_t id = best->id;
    int kills = best->kills;
    Send(A_MVP, [&](Writer& w) {
        w.U8(id);
        w.U16((uint16_t)std::min(kills, 65000));
    });
    ShowMvp(id, kills);
    LOGI("Arena: the wave's best player %u with %d kills", id, kills);
}
void DrawMvp(uint64_t now) {  // as the game's own menu: its dark panel, the gothic title, the menu's blue and slate
    if (!g_mvp.at || now - g_mvp.at > kMvpMs) return;
    auto& g = ui::Gfx();
    float s = g.Scale();
    float t = (float)(now - g_mvp.at);
    float a = std::min(std::clamp(t / 250.0f, 0.0f, 1.0f), std::clamp(((float)kMvpMs - t) / 400.0f, 0.0f, 1.0f));  // fades in and out
    if (a <= 0.02f) return;
    std::string sub = TR("Лучший игрок волны", "The wave's top player");
    std::string kills = Format(TR("Убийств: %d", "Kills: %d"), g_mvp.kills);
    float th = 54 * s;
    float w = std::max({g.TextWidth(ui::FONT_BIG, g_mvp.name), g.TextWidth(ui::FONT_MENU, sub), g.GameTextWidth(ui::GAME_FONT_GOTHIC, "MVP", th)}) + 64 * s;
    float h = th + g.LineHeight(ui::FONT_MENU) * 2 + g.LineHeight(ui::FONT_BIG) + 52 * s;
    // under the break's line on the right ("Пауза ... [Y] пропустить"), not over it
    float x = g.Width() - w - 24 * s, y = g.Height() * 0.36f + g.LineHeight(ui::FONT_NORMAL) * 2 + 34 * s;
    g.Rect(x, y, w, h, ui::WithAlpha(0xE0000000, a));
    g.RectOutline(x, y, w, h, ui::WithAlpha(0xFF26303B, a), std::max(1.0f, s));
    float cx = x + w * 0.5f, ty = y + 12 * s;
    g.GameText(ui::GAME_FONT_GOTHIC, cx, ty, "MVP", th, ui::WithAlpha(0xFFACCBF1, a), ui::TEXT_CENTER);  // the menu's titles
    ty += th + 8 * s;
    g.Text(ui::FONT_MENU, cx, ty, sub, ui::WithAlpha(0xFF8D9CAD, a), ui::TEXT_CENTER | ui::TEXT_SHADOW);
    ty += g.LineHeight(ui::FONT_MENU) + 8 * s;
    g.Text(ui::FONT_BIG, cx, ty, g_mvp.name, ui::WithAlpha(g_mvp.me ? 0xFFACCBF1 : 0xFFFFFFFF, a), ui::TEXT_CENTER | ui::TEXT_SHADOW);
    ty += g.LineHeight(ui::FONT_BIG) + 8 * s;
    g.Text(ui::FONT_MENU, cx, ty, kills, ui::WithAlpha(0xFF5FA04E, a), ui::TEXT_CENTER | ui::TEXT_SHADOW);
}
// the arena's money (the game's): what the enemies drop, spent at the box, the accessories, the barricades
void AddPoints(int n, uint32_t color) {
    if (n == 0) return;
    Cash() = std::max(0, Cash() + n);
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
int RollGun() {  // every weapon (and the homie) as likely as any other: no weights
    return RandInt(0, (int)(sizeof(kBoxGuns) / sizeof(kBoxGuns[0])) - 1);
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

// ---- where the players come in, how low they may be, and the stairs shut (A_SPOT, the host's once its floor is known)
// The planning department: everyone onto the first upper floor, fences from the floor to the ceiling across the
// stairs down. Every place: one who fell out of it (under its floor, or under the upper floors) is put back.
void Anchor(int h);
struct Blocker {
    CVector pos;  // its floor
    float heading = 0;
    int stack = 1;  // fences one over another, up to the ceiling
    std::vector<int> handles;
};
std::vector<Blocker> g_blockers;
bool g_hasSpot = false;
CVector g_spotAt;        // the players' spot to come in at (an upper place's), the floor
bool g_spotOwn = false;  // there is one (not the place's middle)
float g_lowZ = -1e9f;    // the lowest floor of the place's cells
void ClearBlockers() {
    for (Blocker& b : g_blockers)
        for (int& h : b.handles) DelObj(h);
    g_blockers.clear();
}
float ColWidth(int model) {
    void* info = Ref<void*>(0xA9B0C8 + (uintptr_t)model * 4);  // CModelInfo::ms_modelInfoPtrs
    void* col = info ? Field<void*>(info, 0x14) : nullptr;
    if (!col) return 2.5f;
    CVector mn = Field<CVector>(col, 0x00), mx = Field<CVector>(col, 0x0C);
    return std::clamp(mx.x - mn.x, 1.0f, 8.0f);
}
int BlockerModel() { return PickModel({1418, 1408, 1424}); }  // DYN_F_WOOD_3, DYN_F_WOOD_2, DYN_ROADBARRIER_2
void ShowBlockers() {
    int model = BlockerModel();
    if (model < 0) return;
    for (Blocker& b : g_blockers) {
        for (int& h : b.handles) DelObj(h);
        b.handles.clear();
        float lift = 0, height = 1.2f;
        for (int k = 0; k < b.stack; ++k) {
            ColOf(model, lift, height);
            int h = MakeObj(model, b.pos + CVector{0, 0, (float)k * height * 0.95f}, b.heading, lift, height);
            if (h < 0) continue;
            Anchor(h);
            Cmd(0x0382).I(h).I(1).Run();  // SET_OBJECT_COLLISION
            b.handles.push_back(h);
        }
    }
}
// host: the fences across the stairs down, from the cells at their top (in groups 2.5 m apart, as wide as the group)
void MakeBlockers() {
    ClearBlockers();
    int model = BlockerModel();
    if (model < 0 || g_frontier.empty()) return;
    float width = ColWidth(model), lift = 0, height = 1.2f;
    ColOf(model, lift, height);
    std::vector<bool> used(g_frontier.size(), false);
    for (size_t i = 0; i < g_frontier.size(); ++i) {
        if (used[i]) continue;
        std::vector<size_t> group{i};
        used[i] = true;
        for (size_t g = 0; g < group.size(); ++g)
            for (size_t j = 0; j < g_frontier.size(); ++j)
                if (!used[j] && (g_frontier[j].first - g_frontier[group[g]].first).Length() < 2.5f) used[j] = true, group.push_back(j);
        CVector mid{}, way{};
        for (size_t k : group) {
            mid = mid + g_frontier[k].first;
            CVector w = g_frontier[k].second;
            float l = std::max(0.01f, w.Length2D());
            way = way + w * (1.0f / l);
        }
        mid = mid * (1.0f / (float)group.size());
        float wl = way.Length2D();
        if (wl < 0.01f) continue;
        way = way * (1.0f / wl);
        way.z = 0;
        CVector side{-way.y, way.x, 0.0f};
        float a = 1e9f, b = -1e9f;
        for (size_t k : group) {
            float t = (g_frontier[k].first.x - mid.x) * side.x + (g_frontier[k].first.y - mid.y) * side.y;
            a = std::min(a, t), b = std::max(b, t);
        }
        a -= g_step * 0.5f, b += g_step * 0.5f;
        int n = std::max(1, (int)std::ceil((b - a) / (width * 0.95f)));
        CVector at = mid + way * (g_step * 0.5f);
        float floorZ = mid.z;
        if (float z = 0; FloorBelow(at.x, at.y, mid.z + 0.8f, z) && std::fabs(z - mid.z) < 1.0f) floorZ = std::max(z, mid.z - 0.3f);
        // up to the ceiling: nobody climbs over
        CVector up;
        float room = Ray({at.x, at.y, floorZ + 0.3f}, {at.x, at.y, floorZ + 7.0f}, up) ? up.z - floorZ : 3.6f;
        int stack = std::clamp((int)std::ceil(room / (height * 0.95f)), 1, 5);
        for (int k = 0; k < n; ++k) {
            float t = n == 1 ? (a + b) * 0.5f : a + width * 0.5f + (float)k * ((b - a - width) / (float)(n - 1));
            Blocker bl;
            bl.pos = CVector{at.x, at.y, floorZ} + side * t;
            bl.heading = std::atan2(-way.x, way.y) * 57.29578f;
            bl.stack = stack;
            g_blockers.push_back(bl);
        }
    }
    LOGI("Arena: %d stairs down shut (%d fences)", (int)g_blockers.size(), (int)g_blockers.size());
}
void SendSpot() {
    Send(A_SPOT, [](Writer& w) {
        w.U8(g_spotOwn);
        w.Vec({g_spotAt.x, g_spotAt.y, g_spotAt.z});
        w.F32(g_minZ);
        w.F32(g_lowZ);
        w.U8((uint8_t)std::min<size_t>(g_blockers.size(), 40));
        for (size_t i = 0; i < g_blockers.size() && i < 40; ++i) {
            const Blocker& b = g_blockers[i];
            w.Vec({b.pos.x, b.pos.y, b.pos.z});
            w.F32(b.heading);
            w.U8((uint8_t)b.stack);
        }
    });
}
// where this player comes in (an upper place's spot, or the place's middle), a little apart from the others
CVector EntrySpot() {
    const Place& pl = kPlaces[g_arena.place];
    float a = (float)Net().MyId() * 1.25f;
    CVector base = g_spotOwn ? g_spotAt + CVector{0, 0, 1.0f} : pl.pos;
    float r = g_spotOwn ? 0.7f : 1.2f;  // the upper floor's spot has room round it for sure only that far
    return Net().Online() ? base + CVector{std::cos(a) * r, std::sin(a) * r, 0.0f} : base;
}
bool g_spotApplied = false;  // this place's spot taken the players there once
void ForgetSpot() {  // a new place, a new arena: none yet
    ClearBlockers();
    g_hasSpot = g_spotOwn = g_spotApplied = false;
    g_lowZ = g_minZ = -1e9f;
}
// every game: the host's spot came (or the host made it): an upper place takes its players up there
void ApplySpot() {
    g_hasSpot = true;
    bool shown = !g_blockers.empty();
    for (const Blocker& b : g_blockers) shown &= !b.handles.empty();
    if (!shown) ShowBlockers();
    CPlayerPed* me = FindPlayerPed(0);
    bool first = g_spotOwn && !g_spotApplied;  // the place's own spot: everyone there once, as it begins
    if (g_spotOwn) g_spotApplied = true;
    if (g_spotOwn && me && g_arena.taken && !g_spectating && me->Health() > 0.0f && me->AreaCode() == kPlaces[g_arena.place].area &&
        (me->GetPosition().z < g_minZ + 0.5f || first))
        Teleport(kPlaces[g_arena.place].area, EntrySpot(), Rand(0.0f, 360.0f));
}
void OnGridDone() {  // host
    g_lowZ = -1e9f;
    if (!g_cells.empty()) {
        g_lowZ = 1e9f;
        for (const Cell& c : g_cells) g_lowZ = std::min(g_lowZ, c.p.z);
    }
    g_spotOwn = g_minZ > -1e8f && !g_cells.empty();
    if (g_spotOwn) g_spotAt = g_cells[0].p;  // the seed: the middle of the first upper floor
    if (g_hasOwnSpawn) g_spotOwn = true, g_spotAt = g_ownSpawn;  // the host's own point to come in at
    ClearBlockers();
    if (g_spotOwn) MakeBlockers();
    SendSpot();
    ApplySpot();
}
// every frame, every game: this player fell out of the place (under its floor, under the upper floors): back in
void Rescue(uint64_t now) {
    static uint64_t lastMs = 0;
    CPlayerPed* me = FindPlayerPed(0);
    const Place& pl = kPlaces[g_arena.place];
    if (!me || !g_arena.taken || g_spectating || me->Health() <= 0.0f || me->InVehicle() || me->AreaCode() != pl.area || now - lastMs < 2000)
        return;
    float limit = !g_hasSpot ? pl.pos.z - 40.0f : g_minZ > -1e8f ? g_minZ - 2.5f : g_lowZ > -1e8f ? g_lowZ - 12.0f : pl.pos.z - 40.0f;
    if (me->GetPosition().z >= limit) return;
    lastMs = now;
    LOGI("Arena: the player fell out of the place (z %.1f, the least %.1f): back in", me->GetPosition().z, limit);
    Teleport(pl.area, EntrySpot(), Rand(0.0f, 360.0f));
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
    if (by == Net().MyId() && b.boards > was && RepairCost() > 0) AddPoints(-RepairCost());  // a board nailed on costs 10 points
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
void Anchor(int h);
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
        Anchor(h);  // shots and pushes do not move it
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
// ---- the clothes box: by the weapon box; E and $600: a random piece of CJ's clothes, any he can wear (a top, trousers,
// shoes, a chain, a watch, glasses or a mask, a hat, a costume: 255 of them), each kind with its own perk. One piece a
// body part (a new one takes the old one's place, and its perk), the kinds add up. They are CJ's own clothes
// (GIVE_PLAYER_CLOTHES_OUTSIDE_SHOP), so the other players see them too; what CJ wore before comes back after the arena.
// every piece of clothing has its own perk (the same piece always the same one): what each kind of piece can give is
// its part's list below, the pieces of a part take its perks in turn
enum OutfitPerk : uint8_t {
    O_ARMOUR25, O_ARMOUR50, O_HEALTH125, O_HEALTH150, O_REGEN1, O_REGEN2, O_SPRINT, O_RELOAD, O_CASH25, O_CASH50,
    O_GRENADE, O_MOLOTOV, O_AMMO, O_TOUGH, O_FIREPROOF, O_BLASTPROOF, O_KILLHEAL, O_KILLARMOUR, O_SKILL, O_BOX, O_REPAIR,
    O_SUIT,
    // Brotato-like effects
    O_MAGNET, O_BOUNTY, O_LIFESTEAL, O_SCAVENGER, O_INTEREST, O_LASTSTAND, O_GREEDY, O_PERK_COUNT
};
const char* const kPerkText[O_PERK_COUNT][2] = {
    TRP("+25 брони каждую волну", "+25 armour each wave"),
    TRP("+50 брони каждую волну", "+50 armour each wave"),
    TRP("125 здоровья", "125 health"),
    TRP("150 здоровья", "150 health"),
    TRP("здоровье понемногу восстанавливается", "health slowly comes back"),
    TRP("здоровье быстро восстанавливается", "health comes back fast"),
    TRP("бег без усталости", "running without tiring"),
    TRP("быстрая перезарядка", "fast reload"),
    TRP("+25% денег", "+25% money"),
    TRP("+50% денег", "+50% money"),
    TRP("+1 граната каждую волну", "+1 grenade each wave"),
    TRP("+1 молотов каждую волну", "+1 molotov each wave"),
    TRP("+1 обойма к оружию каждую волну", "+1 clip for your guns each wave"),
    TRP("урон по вам на 15% меньше", "15% less damage to you"),
    TRP("огонь жжёт вдвое слабее", "fire burns half as much"),
    TRP("взрывы бьют вдвое слабее", "blasts hit half as hard"),
    TRP("+5 здоровья за убийство", "+5 health a kill"),
    TRP("+5 брони за убийство", "+5 armour a kill"),
    TRP("навык оружия растёт вдвое быстрее", "weapon skill grows twice as fast"),
    TRP("ящик с оружием на $200 дешевле", "the weapon box $200 cheaper"),
    TRP("доски на баррикады бесплатно", "barricade boards for free"),
    TRP("150 здоровья и +50 брони каждую волну", "150 health and +50 armour each wave"),
    TRP("25% шанс сразу забрать деньги с убитого", "25% chance to take a kill's money at once"),
    TRP("+$5 за каждое убийство", "+$5 a kill"),
    TRP("10% шанс +5 здоровья при попадании", "10% chance of +5 health a hit"),
    TRP("15% шанс получить обойму за убийство", "15% chance of a clip a kill"),
    TRP("в начале волны +5% ваших денег (до $100)", "at each wave +5% of your money (up to $100)"),
    TRP("при здоровье ниже 25% урон по вам на 40% меньше", "under 25% health 40% less damage to you"),
    TRP("+$15 за убийство, но урон по вам на 10% больше", "+$15 a kill, but 10% more damage to you"),
};
struct Cloth {
    const char* texture;
    const char* model;
    int part;
};
// CJ's clothes (the shops' and the wardrobe's): torso, legs, shoes, chain, watch, glasses, hat, suit (texture, model, part)
const Cloth kClothes[] = {
    {"vestblack", "vest", 0}, {"vest", "vest", 0}, {"tshirt2horiz", "tshirt2", 0}, {"tshirtwhite", "tshirt", 0},
    {"tshirtilovels", "tshirt", 0}, {"tshirtblunts", "tshirt", 0}, {"shirtbplaid", "shirtb", 0},
    {"shirtbcheck", "shirtb", 0}, {"field", "field", 0}, {"tshirterisyell", "tshirt", 0},
    {"tshirterisorn", "tshirt", 0}, {"trackytop2eris", "trackytop1", 0}, {"bbjackrim", "bbjack", 0},
    {"bballjackrstar", "bbjack", 0}, {"baskballdrib", "baskball", 0}, {"baskballrim", "baskball", 0},
    {"sixtyniners", "tshirt", 0}, {"bandits", "baseball", 0}, {"tshirtprored", "tshirt", 0},
    {"tshirtproblk", "tshirt", 0}, {"trackytop1pro", "trackytop1", 0}, {"hockeytop", "sweat", 0},
    {"bbjersey", "sleevt", 0}, {"shellsuit", "trackytop1", 0}, {"tshirtheatwht", "tshirt", 0},
    {"tshirtbobomonk", "tshirt", 0}, {"tshirtbobored", "tshirt", 0}, {"tshirtbase5", "tshirt", 0},
    {"tshirtsuburb", "tshirt", 0}, {"hoodyamerc", "hoodya", 0}, {"hoodyabase5", "hoodya", 0},
    {"hoodyarockstar", "hoodya", 0}, {"wcoatblue", "wcoat", 0}, {"coach", "coach", 0}, {"coachsemi", "coach", 0},
    {"sweatrstar", "sweat", 0}, {"hoodyAblue", "hoodyA", 0}, {"hoodyAblack", "hoodyA", 0},
    {"hoodyAgreen", "hoodyA", 0}, {"sleevtbrown", "sleevt", 0}, {"shirtablue", "shirta", 0},
    {"shirtayellow", "shirta", 0}, {"shirtagrey", "shirta", 0}, {"shirtbgang", "shirtb", 0},
    {"tshirtzipcrm", "tshirt", 0}, {"tshirtzipgry", "tshirt", 0}, {"denimfade", "denim", 0}, {"bowling", "hawaii", 0},
    {"hoodjackbeige", "hoodjack", 0}, {"baskballloc", "baskball", 0}, {"tshirtlocgrey", "tshirt", 0},
    {"tshirtmaddgrey", "tshirt", 0}, {"tshirtmaddgrn", "tshirt", 0}, {"suit1grey", "suit1", 0},
    {"suit1blk", "suit1", 0}, {"leather", "leather", 0}, {"painter", "painter", 0}, {"hawaiiwht", "hawaii", 0},
    {"hawaiired", "hawaii", 0}, {"sportjack", "trackytop1", 0}, {"suit1red", "suit1", 0}, {"suit1blue", "suit1", 0},
    {"suit1yellow", "suit1", 0}, {"suit2grn", "suit2", 0}, {"tuxedo", "suit2", 0}, {"suit1gang", "suit1", 0},
    {"letter", "sleevt", 0}, {"worktrcamogrn", "worktr", 2}, {"worktrcamogry", "worktr", 2},
    {"worktrgrey", "worktr", 2}, {"worktrkhaki", "worktr", 2}, {"tracktr", "tracktr", 2},
    {"tracktreris", "tracktr", 2}, {"jeansdenim", "jeans", 2}, {"legsblack", "legs", 2}, {"legsheart", "legs", 2},
    {"biegetr", "chinosb", 2}, {"tracktrpro", "tracktr", 2}, {"tracktrwhstr", "tracktr", 2},
    {"tracktrblue", "tracktr", 2}, {"tracktrgang", "tracktr", 2}, {"bbshortwht", "boxingshort", 2},
    {"boxshort", "boxingshort", 2}, {"bbshortred", "boxingshort", 2}, {"shellsuittr", "tracktr", 2},
    {"shortsgrey", "shorts", 2}, {"shortskhaki", "shorts", 2}, {"chongergrey", "chonger", 2},
    {"chongergang", "chonger", 2}, {"chongerred", "chonger", 2}, {"chongerblue", "chonger", 2},
    {"shortsgang", "shorts", 2}, {"denimsgang", "jeans", 2}, {"denimsred", "jeans", 2}, {"chinosbiege", "chinosb", 2},
    {"chinoskhaki", "chinosb", 2}, {"cutoffchinos", "shorts", 2}, {"cutoffchinosblue", "shorts", 2},
    {"chinosblack", "chinosb", 2}, {"chinosblue", "chinosb", 2}, {"leathertr", "leathertr", 2},
    {"leathertrchaps", "leathertr", 2}, {"suit1trgrey", "suit1tr", 2}, {"suit1trblk", "suit1tr", 2},
    {"cutoffdenims", "shorts", 2}, {"suit1trred", "suit1tr", 2}, {"suit1trblue", "suit1tr", 2},
    {"suit1tryellow", "suit1tr", 2}, {"suit1trgreen", "suit1tr", 2}, {"suit1trblk2", "suit1tr", 2},
    {"suit1trgang", "suit1tr", 2}, {"cowboyboot2", "biker", 3}, {"bask2semi", "bask1", 3}, {"bask1eris", "bask1", 3},
    {"sneakerbincgang", "sneaker", 3}, {"sneakerbincblu", "sneaker", 3}, {"sneakerbincblk", "sneaker", 3},
    {"sandal", "flipflop", 3}, {"sandalsock", "flipflop", 3}, {"flipflop", "flipflop", 3}, {"hitop", "bask1", 3},
    {"convproblk", "conv", 3}, {"convproblu", "conv", 3}, {"convprogrn", "conv", 3}, {"sneakerprored", "sneaker", 3},
    {"sneakerproblu", "sneaker", 3}, {"sneakerprowht", "sneaker", 3}, {"bask1prowht", "bask1", 3},
    {"bask1problk", "bask1", 3}, {"boxingshoe", "biker", 3}, {"convheatblk", "conv", 3}, {"convheatred", "conv", 3},
    {"convheatorn", "conv", 3}, {"sneakerheatwht", "sneaker", 3}, {"sneakerheatgry", "sneaker", 3},
    {"sneakerheatblk", "sneaker", 3}, {"bask2heatwht", "bask1", 3}, {"bask2heatband", "bask1", 3},
    {"timbergrey", "bask1", 3}, {"timberred", "bask1", 3}, {"timberfawn", "bask1", 3}, {"timberhike", "bask1", 3},
    {"cowboyboot", "biker", 3}, {"biker", "biker", 3}, {"snakeskin", "biker", 3}, {"shoedressblk", "shoe", 3},
    {"shoedressbrn", "shoe", 3}, {"shoespatz", "shoe", 3}, {"dogtag", "neck", 13}, {"neckafrica", "neck", 13},
    {"stopwatch", "neck", 13}, {"necksaints", "neck", 13}, {"neckhash", "neck", 13}, {"necksilver", "neck2", 13},
    {"neckgold", "neck2", 13}, {"neckropes", "neck2", 13}, {"neckropeg", "neck2", 13}, {"neckls", "neck", 13},
    {"neckdollar", "neck", 13}, {"neckcross", "neck", 13}, {"watchpink", "watch", 14}, {"watchyellow", "watch", 14},
    {"watchpro", "watch", 14}, {"watchpro2", "watch", 14}, {"watchsub1", "watch", 14}, {"watchsub2", "watch", 14},
    {"watchzip1", "watch", 14}, {"watchzip2", "watch", 14}, {"watchgno", "watch", 14}, {"watchgno2", "watch", 14},
    {"watchcro", "watch", 14}, {"watchcro2", "watch", 14}, {"groucho", "grouchos", 15}, {"zorro", "zorromask", 15},
    {"eyepatch", "eyepatch", 15}, {"glasses01", "glasses01", 15}, {"glasses04", "glasses04", 15},
    {"bandred3", "bandmask", 15}, {"bandblue3", "bandmask", 15}, {"bandgang3", "bandmask", 15},
    {"bandblack3", "bandmask", 15}, {"glasses01dark", "glasses01", 15}, {"glasses04dark", "glasses04", 15},
    {"glasses03", "glasses03", 15}, {"glasses03red", "glasses03", 15}, {"glasses03blue", "glasses03", 15},
    {"glasses03dark", "glasses03", 15}, {"glasses05dark", "glasses03", 15}, {"glasses05", "glasses03", 15},
    {"bandred", "bandana", 16}, {"bandblue", "bandana", 16}, {"bandgang", "bandana", 16},
    {"bandblack", "bandana", 16}, {"bandred2", "bandknots", 16}, {"bandblue2", "bandknots", 16},
    {"bandblack2", "bandknots", 16}, {"bandgang2", "bandknots", 16}, {"capknitgrn", "capknit", 16},
    {"captruck", "captruck", 16}, {"cowboy", "cowboy", 16}, {"hattiger", "cowboy", 16}, {"helmet", "helmet", 16},
    {"moto", "moto", 16}, {"boxingcap", "boxingcap", 16}, {"hockey", "hockeymask", 16}, {"capgang", "cap", 16},
    {"capgangback", "capback", 16}, {"capgangside", "capside", 16}, {"capgangover", "capovereye", 16},
    {"capgangup", "caprimup", 16}, {"bikerhelmet", "bikerhelmet", 16}, {"capred", "cap", 16},
    {"capredback", "capback", 16}, {"capredside", "capside", 16}, {"capredover", "capovereye", 16},
    {"capredup", "caprimup", 16}, {"capblue", "cap", 16}, {"capblueback", "capback", 16},
    {"capblueside", "capside", 16}, {"capblueover", "capovereye", 16}, {"capblueup", "caprimup", 16},
    {"skullyblk", "skullycap", 16}, {"skullygrn", "skullycap", 16}, {"hatmancblk", "hatmanc", 16},
    {"hatmancplaid", "hatmanc", 16}, {"capzip", "cap", 16}, {"capzipback", "capback", 16},
    {"capzipside", "capside", 16}, {"capzipover", "capovereye", 16}, {"capzipup", "caprimup", 16},
    {"beretred", "beret", 16}, {"beretblk", "beret", 16}, {"capblk", "cap", 16}, {"capblkback", "capback", 16},
    {"capblkside", "capside", 16}, {"capblkover", "capovereye", 16}, {"capblkup", "caprimup", 16},
    {"trilbydrk", "trilby", 16}, {"trilbylght", "trilby", 16}, {"bowler", "bowler", 16}, {"bowlerred", "bowler", 16},
    {"bowlerblue", "bowler", 16}, {"bowleryellow", "bowler", 16}, {"boater", "boater", 16},
    {"bowlergang", "bowler", 16}, {"boaterblk", "boater", 16}, {"gimpleg", "gimpleg", 17}, {"valet", "valet", 17},
    {"countrytr", "countrytr", 17}, {"croupier", "valet", 17}, {"policetr", "policetr", 17},
    {"balaclava", "balaclava", 17}, {"pimptr", "pimptr", 17}, {"garageleg", "garagetr", 17},
    {"medictr", "medictr", 17},
};
// 255 pieces
constexpr int kClothCount = (int)(sizeof(kClothes) / sizeof(kClothes[0]));
struct PartPerk {
    int part;
    const char* name[2];
    std::vector<OutfitPerk> perks;  // what its pieces give, in turn
};
const PartPerk kParts[] = {
    {0, TRP("Верх", "Top"), {O_ARMOUR25, O_TOUGH, O_ARMOUR50, O_FIREPROOF, O_KILLARMOUR, O_BLASTPROOF, O_HEALTH125, O_REGEN1, O_LASTSTAND, O_LIFESTEAL}},
    {2, TRP("Штаны", "Trousers"), {O_HEALTH125, O_SPRINT, O_AMMO, O_HEALTH150, O_KILLHEAL, O_TOUGH, O_BLASTPROOF, O_SCAVENGER, O_MAGNET}},
    {3, TRP("Обувь", "Shoes"), {O_SPRINT, O_KILLHEAL, O_FIREPROOF, O_RELOAD, O_BLASTPROOF, O_REGEN1, O_MAGNET, O_LASTSTAND}},
    {13, TRP("Цепь", "Chain"), {O_CASH25, O_SKILL, O_CASH50, O_BOX, O_KILLARMOUR, O_REGEN1, O_INTEREST, O_GREEDY, O_BOUNTY}},
    {14, TRP("Часы", "Watch"), {O_RELOAD, O_SKILL, O_AMMO, O_REPAIR, O_BOX, O_CASH25, O_INTEREST, O_SCAVENGER}},
    {15, TRP("Очки или маска", "Glasses or a mask"), {O_REGEN1, O_SKILL, O_REGEN2, O_KILLHEAL, O_TOUGH, O_CASH25, O_LIFESTEAL, O_BOUNTY}},
    {16, TRP("Головной убор", "Headwear"), {O_GRENADE, O_ARMOUR25, O_MOLOTOV, O_TOUGH, O_REGEN1, O_KILLARMOUR, O_CASH25, O_BLASTPROOF, O_SKILL, O_GREEDY, O_MAGNET}},
    {17, TRP("Костюм", "Costume"), {O_SUIT, O_HEALTH150, O_ARMOUR50, O_TOUGH, O_REGEN2, O_FIREPROOF, O_SKILL, O_CASH50, O_AMMO}},
};
// what a piece is called, by its model (the part's name when not here)
struct ClothName {
    const char* model;
    const char* name[2];
};
const ClothName kClothNames[] = {
    {"vest", TRP("Майка", "Vest")}, {"tshirt", TRP("Футболка", "T-shirt")}, {"tshirt2", TRP("Футболка", "T-shirt")},
    {"shirtb", TRP("Рубашка", "Shirt")}, {"shirta", TRP("Рубашка", "Shirt")}, {"field", TRP("Куртка", "Field jacket")},
    {"trackytop1", TRP("Олимпийка", "Track top")}, {"bbjack", TRP("Бомбер", "Bomber jacket")},
    {"baskball", TRP("Баскетбольная майка", "Basketball top")}, {"baseball", TRP("Бейсбольная куртка", "Baseball jacket")},
    {"sweat", TRP("Свитшот", "Sweatshirt")}, {"sleevt", TRP("Лонгслив", "Long sleeve top")}, {"hoodya", TRP("Худи", "Hoodie")},
    {"hoodyA", TRP("Худи", "Hoodie")}, {"wcoat", TRP("Жилет", "Waistcoat")}, {"coach", TRP("Куртка", "Coach jacket")},
    {"denim", TRP("Джинсовка", "Denim jacket")}, {"hawaii", TRP("Гавайская рубашка", "Hawaiian shirt")},
    {"hoodjack", TRP("Куртка с капюшоном", "Hooded jacket")}, {"suit1", TRP("Пиджак", "Suit jacket")},
    {"suit2", TRP("Пиджак", "Suit jacket")}, {"leather", TRP("Кожанка", "Leather jacket")}, {"painter", TRP("Комбинезон", "Overalls")},
    {"worktr", TRP("Рабочие штаны", "Work trousers")}, {"tracktr", TRP("Спортивные штаны", "Track pants")},
    {"jeans", TRP("Джинсы", "Jeans")}, {"legs", TRP("Трусы", "Boxers")}, {"chinosb", TRP("Брюки", "Chinos")},
    {"boxingshort", TRP("Боксёрские шорты", "Boxing shorts")}, {"shorts", TRP("Шорты", "Shorts")},
    {"chonger", TRP("Бриджи", "Long shorts")}, {"leathertr", TRP("Кожаные штаны", "Leather trousers")},
    {"suit1tr", TRP("Брюки", "Suit trousers")}, {"biker", TRP("Ботинки", "Boots")}, {"bask1", TRP("Кроссовки", "Sneakers")},
    {"sneaker", TRP("Кроссовки", "Sneakers")}, {"flipflop", TRP("Шлёпанцы", "Flip-flops")}, {"conv", TRP("Кеды", "Canvas shoes")},
    {"shoe", TRP("Туфли", "Shoes")}, {"neck", TRP("Цепь", "Chain")}, {"neck2", TRP("Цепь", "Chain")}, {"watch", TRP("Часы", "Watch")},
    {"grouchos", TRP("Очки с носом", "Groucho glasses")}, {"zorromask", TRP("Маска Зорро", "Zorro mask")},
    {"eyepatch", TRP("Повязка на глаз", "Eye patch")}, {"glasses01", TRP("Очки", "Glasses")}, {"glasses03", TRP("Очки", "Glasses")},
    {"glasses04", TRP("Очки", "Glasses")}, {"bandmask", TRP("Бандана на лицо", "Face bandana")}, {"bandana", TRP("Бандана", "Bandana")},
    {"bandknots", TRP("Бандана", "Bandana")}, {"capknit", TRP("Шапка", "Knit cap")}, {"captruck", TRP("Кепка", "Trucker cap")},
    {"cap", TRP("Кепка", "Cap")}, {"capback", TRP("Кепка", "Cap")}, {"capside", TRP("Кепка", "Cap")}, {"capovereye", TRP("Кепка", "Cap")},
    {"caprimup", TRP("Кепка", "Cap")}, {"cowboy", TRP("Ковбойская шляпа", "Cowboy hat")}, {"helmet", TRP("Шлем", "Helmet")},
    {"moto", TRP("Мотошлем", "Bike helmet")}, {"bikerhelmet", TRP("Мотошлем", "Bike helmet")},
    {"boxingcap", TRP("Боксёрский шлем", "Boxing headgear")}, {"hockeymask", TRP("Хоккейная маска", "Hockey mask")},
    {"skullycap", TRP("Шапочка", "Skull cap")}, {"hatmanc", TRP("Шляпа", "Hat")}, {"beret", TRP("Берет", "Beret")},
    {"trilby", TRP("Шляпа", "Trilby")}, {"bowler", TRP("Котелок", "Bowler hat")}, {"boater", TRP("Канотье", "Boater")},
    {"gimpleg", TRP("Костюм гимпа", "Gimp suit")}, {"valet", TRP("Форма", "Uniform")}, {"countrytr", TRP("Костюм фермера", "Farmer outfit")},
    {"policetr", TRP("Форма копа", "Police uniform")}, {"balaclava", TRP("Балаклава", "Balaclava")},
    {"pimptr", TRP("Костюм сутенёра", "Pimp suit")}, {"garagetr", TRP("Комбинезон механика", "Mechanic overalls")},
    {"medictr", TRP("Форма медика", "Medic uniform")},
};
constexpr int kPartCount = (int)(sizeof(kParts) / sizeof(kParts[0]));
int PartIndex(int part) {
    for (int i = 0; i < kPartCount; ++i)
        if (kParts[i].part == part) return i;
    return -1;
}
constexpr int kWardCost = 600;
struct Wardrobe {
    CVector pos;
    float heading = 0;
    int handle = -1;
} g_ward;
struct StatBox {  // the skills box ($600), beside the weapon box
    CVector pos;
    int handle = -1;
} g_statBox;
int g_worn[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // the piece worn on each kParts' part, -1: none
uint8_t g_maxHealthBefore = 0;
CPedClothesDesc g_clothesBefore;   // CJ's clothes before the first accessory
bool g_clothesSaved = false;
// the perk of a piece: its part's list, the pieces of that part (in the table's order) taking its perks in turn
OutfitPerk PerkOf(int k) {
    static std::vector<OutfitPerk> perks;
    if (perks.empty()) {
        std::map<int, int> nth;  // the pieces of each part so far
        for (int i = 0; i < kClothCount; ++i) {
            int pi = PartIndex(kClothes[i].part);
            const std::vector<OutfitPerk>* list = pi >= 0 ? &kParts[pi].perks : nullptr;
            int n = nth[kClothes[i].part]++;
            perks.push_back(list && !list->empty() ? (*list)[(size_t)n % list->size()] : O_TOUGH);
        }
    }
    return k >= 0 && k < (int)perks.size() ? perks[(size_t)k] : O_TOUGH;
}
const char* ClothTitle(int k) {
    for (const ClothName& n : kClothNames)
        if (std::strcmp(n.model, kClothes[k].model) == 0) return n.name[Lang()];
    int pi = PartIndex(kClothes[k].part);
    return pi >= 0 ? kParts[pi].name[Lang()] : "?";
}
// how many of the pieces worn give it (the suit counts as 150 health and +50 armour)
int PerkCount(OutfitPerk k) {
    int n = 0;
    for (int i = 0; i < kPartCount; ++i) {
        if (g_worn[i] < 0) continue;
        OutfitPerk p = PerkOf(g_worn[i]);
        n += p == k || (p == O_SUIT && (k == O_HEALTH150 || k == O_ARMOUR50));
    }
    return n;
}
bool HasPerk(OutfitPerk k) { return PerkCount(k) > 0; }
int BoxCost() { return HasPerk(O_BOX) ? kBoxCost - 200 : kBoxCost; }
int RepairCost() { return HasPerk(O_REPAIR) ? 0 : kRepairCost; }
// an object nothing moves or breaks (the boxes): no move or turn by a hit or a push, every proof
void Anchor(int h) {
    if (CObject* o = h >= 0 ? GetObject(h) : nullptr) {
        uint32_t& f = o->PhysicalFlags();
        f |= (1u << 4) | (1u << 5) | (1u << 6) | (1u << 13);  // bDisableTurnForce, bDisableMoveForce, bInfiniteMass, bDontApplySpeed
        f |= 0xFC0000;                                       // bullet, fire, collision, melee proof, invulnerable, explosion proof
        o->MoveSpeed() = CVector{};
        o->TurnSpeed() = CVector{};
    }
}
// the box's neighbours on its left (every game, its own): a radio (E: the next station of the game's radio, for this
// player alone) and a drinks machine (E: $10, full health)
struct Prop {
    CVector pos;
    float heading = 0;
    int handle = -1;
};
std::vector<Prop> g_radios, g_sodas;
// the host's own spots for them (/arena радио, /arena газировка, up to 4 each; every game hears them, A_PROPS): with
// none, one of each by the box
std::vector<Prop> g_radioSpots, g_sodaSpots;
int g_station = -1;  // this player's radio: -1 off, 0..10 the game's stations (their audio zones 52..62)
const char* const kStations[11] = {"Playback FM", "K-Rose", "K-DST", "Bounce FM", "SF-UR", "Radio Los Santos", "Radio X",
                                   "CSR 103.9", "K-Jah West", "Master Sounds 98.3", "WCTR"};
constexpr int kSodaCost = 10;
void DelProps() {
    for (Prop& p : g_radios) DelObj(p.handle);
    for (Prop& p : g_sodas) DelObj(p.handle);
    g_radios.clear(), g_sodas.clear();
}
bool BesideBox(float side, CVector& at) {  // on the floor beside the box: side metres to its right (left: below 0)
    float r = g_box.heading / 57.29578f;
    CVector right{std::cos(r), std::sin(r), 0.0f};
    at = g_box.pos + right * side;
    float z = 0;
    if (!FloorBelow(at.x, at.y, at.z + 1.2f, z) || std::fabs(z - g_box.pos.z) > 1.0f) return false;
    at.z = z;
    return Clear(g_box.pos + CVector{0, 0, 0.6f}, at + CVector{0, 0, 0.6f});
}
bool FloorNoBars(float x, float y, float fromZ, float& z);
void ShowProps() {
    DelProps();
    float lift = 0, height = 0;
    int radio = PickModel({2226, 2103, 2102});  // a boombox, a hi-fi
    int soda = PickModel({955, 1775, 956});      // a Sprunk machine
    auto make = [&](std::vector<Prop>& out, int model, const CVector& spot, float heading) {
        Prop p;
        CVector at = spot;
        float fz = 0;  // on the floor itself (the spot is the player's feet, a little off it at times)
        if (FloorNoBars(at.x, at.y, at.z + 0.6f, fz) && std::fabs(fz - at.z) < 1.2f) at.z = fz;
        p.pos = at, p.heading = heading;
        lift = 0, height = 0;  // its own (a model whose collision is not known kept the last one's: it hung in the air)
        p.handle = MakeObj(model, at, heading, lift, height);
        if (p.handle >= 0) Cmd(0x01BC).I(p.handle).F(at.x).F(at.y).F(at.z + lift).Run();  // SET_OBJECT_COORDINATES: its foot on the floor
        Anchor(p.handle);
        LOGI("Arena: prop %d at %.2f %.2f %.2f (lift %.2f, height %.2f)", model, at.x, at.y, at.z, lift, height);
        out.push_back(p);
    };
    CVector at;
    if (radio >= 0) {
        for (const Prop& sp : g_radioSpots) make(g_radios, radio, sp.pos, sp.heading);
        if (g_radioSpots.empty() && g_box.placed && (BesideBox(-2.4f, at) || BesideBox(4.6f, at))) make(g_radios, radio, at, g_box.heading);
    }
    if (soda >= 0) {
        for (const Prop& sp : g_sodaSpots) make(g_sodas, soda, sp.pos, sp.heading);
        if (g_sodaSpots.empty() && g_box.placed && (BesideBox(-4.4f, at) || BesideBox(6.6f, at) || BesideBox(-2.4f, at)))
            make(g_sodas, soda, at, g_box.heading);
    }
}
void SendProps() {  // host
    Send(A_PROPS, [](Writer& w) {
        for (auto* list : {&g_radioSpots, &g_sodaSpots}) {
            w.U8((uint8_t)std::min<size_t>(list->size(), 4));
            for (size_t i = 0; i < list->size() && i < 4; ++i) {
                w.Vec({(*list)[i].pos.x, (*list)[i].pos.y, (*list)[i].pos.z});
                w.F32((*list)[i].heading);
            }
        }
    });
}
void ShowWardrobe() {  // every game: by the box, to its right, on the floor
    ShowProps();
    DelObj(g_ward.handle);
    if (!g_box.placed) return;
    int model = PickModel({1221, 1220, 1230, 2969});  // a cardboard box (anchored: no push, no shot, no blast breaks it)
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
    // its collision reaches lower than the box itself: it hung in the air; down onto the floor
    if (g_ward.handle >= 0) Cmd(0x01BC).I(g_ward.handle).F(at.x).F(at.y).F(at.z + std::max(0.0f, lift - 0.45f)).Run();  // SET_OBJECT_COORDINATES
    LOGI("Arena: the clothes box %d (lift %.2f, height %.2f)", model, lift, height);
    Anchor(g_ward.handle);
    ShowStatBox();
}
void OutfitPerks(bool on) {  // the perks that stay on while worn
    if (HasPerk(O_SPRINT)) Cmd(0x0330).I(0).I(on ? 1 : 0).Run();  // SET_PLAYER_NEVER_GETS_TIRED
    if (HasPerk(O_RELOAD)) Cmd(0x0331).I(0).I(on ? 1 : 0).Run();  // SET_PLAYER_FAST_RELOAD
    g_cashMult = on ? 1.0f + 0.25f * (float)PerkCount(O_CASH25) + 0.5f * (float)PerkCount(O_CASH50) : 1.0f;
    int top = HasPerk(O_HEALTH150) ? 150 : HasPerk(O_HEALTH125) ? 125 : 0;
    if (on && top) {
        if (!g_maxHealthBefore) g_maxHealthBefore = PlayerInfos()[0].maxHealth;
        PlayerInfos()[0].maxHealth = (uint8_t)std::max<int>(top, g_maxHealthBefore);
    } else if (g_maxHealthBefore) {
        PlayerInfos()[0].maxHealth = g_maxHealthBefore;
        g_maxHealthBefore = 0;
    }
}
float MaxHealth() { return std::max<float>(PlayerInfos()[0].maxHealth, 100.0f); }
void ArmourWave();
void OutfitWaveStart() {  // at each wave: the armour, the health, the grenades and the clips
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || g_spectating) return;
    ArmourWave();
    int h = GetPedRef(me);
    float armour = 25.0f * (float)PerkCount(O_ARMOUR25) + 50.0f * (float)PerkCount(O_ARMOUR50);
    if (armour > 0) me->Armour() = std::min(100.0f, me->Armour() + armour);
    if (HasPerk(O_HEALTH150) || HasPerk(O_HEALTH125)) me->Health() = std::max(me->Health(), MaxHealth());
    if (int n = PerkCount(O_GRENADE); n && !SlotTaken(me, WEAPON_GRENADE)) Give(h, WEAPON_GRENADE, n);  // not over molotovs
    if (int n = PerkCount(O_MOLOTOV); n && !SlotTaken(me, WEAPON_MOLOTOV)) Give(h, WEAPON_MOLOTOV, n);  // nor over grenades
    if (int n = PerkCount(O_AMMO))
        for (int slot = 2; slot <= 6; ++slot) {  // the pistols, the shotguns, the sub-machine guns, the rifles
            CWeapon& w = me->Weapons()[slot];
            if (w.type <= 0) continue;
            void* info = Call<0x743C60, void*>(w.type, (uint8_t)1);  // CWeaponInfo::GetWeaponInfo
            int clip = info ? std::max<int>(1, Field<int16_t>(info, 0x20)) : 10;
            w.ammoTotal = std::min(99999, (int)w.ammoTotal + clip * n);
        }
    if (int n = PerkCount(O_INTEREST)) {  // Brotato's piggy bank
        int add = std::min(100 * n, Cash() * 5 * n / 100);
        if (add > 0) AddPoints(add);
    }
}
void BuyOutfit() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me) return;
    if (Cash() < kWardCost) {
        ui::TheChat().AddError(Format(TR("Нужно $%d.", "You need $%d."), kWardCost));
        return;
    }
    int k = RandInt(0, kClothCount - 1);
    int pi = PartIndex(kClothes[k].part);
    if (pi >= 0 && k == g_worn[pi]) k = (k + 1) % kClothCount, pi = PartIndex(kClothes[k].part);  // not the one it wears
    if (pi < 0) return;
    const Cloth& a = kClothes[k];
    AddPoints(-kWardCost);
    OutfitPerks(false);
    g_worn[pi] = k;
    OutfitPerks(true);
    OutfitWaveStart();
    if (me->ModelIndex() == 0) {  // CJ: on him (another character keeps only the perk)
        if (!g_clothesSaved && me->ClothesDesc()) g_clothesBefore = *me->ClothesDesc(), g_clothesSaved = true;
        Cmd(0x087B).I(0).S(a.texture).S(a.model).I(a.part).Run();  // GIVE_PLAYER_CLOTHES_OUTSIDE_SHOP
        RequestRebuild();  // BUILD_PLAYER_MODEL's work, at a safe moment
    }
    CallMethod<0x506EA0>((void*)0xB6BC90, 40 /* AE_FRONTEND_PURCHASE_WEAPON */, 0.0f, 1.0f);
    // its rarity, only a colour for its name: blue (60%), pink (30%), red (10%); the rest of the line as the system's
    int roll = RandInt(0, 99);
    const char* tint = roll < 10 ? "{FF3B3B}" : roll < 40 ? "{FF6EC7}" : "{4DA6FF}";
    ui::TheChat().AddSystem(Format("%s%s{FFC864}: %s", tint, ClothTitle(k), kPerkText[PerkOf(k)][Lang()]));
}
void TakeOffOutfit() {  // the arena is over: the perks go, CJ's clothes from before it come back
    OutfitPerks(false);
    for (int& w : g_worn) w = -1;
    g_cashMult = 1.0f;
    CPlayerPed* me = FindPlayerPed(0);
    if (g_clothesSaved && me && me->ModelIndex() == 0 && me->ClothesDesc()) {
        *me->ClothesDesc() = g_clothesBefore;
        RequestRebuild();
    }
    g_clothesSaved = false;
}

// ---- the homie from the box: a Grove Street man with an M4 who follows this player (its group) and fights the arena's
// people; this player's game keeps him (the others see him as its ped). He is gone with the arena.
std::vector<int> g_guards;
const char* const kGuardNames[5][2] = {TRP("Олэг", "Oleg"), TRP("Виталик", "Vitalik"), TRP("Дементий", "Dementiy"),
                                       TRP("Славик", "Slavik"), TRP("Костя", "Kostya")};
std::map<int, int> g_guardNames;          // a homie here (this game's, or a copy of another's): its name
std::vector<std::pair<uint16_t, int>> g_guardIds;  // copies of other games' homies not here yet: net id, name
void SendGuardName(CPed* p, int name, uint8_t owner) {
    NetPed* np = p ? Peds::Instance().Find(p) : nullptr;
    if (!np) return;
    uint16_t id = np->netId;
    Send(A_GUARDNAME, [&](Writer& w) {
        w.U16(id);
        w.U8((uint8_t)name);
        w.U8(owner);
    });
}
std::vector<int> g_guardGroups;  // host: the groups made for partners' homies (a partner's copy leads each)
// The homie (host): the host's game makes every homie, a partner's too, so that he fights the host's own enemies (a
// partner's game has only copies of them, which nothing it does there hurts). He follows his player: the host's in
// its group, a partner's copy in a group of its own.
void SpawnBodyguard(uint8_t owner) {
    CPlayerPed* me = FindPlayerPed(0);
    bool mine = owner == Net().MyId() || !Net().Online();
    CPed* leader = mine ? (CPed*)me : nullptr;
    if (!mine)
        if (RemotePlayer* rp = Players::Instance().Get(owner)) leader = rp->ped;
    int model = 105 + RandInt(0, 2);
    if (!leader || !IsPedValid(leader) || !LoadModelNow(model) || !HasRoom(PedPool(), kKeepPeds + 2, "ped")) return;
    CVector f = mine ? cam::Front() : CVector{-std::sin(leader->GetHeading()), std::cos(leader->GetHeading()), 0.0f};
    f.z = 0;
    float l = std::max(0.01f, f.Length());
    CVector at = leader->GetPosition() - f * (1.2f / l);
    int h = -1;
    Cmd(0x009A).I(8 /* PED_TYPE_GANG2 */).I(model).F(at.x).F(at.y).F(at.z - 1.0f).Out(&h).Run();  // CREATE_CHAR
    if (h < 0) return;
    if (CPed* made = GetPed(h)) {
        Peds::Instance().Share(made);
        made->Armour() = 100.0f;
    }
    Cmd(0x0860).I(h).I(leader->AreaCode()).Run();  // SET_CHAR_AREA_VISIBLE
    Cmd(0x0223).I(h).I(300).Run();             // SET_CHAR_HEALTH
    Cmd(0x087E).I(h).I(0).Run();               // no weapons off him
    Give(h, 31, 9999);
    Cmd(0x01B9).I(h).I(GunFor(31)).Run();
    Cmd(0x02E2).I(h).I(75).Run();              // SET_CHAR_ACCURACY
    Cmd(0x077A).I(h).I(4).I(PED_TYPE_MISSION1).Run();  // SET_CHAR_RELATIONSHIP: hates the arena's people
    int group = -1;
    if (mine) {
        Cmd(0x07AF).I(0).Out(&group).Run();  // GET_PLAYER_GROUP
    } else {
        Cmd(0x062F).I(0).Out(&group).Run();  // CREATE_GROUP
        if (group >= 0) Cmd(0x0630).I(group).I(GetPedRef(leader)).Run(), g_guardGroups.push_back(group);  // SET_GROUP_LEADER
    }
    if (group >= 0) Cmd(0x0631).I(group).I(h).Run();  // SET_GROUP_MEMBER: he follows and fights for his player
    g_guards.push_back(h);
    int name = RandInt(0, 4);
    g_guardNames[h] = name;
    SendGuardName(GetPed(h), name, owner);
    if (mine) ui::TheChat().AddSystem(Format(TR("Кент %s с вами.", "%s, a homie, is with you."), kGuardNames[name][Lang()]));
}
// this game's homies: at the nearest enemy they see (on a partner's game the enemies are copies, the game's own hate
// does not reach them: set on them by hand); with nobody there they follow their player
void GuardsFrame(uint64_t now) {
    static uint64_t lastMs = 0;
    if (now - lastMs < 1500 || g_guards.empty()) return;
    lastMs = now;
    for (int h : g_guards) {
        CPed* gp = GetPed(h);
        if (!gp || gp->Health() <= 0.0f) continue;
        CPed* best = nullptr;
        float bestD = 35.0f;
        CPoolHeader* pool = PedPool();
        for (int i = 0; pool && i < pool->size; ++i) {
            if (pool->byteMap[i] < 0) continue;
            auto* q = (CPed*)(pool->objects + (size_t)i * kPedPoolElemSize);
            if (q == gp || q->IsPlayer() || q->Health() <= 0.0f || !arena::IsEnemy(q) || q == g_merchant.ped) continue;
            if (std::find(g_guards.begin(), g_guards.end(), GetPedRef(q)) != g_guards.end() || g_guardNames.count(GetPedRef(q))) continue;
            float d = (q->GetPosition() - gp->GetPosition()).Length();
            if (d < bestD && Clear(gp->GetPosition() + CVector{0, 0, 0.6f}, q->GetPosition() + CVector{0, 0, 0.6f})) bestD = d, best = q;
        }
        if (best) Cmd(0x05E2).I(h).I(GetPedRef(best)).Run();  // TASK_KILL_CHAR_ON_FOOT
    }
}
void GuardsTo(const CVector& at, int area) {  // with this player to another place
    for (int h : g_guards)
        if (CPed* p = GetPed(h); p && p->Health() > 0.0f) {
            Cmd(0x0860).I(h).I(area).Run();
            Cmd(0x00A1).I(h).F(at.x + 1.0f).F(at.y).F(at.z - 1.0f).Run();
        }
}
void GuardCopies() {  // every frame: the names of other games' homies once their copies are here
    for (auto it = g_guardIds.begin(); it != g_guardIds.end();) {
        int h = Peds::Instance().HandleOf(it->first);
        if (h >= 0) g_guardNames[h] = it->second, it = g_guardIds.erase(it);
        else ++it;
    }
    for (auto it = g_guardNames.begin(); it != g_guardNames.end();) it = GetPed(it->first) ? std::next(it) : g_guardNames.erase(it);
}
void ClearGuards() {
    g_guardNames.clear();
    g_guardIds.clear();
    for (int grp : g_guardGroups) Cmd(0x0632).I(grp).Run();  // REMOVE_GROUP
    g_guardGroups.clear();
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
    if (by == Net().MyId() || !Net().Online()) AddPoints(-BoxCost());
}
// host: a player opens it (its points it checked itself)
void UseBox(uint8_t by) {
    if (!g_box.placed || g_box.rolling || g_box.offered || g_box.moveAt) return;
    ++g_box.uses;
    bool teddy = false;  // no teddy bear any more
    int gun = RollGun();
    Send(A_BOXROLL, [&](Writer& w) {
        w.U8(by);
        w.U8((uint8_t)gun);
        w.U8(teddy);
    });
    StartRoll(by, gun, teddy);
}
void SpawnBodyguard(uint8_t owner);
// E at the box while its weapon waits for this player: it takes it (a weapon in the same slot goes for it)
void TakeOffer() {
    if (!g_box.offered || !(g_box.by == Net().MyId() || !Net().Online())) return;
    g_box.offered = false;
    Send(A_BOXTAKEN);
    const BoxGun& g = kBoxGuns[(size_t)g_box.gun % (sizeof(kBoxGuns) / sizeof(kBoxGuns[0]))];
    if (!g.weapon) {  // the homie: this player's own, in its game (made by the host's he followed a copy and was not seen)
        SpawnBodyguard(Net().MyId());
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
            if (mine) AddPoints(BoxCost());
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
    if (Cash() < price) {
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
enum DropType : uint8_t { D_AMMO, D_HEALTH, D_SMALL, D_GUN, D_CASH, D_SCRAP };  // D_SCRAP: a downed drone's scrap (2: a Shahed)  // D_CASH: money off a dead enemy (its sum in ammo)
struct Drop {
    uint8_t id = 0, type = 0;
    uint8_t weapon = 0, owner = 0xFF;  // D_GUN: what it is, who put it down (not its to take back for 2 s)
    uint8_t special = 0;               // D_GUN: 1 T-Bone's AK-47 with the grenade launcher under it
    uint16_t ammo = 0;
    CVector pos;
    int handle = -1;
    uint64_t until = 0, born = 0;
    bool asked = false;
    game::CPed* from = nullptr;  // host: D_CASH: the dead one it fell off
};
std::vector<Drop> g_drops;
std::map<game::CPed*, uint8_t> g_magnets;  // host: the dead whose money goes straight to that player (the clothes' magnet)
void TakeDrop(uint8_t id, uint8_t by);
// host: the killer's magnet took this dead one's money: its drop, if out already, else as it falls
void Magnet(game::CPed* dead, uint8_t by) {
    for (const Drop& d : g_drops)
        if (d.type == D_CASH && d.from == dead) return TakeDrop(d.id, by);
    g_magnets[dead] = by;
}
uint8_t g_nextDrop = 1;
const char* WeaponName(int w);
int WeaponModel(int weapon);
std::string DropName(const Drop& d) {
    switch (d.type) {
        case D_AMMO: return TR("Пополнение патронов", "Ammo refill");
        case D_HEALTH: return TR("Пополнение здоровья", "Health refill");
        case D_SMALL: return TR("Патроны", "Ammo");
        case D_CASH: return Format("$%d", (int)d.ammo);
        case D_SCRAP: return TR("Груда хлама", "A heap of scrap");
        default:
            if (d.special == 1) return Format(TR("АК-47 с подствольником · %d", "AK-47 with a grenade launcher · %d"), (int)d.ammo);
            return Format("%s · %d", WeaponName(d.weapon), (int)d.ammo);
    }
}
void ShowDrop(Drop& d) {
    int model = d.type == D_AMMO || d.type == D_SMALL ? PickModel({2358, 2969, 2040})  // an ammo box
                : d.type == D_HEALTH                  ? PickModel({1240, 1580})        // the heart
                : d.type == D_CASH                    ? PickModel({1212, 1550})        // a wad of money, a money bag
                : d.type == D_SCRAP                   ? PickModel({2908, 1265, 1264})  // scrap
                                                      : WeaponModel(d.weapon);
    if (model > 0 && !IsModelLoaded(model)) LoadModelNow(model);
    d.handle = MakeFloating(model, d.pos, 0.0f);
}
void AddDrop(const Drop& from) {
    Drop d = from;
    d.born = NowMs();
    d.until = d.born + (d.type == D_GUN ? 90000 : d.type == D_CASH ? 3600000 : 30000);  // money lies till the arena ends
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
    } else if (d.type == D_SCRAP && mine) {
        if (++g_scrap >= 2) g_scrap -= 2, ++g_shahedCharges;
        ui::TheChat().AddSystem(Format(TR("Груда хлама: %d/2 (зарядов Shahed: %d).", "Scrap: %d/2 (Shahed charges: %d)."), g_scrap, g_shahedCharges));
    } else if (d.type == D_CASH && mine) {
        AddPoints((int)((float)d.ammo * g_cashMult));
        CallMethod<0x506EA0>((void*)0xB6BC90, 7 /* AE_FRONTEND_PICKUP_MONEY */, 0.0f, 1.0f);
    } else if (d.type == D_GUN && mine) {
        int h = GetPedRef(me);
        int w = GunFor(d.weapon);
        if (w) Cmd(0x01B2).I(h).I(w).I(std::max<int>(1, d.ammo)).Run(), Cmd(0x01B9).I(h).I(w).Run();
        ui::TheChat().AddSystem(Format(TR("Вы подобрали: %s.", "You picked up: %s."), DropName(d).c_str()));
        if (d.weapon == 9) {
            g_dashCharges = 6;
            ui::TheChat().AddSystem(TR("[N] Рывок бензопилой: 6 рывков.", "[N] The chainsaw's dash: 6 dashes."));
        }
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
    g_magnets.clear();
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
    if (Cash() < kBuildCost) {
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
// Everyone's points of a place, the mod's own (made by the host of the first games: its arena-spots.ini): a kind of
// them in one's own file (spawn, enemy, zone, build) takes the place of these
struct DefaultSpot {
    const char* sec;
    const char* key;
    const char* value;
};
const DefaultSpot kDefaultSpots[] = {
    {"crack", "spawn", "2560.89 -1301.99 1030.42 91.0"},
    {"crack", "zone1", "2540.39 -1295.04 1030.42 -80.1"},
    {"crack", "zone2", "2526.53 -1288.99 1030.42 -22.2"},
    {"crack", "zone3", "2570.26 -1285.57 1036.77 -34.2"},
    {"crack", "zone4", "2550.58 -1289.94 1043.12 -104.6"},
    {"crack", "build1", "2546.17 -1294.86 1030.42 -88.5"},
    {"crack", "build2", "2541.34 -1299.21 1030.42 -175.9"},
    {"crack", "build3", "2525.73 -1290.01 1030.42 -172.6"},
    {"crack", "build4", "2570.87 -1298.43 1036.77 175.4"},
    {"crack", "build5", "2568.98 -1298.72 1036.78 173.1"},
    {"crack", "build6", "2553.70 -1297.20 1043.12 -93.1"},
    {"crack", "build7", "2553.87 -1289.62 1043.12 -87.4"},
    {"crack", "build8", "2552.75 -1287.90 1043.12 -27.3"},
    {"crack", "build9", "2552.81 -1298.59 1043.12 -164.1"},
    {"crack", "build10", "2570.57 -1294.35 1036.77 -177.5"},
    {"motel", "spawn", "2244.32 -1190.78 1028.80 45.1"},
    {"motel", "enemy1", "2237.24 -1150.52 1028.80 88.1"},
    {"motel", "enemy2", "2192.53 -1182.65 1028.80 1.6"},
    {"motel", "merchant", "2198.94 -1188.60 1028.80 -89.9"},
    {"motel", "merchantto", "2225.79 -1188.41 1028.80 -90.9"},
    {"crack", "merchant", "2527.32 -1293.76 1030.42 -89.0"},
    {"crack", "merchantto", "2535.03 -1296.64 1030.42 -91.4"},
    {"mansion", "spawn", "1272.61 -835.76 1084.63 87.2"},
    {"mansion", "radio1", "1272.09 -828.11 1084.63 2.0"},
    {"mansion", "soda1", "1275.26 -824.54 1084.63 0.5"},
    {"mansion", "soda2", "1273.57 -824.54 1084.63 0.5"},
    {"mansion", "merchant", "1239.03 -832.37 1083.01 -112.3"},
    {"mansion", "merchantto", "1262.95 -837.68 1083.01 -13.3"},
    {"rc", "merchant", "-979.35 1042.08 1343.99 0.5"},
    {"rc", "merchantto", "-979.43 1055.60 1343.98 -57.6"},
    {"planning", "radio1", "353.38 178.23 1013.19 -5.9"},
    {"planning", "soda1", "345.95 169.33 1013.19 -178.6"},
    {"planning", "soda2", "343.59 169.33 1013.19 178.8"},
    {"motel", "soda1", "2234.62 -1192.53 1028.80 91.7"},
    {"motel", "radio1", "2248.91 -1190.91 1029.42 -90.3"},
    {"rc", "radio1", "-973.44 1063.54 1344.01 1.0"},
    {"rc", "soda1", "-974.65 1086.11 1343.98 -0.2"},
};
// a kind's points: the file's, or (none of that kind there) the mod's own
std::vector<std::string> SpotLines(const Ini* ini, const std::string& sec, const char* kind, int most) {
    std::vector<std::string> out;
    bool single = most == 0;
    for (int k = 1; k <= std::max(1, most); ++k) {
        std::string key = single ? std::string(kind) : Format("%s%d", kind, k);
        if (ini) {
            std::string v = ini->Get(sec, key);
            if (!v.empty()) out.push_back(v);
        }
    }
    if (!out.empty()) return out;
    for (int k = 1; k <= std::max(1, most); ++k) {
        std::string key = single ? std::string(kind) : Format("%s%d", kind, k);
        for (const DefaultSpot& d : kDefaultSpots)
            if (sec == d.sec && key == d.key) out.push_back(d.value);
    }
    return out;
}
// the players' own spot of a place (the host's file's, or the mod's): true when there is one
bool OwnSpawnOf(int place, CVector& at) {
    Ini ini;
    bool have = ini.Load(SpotsPath());
    float h = 0;
    for (const std::string& v : SpotLines(have ? &ini : nullptr, kPlaces[place].keys[0][1], "spawn", 0))
        if (ReadVec(v, at, h)) return true;
    return false;
}
// host: the players' spot, the enemies' points and the zones of this place (its own file's, or the mod's own)
void LoadOwnPoints() {
    g_hasOwnSpawn = false;
    g_enemyPts.clear();
    g_zones.clear();
    g_radioSpots.clear(), g_sodaSpots.clear();
    g_buildSpots.clear();
    Ini ini;
    bool have = ini.Load(SpotsPath());
    const Ini* file = have ? &ini : nullptr;
    std::string sec = PlaceSection();
    CVector p;
    float h = 0;
    for (const std::string& v : SpotLines(file, sec, "spawn", 0))
        if (ReadVec(v, p, h)) g_hasOwnSpawn = true, g_ownSpawn = p;
    g_hasMerchantSpot = g_hasMerchantTo = false;
    for (const std::string& v : SpotLines(file, sec, "merchant", 0))
        if (ReadVec(v, p, h)) g_hasMerchantSpot = true, g_merchantSpot = p;
    for (const std::string& v : SpotLines(file, sec, "merchantto", 0))
        if (ReadVec(v, p, h)) g_hasMerchantTo = true, g_merchantTo = p;
    for (const std::string& v : SpotLines(file, sec, "enemy", 8))
        if (ReadVec(v, p, h)) g_enemyPts.push_back(p);
    for (const std::string& v : SpotLines(file, sec, "zone", 4))
        if (ReadVec(v, p, h)) g_zones.push_back(p);
    for (const std::string& v : SpotLines(file, sec, "build", kMaxBuildSpots))
        if (ReadVec(v, p, h)) g_buildSpots.push_back({p, h});
    for (const std::string& v : SpotLines(file, sec, "radio", 4)) {
        Prop pr;
        if (ReadVec(v, p, h)) pr.pos = p, pr.heading = h, g_radioSpots.push_back(pr);
    }
    for (const std::string& v : SpotLines(file, sec, "soda", 4)) {
        Prop pr;
        if (ReadVec(v, p, h) && g_sodaSpots.size() < 2) pr.pos = p, pr.heading = h, g_sodaSpots.push_back(pr);
    }
    if (g_hasOwnSpawn || !g_enemyPts.empty() || !g_zones.empty() || !g_buildSpots.empty())
        LOGI("Arena: own points here: spawn %d, %d enemy points, %d zones, %d builder spots", g_hasOwnSpawn ? 1 : 0, (int)g_enemyPts.size(),
             (int)g_zones.size(), (int)g_buildSpots.size());
}
// host: the saved spots instead of the mod's own; true when there were some
bool LoadSpots() {
    Ini ini;
    bool file = ini.Load(SpotsPath());
    std::string sec = PlaceSection();
    const Place& pl = kPlaces[g_arena.place];
    bool any = false;
    CVector p;
    float h = 0;
    if (file && ReadVec(ini.Get(sec, "box"), p, h)) {
        g_box.placed = true, g_box.pos = p, g_box.heading = h;
        ShowBox();
        SendBox();
        any = true;
    } else if (pl.box.x != 0.0f || pl.box.y != 0.0f) {  // the place's own spot for the box (the mod's, for everyone)
        g_box.placed = true, g_box.pos = pl.box, g_box.heading = pl.boxHeading;
        ShowBox();
        SendBox();
    }
    if (!file) return any;
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
    } else if (what == "radio" || what == "soda") {
        bool radio = what == "radio";
        std::vector<Prop>& list = radio ? g_radioSpots : g_sodaSpots;
        int most = radio ? 4 : 2, k = 1;  // drinks machines: 2 a place at most
        while (k <= most && !ini.Get(sec, Format("%s%d", what.c_str(), k)).empty()) ++k;
        if (k > most) {
            ui::TheChat().AddError(radio ? TR("Уже 4 для этого места.", "4 for this place already.")
                                         : TR("Уже 2 автомата для этого места (сбросить: /arena газировка сброс).",
                                              "2 machines for this place already (forget them: /arena soda reset)."));
            return;
        }
        ini.Set(sec, Format("%s%d", what.c_str(), k), line);
        Prop pr;
        pr.pos = p, pr.heading = h;
        list.push_back(pr);
        ShowProps();
        if (g_arena.host) SendProps();
        ui::TheChat().AddSystem(Format(radio ? TR("Радио %d поставлено здесь (сохранено для этого места).", "Radio %d put here (kept for this place).")
                                             : TR("Автомат %d поставлен здесь (сохранено для этого места).", "Machine %d put here (kept for this place)."), k));
    } else if (what.size() > 5 && what.compare(what.size() - 5, 5, "reset") == 0) {  // one kind forgotten: /arena <kind> сброс
        std::string kind = what.substr(0, what.size() - 5);
        int most = kind == "merchant" || kind == "merchantto" ? 0 : kind == "enemy" ? 8 : kind == "zone" || kind == "radio" || kind == "soda" ? 4 : kind == "bar" ? kMaxBars
                 : kind == "build" ? kMaxBuildSpots : 0;
        if (most == 0) ini.Set(sec, kind, "");  // box, spawn: one each
        for (int k = 1; k <= most; ++k) ini.Set(sec, Format("%s%d", kind.c_str(), k), "");
        ini.Save(SpotsPath());
        const char* text =
            kind == "box"     ? TR("Ящик этого места сброшен: со следующей арены он на своём месте по умолчанию.",
                                   "The box of this place forgotten: from the next arena it is at its own spot again.")
            : kind == "spawn" ? TR("Точка появления игроков сброшена (снова по умолчанию, со следующей арены).",
                                   "The players' spot forgotten (the default again, from the next arena on).")
            : kind == "enemy" ? TR("Все точки врагов этого места сброшены (снова по умолчанию).", "All the enemy points of this place forgotten (the defaults again).")
            : kind == "zone"  ? TR("Все зоны этого места сброшены (снова по умолчанию).", "All the zones of this place forgotten (the defaults again).")
            : kind == "radio" ? TR("Все радио этого места сброшены (снова одно у ящика).", "All the radios of this place forgotten (one by the box again).")
            : kind == "soda"  ? TR("Все автоматы с напитками этого места сброшены (снова один у ящика).",
                                   "All the drinks machines of this place forgotten (one by the box again).")
            : kind == "bar"   ? TR("Все сохранённые баррикады этого места сброшены (со следующей арены).",
                                   "All the kept barricades of this place forgotten (from the next arena on).")
                              : TR("Все места строителя этого места сброшены (снова по умолчанию).", "All the builder spots of this place forgotten (the defaults again).");
        if (g_arena.host) {
            std::vector<Prop> radios = g_radioSpots, sodas = g_sodaSpots;
            LoadOwnPoints();  // the enemies', the zones', the builders' points as they are now
            if (kind != "radio") g_radioSpots = radios;
            if (kind != "soda") g_sodaSpots = sodas;
        } else {
            if (kind == "radio") g_radioSpots.clear();
            if (kind == "soda") g_sodaSpots.clear();
        }
        if (kind == "radio" || kind == "soda") {
            ShowProps();
            if (g_arena.host) SendProps();
        }
        ui::TheChat().AddSystem(text);
        return;
    } else if (what == "merchant") {
        ini.Set(sec, "merchant", line);
        g_hasMerchantSpot = true, g_merchantSpot = p;
        ui::TheChat().AddSystem(TR("Торговец будет приходить отсюда (сохранено для этого места). Куда идти: /торговец цель",
                                   "The merchant comes in from here (kept for this place). Where to: /merchant to"));
    } else if (what == "merchantto") {
        ini.Set(sec, "merchantto", line);
        g_hasMerchantTo = true, g_merchantTo = p;
        ui::TheChat().AddSystem(TR("Торговец будет идти сюда и торговать здесь (сохранено для этого места).", "The merchant walks here and trades here (kept for this place)."));
    } else if (what == "spawn") {
        ini.Set(sec, "spawn", line);
        ui::TheChat().AddSystem(TR("Точка появления игроков сохранена (со следующей арены).", "The players' spot to come in at is kept (from the next arena on)."));
    } else if (what == "enemy" || what == "zone") {
        const char* key = what == "enemy" ? "enemy" : "zone";
        int most = what == "enemy" ? 8 : 4, k = 1;
        while (k <= most && !ini.Get(sec, Format("%s%d", key, k)).empty()) ++k;
        if (k > most) {
            ui::TheChat().AddError(Format(TR("Уже %d точек для этого места.", "%d points for this place already."), most));
            return;
        }
        ini.Set(sec, Format("%s%d", key, k), line);
        ui::TheChat().AddSystem(Format(what == "enemy" ? TR("Точка врагов %d сохранена (со следующей арены).", "Enemy point %d kept (from the next arena on).")
                                                       : TR("Зона %d сохранена (со следующей арены).", "Zone %d kept (from the next arena on)."), k));
    } else if (what == "build") {
        int k = 1;
        while (k <= kMaxBuildSpots && !ini.Get(sec, Format("build%d", k)).empty()) ++k;
        if (k > kMaxBuildSpots) {
            ui::TheChat().AddError(Format(TR("Уже %d мест строителя для этого места.", "%d builder spots for this place already."), kMaxBuildSpots));
            return;
        }
        ini.Set(sec, Format("build%d", k), line);
        g_buildSpots.push_back({p, h});
        ui::TheChat().AddSystem(Format(TR("Место баррикады строителя %d сохранено: забор встанет здесь поперёк вашего взгляда.",
                                          "Builder spot %d kept: the fence goes up here, across where you look."), k));
    } else if (what == "wall") {
        ui::TheChat().AddError(TR("Оружия на стенах больше нет.", "There are no wall guns any more."));
        return;
    } else if (what == "reset") {
        for (const char* key : {"box", "spawn"}) ini.Set(sec, key, "");
        for (int k = 1; k <= 8; ++k) ini.Set(sec, Format("enemy%d", k), "");
        for (int k = 1; k <= 4; ++k) {
            ini.Set(sec, Format("zone%d", k), "");
            ini.Set(sec, Format("radio%d", k), "");
            ini.Set(sec, Format("soda%d", k), "");
        }
        for (int k = 1; k <= kMaxBars; ++k) ini.Set(sec, Format("bar%d", k), "");
        for (int k = 1; k <= kMaxBuildSpots; ++k) ini.Set(sec, Format("build%d", k), "");
        g_buildSpots.clear();
        for (int k = 1; k <= 8; ++k) ini.Set(sec, Format("wall%d", k), "");
        ui::TheChat().AddSystem(TR("Свои места для этого места забыты: мод снова расставит всё сам.",
                                   "The spots of this place are forgotten: the mod sets everything out itself again."));
    }
    ini.Save(SpotsPath());
}

// ---- E near a barricade or the box
// the drinks machines: this player twice a minute at most, 20% of its health a drink
std::vector<uint64_t> g_sodaUses;
uint64_t g_sodaHealAt = 0, g_sodaDrinkAt = 0;
float MaxHealth();
bool EnsureBlock(const char* block);
void SodaFrame(uint64_t now) {
    if (g_sodaDrinkAt && now >= g_sodaDrinkAt) {
        g_sodaDrinkAt = 0;
        if (CPlayerPed* me = FindPlayerPed(0); me && me->Health() > 0.0f && EnsureBlock("VENDING"))
            Cmd(0x0605).I(GetPedRef(me)).S("VEND_Drink2_P").S("VENDING").F(4.0f).I(0).I(0).I(0).I(0).I(-1).Run();
    }
    if (!g_sodaHealAt || now < g_sodaHealAt) return;
    g_sodaHealAt = 0;
    CPlayerPed* me = FindPlayerPed(0);
    if (me && me->Health() > 0.0f) me->Health() = std::min(MaxHealth(), me->Health() + MaxHealth() * 0.2f);
}
int SodaWait() {  // seconds till the next drink, 0: now
    uint64_t now = NowMs();
    g_sodaUses.erase(std::remove_if(g_sodaUses.begin(), g_sodaUses.end(), [now](uint64_t t) { return now - t >= 60000; }), g_sodaUses.end());
    if (g_sodaUses.size() < 2) return 0;
    return (int)((g_sodaUses.front() + 60000 - now + 999) / 1000);
}
enum NearKind : uint8_t { N_NONE, N_BAR, N_BOX, N_WALL, N_WARD, N_GUN, N_RADIO, N_SODA, N_STATS };
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
    for (const Prop& pr : g_radios) {
        CVector d = pr.pos - at;
        if (pr.handle >= 0 && d.Length2D() < 1.8f && std::fabs(d.z) < 2.0f && d.Length2D() < best) best = d.Length2D(), g_nearKind = N_RADIO, g_nearIdx = 0;
    }
    if (g_statBox.handle >= 0) {
        CVector d = g_statBox.pos - at;
        if (d.Length2D() < 2.2f && std::fabs(d.z) < 2.0f && d.Length2D() < best) best = d.Length2D(), g_nearKind = N_STATS, g_nearIdx = 0;
    }
    for (size_t i = 0; i < g_sodas.size(); ++i) {
        const Prop& pr = g_sodas[i];
        CVector d = pr.pos - at;
        if (pr.handle >= 0 && d.Length2D() < 1.8f && std::fabs(d.z) < 2.0f && d.Length2D() < best) best = d.Length2D(), g_nearKind = N_SODA, g_nearIdx = (int)i;
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
    AddPoints(-Cash() / 10);
    g_spectators.insert(Net().MyId());
    Send(A_SPEC, [](Writer& w) { w.U8(1); });
    ui::TheChat().AddSystem(TR("Вы выбыли до следующей волны. Мышь: камера, [ЛКМ]: другой игрок.", "Out till the next wave. Mouse: camera, [LMB]: next player."));
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
    bool nobody = spots.empty() || (spots.size() == 1 && (spots[0] - pl.pos).Length() < 0.01f);  // no one up: the entry
    CVector at = nobody ? EntrySpot() : spots[0] + CVector{0.8f, 0.6f, 0};
    Teleport(pl.area, at, Rand(0.0f, 360.0f));
    Give(h, WEAPON_PISTOL, 68);
    Cmd(0x01B9).I(h).I(WEAPON_PISTOL).Run();
    g_dead = false;
    ui::TheChat().AddSystem(TR("Вы снова в бою.", "You are back in the fight."));
}
// every frame while spectating: the camera of its own (a fixed camera set each frame) behind a live team mate, turned
// round it with the mouse (left-right, up-down), the left button for the next one; with nobody up, over the place.
// The hidden body goes with it under the floor (the world streams round it, its game shares its people). The game's
// own camera on another player's copy (CAMERA_ON_PED) stayed on this player's body under the floor.
float g_specYaw = 0, g_specPitch = 0.35f;
void SpectateFrame(uint64_t now) {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me) return;
    std::vector<std::pair<uint8_t, CPed*>> live;
    int area = kPlaces[g_arena.place].area;
    for (auto& [id, rp] : Players::Instance().All())
        if (rp.ped && IsPedValid(rp.ped) && rp.hasState && !Spectator(id) && !(rp.state.flags & OnFootState::F_DEAD) && rp.state.area == area)
            live.push_back({id, rp.ped});
    CVector focus = g_spotOwn ? g_spotAt + CVector{0, 0, 1.0f} : kPlaces[g_arena.place].pos;
    CPed* t = nullptr;
    if (!live.empty()) {
        bool click = IsForeground() && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) && now - g_specClickMs > 400;
        size_t cur = 0;
        for (size_t i = 0; i < live.size(); ++i)
            if (live[i].first == g_specTarget) cur = i;
        if (click) g_specClickMs = now, cur = (cur + 1) % live.size();
        g_specTarget = live[cur].first;
        t = live[cur].second;
        focus = t->GetPosition() + CVector{0, 0, 0.6f};
        if (t != g_specCamOn) {  // a new one: from behind it
            g_specCamOn = t;
            g_specYaw = t->GetHeading();
            g_specPitch = 0.35f;
        }
    }
    // the mouse turns the camera round it (CPad::NewMouseControllerState: m_AmountMoved x, y)
    float mx = Ref<float>(0xB73418 + 0x0C), my = Ref<float>(0xB73418 + 0x10);
    if (IsForeground() && !ui::TheChat().IsOpen()) {
        g_specYaw -= mx * 0.006f;
        g_specPitch = std::clamp(g_specPitch - my * 0.006f, -0.25f, 1.2f);  // as fast as left-right
    }
    float dist = 4.5f;
    CVector back{std::sin(g_specYaw) * std::cos(g_specPitch), -std::cos(g_specYaw) * std::cos(g_specPitch), std::sin(g_specPitch)};
    CVector camAt = focus + back * dist, hit;
    if (Ray(focus, camAt, hit)) camAt = hit + (focus - hit) * (0.3f / std::max(0.3f, (focus - hit).Length()));  // not through a wall
    Cmd(0x015F).F(camAt.x).F(camAt.y).F(camAt.z).F(0.0f).F(0.0f).F(0.0f).Run();  // SET_FIXED_CAMERA_POSITION
    Cmd(0x0160).F(focus.x).F(focus.y).F(focus.z).I(2).Run();                      // POINT_CAMERA_AT_POINT: at once
    if (t && now - g_specMoveMs > 500) {
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
    bool nobody = spots.empty() || (spots.size() == 1 && (spots[0] - pl.pos).Length() < 0.01f);
    CVector at = nobody ? EntrySpot() : spots[0] + CVector{1.0f, 0, 0};
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
    DelObj(g_ward.handle), DelObj(g_statBox.handle), DelProps();
    g_box = BoxState{};
    DelObj(g_spinHandle);
    g_spinModel = -1;
    ClearWalls();
    ClearDrops();
    g_kills = g_falls = 0;
    g_scores.clear();
    g_popups.clear();
    g_nextBoss = -1;
    ResetShop();
    g_mvp = Mvp{};
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
// The flamethrower man (Bikers, Vagos): 250 health and 50 armour, slow, comes unannounced, a flamethrower close by and molotovs from afar
constexpr int WEAPON_BAT_ARENA = 5;
CVector MemberSpot(const CVector& anchor, int k);
void ShowEvent(uint8_t kind) {  // every game
    if (kind == 2) ui::TheChat().AddSystem(TR("СЛОЖНАЯ ВОЛНА! Врагов больше, и они крепче.", "A HARD WAVE! More of them, and tougher."));
    if (kind == 1)
        ui::TheChat().AddSystem(TR("СОБЫТИЕ: бейсбольная ярость! Все враги с битами бегут с обеих сторон.",
                                   "EVENT: baseball fury! Every enemy with a bat, running in from both sides."));
}
void MakeFlamer(int wave) {
    const Faction& f = kFactions[g_arena.faction];
    g_squads.push_back(Squad{});
    int model = f.models[3];
    int h = MakeEnemy(model, WEAPON_FLAMETHROWER, 250, 50, 60, SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 8.0f),
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
// The Twins, the mini-boss: two men of different looks, a plain enemy's health each, never knocked down; one with two sawn-offs, the other with two
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
    int hp = 100 + 15 * wave;  // a plain enemy's health
    int h0 = MakeEnemy(m0, WEAPON_SAWNOFF, hp, 0, 75, at, squad, R_TWIN, false);
    int h1 = MakeEnemy(m1, WEAPON_PISTOL, hp, 0, 80, MemberSpot(at, 1), squad, R_TWIN, false);  // eagles (TwinEagles)
    g_twinPeds[0] = h0 >= 0 ? GetPed(h0) : nullptr;
    g_twinPeds[1] = h1 >= 0 ? GetPed(h1) : nullptr;
    MountQuad(h0), MountQuad(h1);  // the RC battlefield: on quad bikes
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
// The Butcher: a chainsaw, 525 health; winds it up, then charges straight on at great speed till a wall stops him
// (stunned 4 s there); whoever he runs into falls wounded, and he goes on through them. With 100 health or less he
// bounces from wall to wall instead (Dead by Daylight's Blight), up to 8 times a charge.
void MakeSaw(int wave) {
    const Faction& f = kFactions[g_arena.faction];
    g_squads.push_back(Squad{});
    int h = MakeEnemy(f.models[2], 9 /* chainsaw */, 525, 0, 70, SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 8.0f), (int)g_squads.size() - 1,
                      R_SAW, true);
    g_sawPed = h >= 0 ? GetPed(h) : nullptr;
    MountQuad(h);
    if (g_sawPed) {
        g_info[g_sawPed].nextChargeMs = NowMs() + 4000;
        SendTag(0, g_sawPed);
    }
    ui::TheChat().AddSystem(TR("Мини-босс: Мясник!", "A mini-boss: the Butcher!"));
}
// The Officer: a katana, 450 health and armour; keeps to the far back of the place (the players must go to him), calls 4
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
    CVector home = at;
    if (RcPlace()) at = SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 8.0f);  // he rides to his place on a quad bike
    int h = MakeEnemy(f.models[0], 8 /* katana */, 450, 100, 80, at, (int)g_squads.size() - 1, R_OFFICER, false);
    g_officerPed = h >= 0 ? GetPed(h) : nullptr;
    if (g_officerPed) {
        Enemy& e = g_info[g_officerPed];
        e.home = home - CVector{0, 0, 1.0f};
        e.nextCallMs = NowMs() + 10000;
        e.lastHealth = 450;
        e.maxHealth = 450;
        Cmd(0x0350).I(h).I(1).Run();  // SET_CHAR_STAY_IN_SAME_PLACE: he waits there
        SendTag(1, g_officerPed);
        MountQuad(h);
    }
    ui::TheChat().AddSystem(TR("Мини-босс: Офицер в тылу!", "A mini-boss: the Officer, at the back!"));
}

void SpawnWave() {
    ReleaseDead();
    ++g_arena.wave;
    MarkWaveStart();
    g_bossPed = nullptr;
    g_twinPeds[0] = g_twinPeds[1] = nullptr;
    g_flamers.clear();
    g_killed.clear();
    g_lastSpawns = g_spawnsNow;
    g_spawnsNow.clear();
    if (CrackPlace()) {  // the crack palace: this wave's zone first, so its men come in at it (ZoneWave tells everyone)
        AutoZones();
        if (!g_zones.empty()) {
            int idx = (g_arena.wave - 1) % (int)g_zones.size();
            g_zoneNow = idx, g_zoneAt = g_zones[(size_t)idx];
        }
    }
    Rejoin();
    OutfitWaveStart();
    SendBars();
    if (g_hasSpot) SendSpot();  // for whoever came in late
    SendProps();
    int w = g_arena.wave;
    g_dronesLeft = 0;
    const Faction& fac = kFactions[g_arena.faction];
    g_sawPed = g_officerPed = nullptr;
    // the 10% event (from the 2nd wave, not with a boss or a mini-boss): four flamethrower men, nobody else
    // the fire event (4 flamethrower men): not at the motel nor the meat factory
    std::string placeKey = kPlaces[g_arena.place].keys[0][1];
    g_fireWave = w >= 2 && !BossWave(w) && !MiniBoss(w) && placeKey != "motel" && placeKey != "meat" && RandInt(0, 99) < 10;
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
    // the motel's 15% event (from the 2nd wave, no boss nor mini-boss): everyone comes in at its two points at once,
    // a baseball bat each, and runs mad at the players and the barricades
    if (placeKey == "motel" && w >= 2 && !BossWave(w) && !MiniBoss(w) && RandInt(0, 99) < 15) {
        int count = std::min(14, 3 + w * 2 + PlayersIn());
        if (PlayersIn() < 3) count = std::max(3, count * (PlayersIn() == 1 ? 70 : 85) / 100);
        CVector pts[2];
        int have = 0;
        if (g_enemyPts.size() >= 2) {  // the host's own points: the two farthest apart
            float most = -1;
            for (size_t i = 0; i < g_enemyPts.size(); ++i)
                for (size_t j = i + 1; j < g_enemyPts.size(); ++j)
                    if (float dd = (g_enemyPts[i] - g_enemyPts[j]).Length(); dd > most) most = dd, pts[0] = g_enemyPts[i], pts[1] = g_enemyPts[j];
            pts[0] = pts[0] + CVector{0, 0, 1.0f}, pts[1] = pts[1] + CVector{0, 0, 1.0f};
            have = 2;
        } else {
            for (const Bar& b : g_bars)
                if (!b.built && have < 2) pts[have++] = b.spawn + CVector{0, 0, 1.0f};
        }
        float base = Rand(0.0f, 6.2831853f);
        for (int k = have; k < 2; ++k) pts[k] = SpawnSpot(base + 3.14159f * (float)k, 0.8f, 6.0f);
        const Faction& ff = kFactions[g_arena.faction];
        for (int k = 0; k < count; ++k) {
            g_squads.push_back(Squad{});
            CVector at = GridUsable() ? MemberSpot(pts[k % 2], k / 2 % 4) : pts[k % 2];
            int h = MakeEnemy(ff.models[(size_t)RandInt(0, 3)], WEAPON_BAT_ARENA, 90 + 12 * w, 0, 60, at, (int)g_squads.size() - 1, R_GUN, k % 3 != 0);
            if (CPed* p = h >= 0 ? GetPed(h) : nullptr) {
                Enemy& e = g_info[p];
                e.rusher = true, e.mode = M_PUSH, e.modeUntil = ~0ull;
                Cmd(0x0946).I(h).I(1).Run();  // no stagger: nothing stops the run
            }
        }
        g_arena.waveMs = NowMs();
        int n = (int)g_enemies.size(), wave = g_arena.wave;
        Send(A_WAVE, [&](Writer& wr) {
            wr.U8((uint8_t)wave);
            wr.U8((uint8_t)n);
        });
        Send(A_EVENT, [](Writer& wr) { wr.U8(1); });
        ShowEvent(1);
        LOGI("Arena: wave %d, the motel's bat event: %d men", wave, n);
        return;
    }
    int count = std::min(14, 2 + w * 2 + PlayersIn());
    if (PlayersIn() < 3) count = std::max(2, count * (PlayersIn() == 1 ? 70 : 85) / 100);  // one or two players: a lighter wave
    bool hard = HardWave(w);
    if (hard) count = std::min(18, count * 3 / 2);
    if (MiniBoss(w) == 2) count = std::max(2, count * 6 / 10);  // the Butcher's wave: fewer of the others
    // squads of 3-4, spread evenly round the players; the first comes straight on, the others go round; the knifemen
    // (1 from 6 enemies, 2 from 11) alone, from elsewhere
    int knives = placeKey == "motel" ? 0 : count >= 11 ? 2 : count >= 6 ? 1 : 0;  // no knifemen at the motel
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
    if (hard) {  // the hard wave: a third more health on each of them
        for (auto& [q, qe] : g_info)
            if (IsPedValid(q) && q->Health() > 0.0f) q->Health() *= 1.3f, qe.maxHealth *= 1.3f, qe.spawnHealth *= 1.3f;
        Send(A_EVENT, [](Writer& wr) { wr.U8(2); });
        ShowEvent(2);
    }
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
        int b = g_nextBoss >= 0 ? g_nextBoss : PickBoss(g_arena.faction);
        g_nextBoss = -1;
        Send(A_BOSS, [b](Writer& wr) { wr.U8((uint8_t)b); });
        LoadBoss(b);
        bool special = IsModelLoaded(kBossModel);
        int model = special ? kBossModel : fac.models[0];
        g_squads.push_back(Squad{});
        int bossSquad = (int)g_squads.size() - 1;
        // as tough as the SWAT were in 0.4.1: 100 health, 100 armour, no one-shot headshot. Half the bosses come with
        // the AK-47 and its grenade launcher, a quarter with a katana
        int roll = RandInt(0, 99);
        g_bossKatana = b != kTBone && roll < 50;  // half with a katana, the other half with the launcher (T-Bone always)
        g_bossUgl = false;  // no launcher any more: a plain AK-47 (or the katana)
        int bossWeapon = g_bossKatana ? 8 : 30;
        int h = MakeEnemy(model, bossWeapon, g_bossKatana ? 300 : 100, 100, 85, SpawnSpot(base + 3.14159f, 1.0f, 8.0f), bossSquad, R_BOSS, false);
        if (CPed* p = h >= 0 ? GetPed(h) : nullptr) g_bossPed = p;
        // his men: once, unannounced, silent, round to the players' backs
        int men = std::min(6, 3 + PlayersIn() - 1);
        MakeSquad(base + 3.14159f + 1.2f, 0.8f, true, 150.0f, std::min(4, men), w, true);
        if (men > 4) MakeSquad(base + 3.14159f - 1.2f, 0.8f, true, 210.0f, men - 4, w, true);
        g_arena.boss = b;
        bossLine = BossName(b);
    }
    if (RcPlace()) g_dronesLeft = std::min(3, w), g_nextDroneMs = NowMs() + 8000;  // the RC helicopters: 1 in the first wave, up to 3
    if (CrackPlace()) ZoneWave(w);
    if (g_nextBoss < 0) g_nextBoss = PickBoss(g_arena.faction);  // the next boss wave's, shown at the bar's end
    {
        int nb = g_nextBoss;
        Send(A_NEXTBOSS, [nb](Writer& wr) { wr.U8((uint8_t)nb); });
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
    if (!Ref<uint8_t>(b + 0x10)) {
        RequestModel(25575 + idx, 2);
        LoadAllRequestedModels(false);
    }
    if (!Ref<uint8_t>(b + 0x10)) return false;
    // held for good (CAnimBlock::usRefs +0x12, as CAnimManager::AddAnimBlockRef): a block nobody held was let go by the
    // streaming while a ped's task still played its animation (crash.log: 0x72F4DE from CAnimManager::BlendAnimation)
    static bool held[256] = {};
    if (idx < 256 && !held[idx]) held[idx] = true, ++Ref<uint16_t>(b + 0x12);
    return true;
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
    CVector feet = t->GetPosition() - CVector{0, 0, 0.8f};
    CVector aim = feet + t->MoveSpeed() * 50.0f * e.throwFlight * 0.8f;  // at its feet, where it will be
    CVector from = ThrowFrom(p, aim);
    float flight = ArcClear(from, ThrowVel(from, aim, e.throwFlight), e.throwFlight, aim) ? e.throwFlight : ClearFlight(from, aim);
    if (flight <= 0.0f) {  // where it runs to is behind something: at where it stands then
        aim = feet;
        from = ThrowFrom(p, aim);
        flight = ClearFlight(from, aim);
    }
    if (flight <= 0.0f) {  // the player stepped behind something meanwhile: he keeps it and tries again soon
        ++e.grenades;
        e.nextThrowMs = NowMs() + 1500;
        return;
    }
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

// the same along any steps from any spot (a quad bike's way): the farthest of the next 6 cells it has a clear line to
bool WaypointAlong(const CVector& from, const std::vector<int>& steps, CVector& out, float lift) {
    if (!GridUsable() || steps.size() != g_cells.size()) return false;
    int cur = CellNear(from);
    if (cur < 0 || steps[(size_t)cur] < 0) {
        cur = -1;
        float bestD = 6.0f;
        for (size_t k = 0; k < g_cells.size(); ++k) {
            float dd = (g_cells[k].p + CVector{0, 0, 1.0f} - from).Length();
            if (steps[k] >= 0 && dd < bestD) bestD = dd, cur = (int)k;
        }
    }
    if (cur < 0) return false;
    int best = -1;
    for (int st = 0; st < 6; ++st) {
        int next = cur;
        for (int n : g_cells[(size_t)cur].next)
            if (steps[(size_t)n] >= 0 && steps[(size_t)n] < steps[(size_t)next]) next = n;
        if (next == cur) break;
        cur = next;
        if (!Clear(from, g_cells[(size_t)cur].p + CVector{0, 0, lift})) break;
        best = cur;
    }
    if (best < 0) best = cur;
    out = g_cells[(size_t)best].p;
    return true;
}

// The RC battlefield's mini-bosses come in on quad bikes (host): from where they appear, driven straight from cell to
// cell of the floor (the game's car AI with a straight line to each, not the roads it would look for) to the players
// (the Officer to his place at the back); off it some 9 m from the player, after 15 s, or stuck for 2.5 s
std::vector<int> g_quads;  // host: the quad bikes (gone with the arena unless a player sits on one)
int MountQuad(int h) {
    CPed* p = h >= 0 ? GetPed(h) : nullptr;
    constexpr int MODEL_QUAD = 471;
    if (!p || !RcPlace() || !LoadModelNow(MODEL_QUAD) || !HasRoom(VehiclePool(), 6, "vehicle")) return -1;
    CVector at = p->GetPosition();
    int car = -1;
    Cmd(0x00A5).I(MODEL_QUAD).F(at.x).F(at.y).F(at.z - 0.5f).Out(&car).Run();  // CREATE_CAR
    if (car < 0) return -1;
    Cmd(0x0840).I(car).I(kPlaces[g_arena.place].area).Run();  // SET_VEHICLE_AREA_VISIBLE
    if (CPed* t = NearestPlayer(at)) {
        CVector d = t->GetPosition() - at;
        Cmd(0x0175).I(car).F(std::atan2(-d.x, d.y) * 57.29578f).Run();  // SET_CAR_HEADING: at the players
    }
    Cmd(0x036A).I(h).I(car).Run();      // WARP_CHAR_INTO_CAR
    Cmd(0x04E0).I(car).I(255).Run();    // SET_CAR_STRAIGHT_LINE_DISTANCE: straight to every spot it is sent to
    Cmd(0x00AD).I(car).F(16.0f).Run();  // SET_CAR_CRUISE_SPEED
    Cmd(0x00AE).I(car).I(2).Run();      // SET_CAR_DRIVING_STYLE: round the others
    g_quads.push_back(car);
    if (auto it = g_info.find(p); it != g_info.end()) {
        it->second.quad = car;
        it->second.rideSince = NowMs();
        it->second.rideStillSince = 0;
    }
    return car;
}
void ClearQuads() {
    for (int car : g_quads) {
        CVehicle* v = GetVehicle(car);
        if (!v) continue;
        bool someone = v->Driver() != nullptr;  // a player, or a body on it: left to the game
        Cmd(someone ? 0x01C3 : 0x00A6).I(car).Run();  // MARK_CAR_AS_NO_LONGER_NEEDED / DELETE_CAR
    }
    g_quads.clear();
}
void SetOn(CPed* p, Enemy& e, CPed* t);
void Ride(CPed* p, Enemy& e, CPed* t, uint64_t now) {
    CVehicle* v = e.quad >= 0 ? GetVehicle(e.quad) : nullptr;
    if (!v || p->VehicleIfIn() != v) {  // off it (thrown off, or getting off): on foot from now on
        if (!p->InVehicle() && e.quad >= 0) e.quad = -1, SetOn(p, e, t);
        return;
    }
    bool officer = e.role == R_OFFICER;
    CVector goal = officer ? e.home + CVector{0, 0, 1.0f} : t->GetPosition();
    float left = (goal - v->GetPosition()).Length2D();
    if (v->MoveSpeed().Length() * 50.0f > 1.5f) e.rideStillSince = 0;
    else if (!e.rideStillSince) e.rideStillSince = now;
    bool stuck = e.rideStillSince && now - e.rideStillSince > 2500 && now - e.rideSince > 3000;
    if (left < (officer ? 4.0f : 9.0f) || now - e.rideSince > 15000 || stuck) {
        Cmd(0x0633).I(GetPedRef(p)).Run();  // TASK_LEAVE_ANY_CAR
        e.quad = -1;
        return;
    }
    CVector wp = goal, from = v->GetPosition() + CVector{0, 0, 0.5f};
    if (!Clear(from, goal + CVector{0, 0, 0.3f})) {
        if (officer) WaypointAlong(from, StepsTo(CellNear(goal)), wp, 0.8f);
        else WaypointAlong(from, StepsFor(t), wp, 0.8f);
    }
    Cmd(0x00A7).I(e.quad).F(wp.x).F(wp.y).F(wp.z).Run();  // CAR_GOTO_COORDINATES
}


// ---- the RC battlefield's kamikaze helicopters (host): small RC helicopters (RC Raider) that come in during the waves
// one at a time (up to 3 at once, 2 + the wave's number in a wave, 8 at most), fly along the floor's cells 2 m over it
// to the nearest player and straight at it once nothing is between, and blow up against it (a small blast, 3 m).
// Two hits bring one down (a blast where it falls, $50 there). The vehicle sync shows them in the other games, whose
// hits come here (A_DRONEHIT). Their own health stays full: the hits count here.
struct Drone {
    int handle = -1;
    float hp = 40.0f;
    CVector vel, wp;
    uint64_t born = 0, wpMs = 0;
};
std::vector<Drone> g_drones;
constexpr int MODEL_RCRAIDER = 465, MODEL_RCGOBLIN = 501;
bool IsDroneModel(int m) { return m == MODEL_RCRAIDER || m == MODEL_RCGOBLIN; }
void SpinRotor(CVehicle* v) { Ref<float>((uintptr_t)v + 0x84C) = 0.35f; }  // CAutomobile::m_fHeliRotorSpeed
void SpawnDrone() {
    int model = LoadModelNow(MODEL_RCRAIDER) ? MODEL_RCRAIDER : LoadModelNow(MODEL_RCGOBLIN) ? MODEL_RCGOBLIN : -1;
    if (model < 0 || !HasRoom(VehiclePool(), 6, "vehicle")) return;
    // from the drone operator (over his head), else from the far end of the place (the farthest tenth of the cells)
    CVector at;
    bool picked = false;
    if (!picked && GridUsable()) {
        std::vector<CVector> players = PlayerSpots();
        std::vector<std::pair<float, int>> ends;
        for (int k : StandCells())
            if (!NearBase(g_cells[(size_t)k].p)) ends.push_back({DistToPlayers(g_cells[(size_t)k].p, players), k});
        if (!ends.empty()) {
            std::sort(ends.begin(), ends.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            size_t top = std::max<size_t>(1, ends.size() / 10);
            at = g_cells[(size_t)ends[(size_t)RandInt(0, (int)top - 1)].second].p + CVector{0, 0, 3.0f};
            picked = true;
        }
    }
    if (!picked) at = SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 4.0f) + CVector{0, 0, 2.5f};
    int car = -1;
    Cmd(0x00A5).I(model).F(at.x).F(at.y).F(at.z).Out(&car).Run();  // CREATE_CAR
    if (car < 0) return;
    Cmd(0x0840).I(car).I(kPlaces[g_arena.place].area).Run();  // SET_VEHICLE_AREA_VISIBLE
    Cmd(0x02AC).I(car).I(0).I(1).I(0).I(1).I(0).Run();         // SET_CAR_PROOFS: fire, walls; bullets and blasts hurt
    Cmd(0x0918).I(car).I(1).Run();                             // SET_CAR_ENGINE_ON
    Drone d;
    d.handle = car;
    d.born = NowMs();
    d.wp = at;
    g_drones.push_back(d);
    LOGI("Arena: an RC helicopter comes in");

}
// the floor far under a spot (a drone high up: the 3.5 m of FloorBelow did not reach it, its loot stayed in the air)
float FloorFar(const CVector& at) {
    CVector hit;
    return Ray(at - CVector{0, 0, 0.5f}, at - CVector{0, 0, 80.0f}, hit) ? hit.z : at.z - 2.0f;
}
void Boom(Drone& d, bool shotDown) {
    CVehicle* v = GetVehicle(d.handle);
    if (v) {
        CVector at = v->GetPosition();
        Call<0x736A50>((CEntity*)nullptr, (CEntity*)nullptr, 11 /* EXPLOSION_SMALL */, at, (uint32_t)0, (uint8_t)1, -1.0f, (uint8_t)0);  // AddExplosion (seen)
        if (shotDown) {
            float z = FloorFar(at);  // on the floor under it
            SpawnDrop(D_CASH, CVector{at.x, at.y, z + 0.3f}, 0, 50);
        }
        Cmd(0x00A6).I(d.handle).Run();  // DELETE_CAR
    }
    d.handle = -1;
}
void ClearDrones() {
    for (Drone& d : g_drones)
        if (GetVehicle(d.handle)) Cmd(0x00A6).I(d.handle).Run();
    g_drones.clear();
    g_dronesLeft = 0;
}
void DronesFrame(uint64_t now) {  // host, every frame
    if (g_dronesLeft > 0 && !g_break && g_arena.left > 0 && now >= g_nextDroneMs && g_drones.size() < 3) {
        SpawnDrone();
        --g_dronesLeft;
        g_nextDroneMs = now + (uint64_t)RandInt(6000, 9000);
    }
    float dt = std::min(0.1f, TimeStep() / 50.0f);
    for (Drone& d : g_drones) {
        CVehicle* v = GetVehicle(d.handle);
        if (!v) {
            d.handle = -1;
            continue;
        }
        if (v->Health() < 1000.0f) d.hp -= 1000.0f - v->Health(), v->Health() = 1000.0f;  // a hit here
        if (d.hp <= 0.0f) {
            CVector at = v->GetPosition();
            at = CVector{at.x + 0.6f, at.y, FloorFar(at) + 0.3f};  // on the floor under it, beside its money
            SpawnDrop(D_SCRAP, at);  // its scrap: two of them make a Shahed
            Boom(d, true);
            continue;
        }
        CVector pos = v->GetPosition();
        CPed* t = NearestPlayer(pos);
        if (!t) continue;
        CVector chest = t->GetPosition() + CVector{0, 0, 0.3f};
        if ((chest - pos).Length() < 1.6f || now - d.born > 60000) {  // against it: the blast (or a minute out: gone too)
            Boom(d, false);
            continue;
        }
        if (now - d.wpMs > 250) {
            d.wpMs = now;
            // high over the floor (4 m, under the ceiling) till the player is near and in sight: then down at it
            float flat = (chest - pos).Length2D();
            if (Clear(pos, chest)) {
                d.wp = chest;
                if (flat > 7.0f) d.wp.z = std::max(chest.z, std::min(pos.z, chest.z + 3.0f));  // the dive at the end
            } else if (CVector cell; WaypointAlong(pos, StepsFor(t), cell, 3.0f)) {
                CVector up;
                float top = Ray(cell + CVector{0, 0, 0.5f}, cell + CVector{0, 0, 4.5f}, up) ? up.z - cell.z - 0.8f : 4.0f;
                d.wp = cell + CVector{0, 0, std::clamp(top, 1.8f, 4.0f)};
            } else {
                d.wp = chest;
            }
        }
        CVector dir = d.wp - pos;
        float len = dir.Length();
        CVector want = len > 0.05f ? dir * (std::min(11.0f, len * 4.0f) / len) : CVector{};  // faster than a running man
        float k = std::min(1.0f, dt * 5.0f);
        d.vel = d.vel + (want - d.vel) * k;
        v->MoveSpeed() = d.vel * (1.0f / 50.0f) + CVector{0, 0, 0.008f * TimeStep()};  // metres a frame, the gravity made up for
        v->TurnSpeed() = CVector{};
        v->Flags() &= ~(1u << 2);  // bIsStatic: it is moved
        if (CMatrix* m = v->Matrix(); m && d.vel.Length2D() > 0.3f) {  // nose to where it flies, a little down
            CVector f{d.vel.x, d.vel.y, 0.0f};
            f = f * (1.0f / f.Length());
            m->forward = CVector{f.x * 0.97f, f.y * 0.97f, -0.24f};
            m->right = CVector{f.y, -f.x, 0.0f};
            m->up = CVector{f.x * 0.24f, f.y * 0.24f, 0.97f};
        }
        SpinRotor(v);
    }
    g_drones.erase(std::remove_if(g_drones.begin(), g_drones.end(), [](const Drone& d) { return d.handle < 0; }), g_drones.end());
}
// a partner's game: its hits on the copies of the helicopters go to the host; their rotors turn here too
void DroneCopies() {
    if (g_arena.host || !RcPlace()) return;
    CPoolHeader* pool = VehiclePool();
    if (!pool) return;
    int area = kPlaces[g_arena.place].area;
    for (int i = 0; i < pool->size; ++i) {
        if (pool->byteMap[i] < 0) continue;
        auto* v = (CVehicle*)(pool->objects + (size_t)i * kVehiclePoolElemSize);
        if (!IsDroneModel(v->ModelIndex()) || v->AreaCode() != area) continue;
        SpinRotor(v);
        if (v->Health() >= 1000.0f) continue;
        float dmg = 1000.0f - v->Health();
        v->Health() = 1000.0f;
        if (NetVehicle* nv = Vehicles::Instance().Find(v))
            Send(A_DRONEHIT, [&](Writer& w) {
                w.U16(nv->netId);
                w.F32(std::min(dmg, 200.0f));
            });
    }
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
bool Waypoint(CPed* p, CPed* t, CVector& out);
void Sneak(CPed* p, Enemy& e, CPed* t, float d, uint64_t now) {
    int h = GetPedRef(p);
    CVector view = ViewOf(t), body = p->GetPosition(), rel = body - t->GetPosition();
    bool seen = InView(t, view, body);
    bool behind = rel.x * view.x + rel.y * view.y < -0.2f * std::max(0.1f, rel.Length2D());
    bool sameFloor = std::fabs(rel.z) < 1.6f;
    if (sameFloor && d < 4.5f && (!seen || d < 2.5f)) {  // the last steps
        e.waitSince = 0;
        if (e.routing || now - e.setOnMs > 900) {  // the player turns: its back again, often
            e.routing = false;
            e.setOnMs = now;
            if (behind || !seen) Cmd(0x06A8).I(h).I(GetPedRef(t)).I(5000).F(0.6f).F(180.0f).Run();  // TASK_GOTO_CHAR_OFFSET: behind it
            else Cmd(0x05E2).I(h).I(GetPedRef(t)).Run();                                            // TASK_KILL_CHAR_ON_FOOT
        }
        return;
    }
    if (seen && d < 6.0f && sameFloor) {  // seen close by: at it
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
    if (goal < 0) {  // no way behind it now: round to its back the game's way (another floor: up the stairs first)
        CVector wp;
        if (!sameFloor && Waypoint(p, t, wp)) {
            GoTo(p, e, wp, 6);
            return;
        }
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
        if (now - e.waitSince < 2500) {
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
bool FloorNoBars(float x, float y, float fromZ, float& z);
// a medic with nothing to heal: at the back, behind its own (their middle, further from the players), shooting from there
bool MedicBack(CPed* p, Enemy& e, CPed* t, uint64_t now) {
    int h = GetPedRef(p);
    CVector mid{}, pmid{};
    int n = 0;
    for (auto& [q, qe] : g_info)
        if (q != p && IsPedValid(q) && q->Health() > 0.0f && qe.role != R_MEDIC && qe.role != R_SNIPER) mid = mid + q->GetPosition(), ++n;
    std::vector<CVector> players = PlayerSpots();
    for (const CVector& q : players) pmid = pmid + q;
    if (n && !players.empty()) {
        mid = mid * (1.0f / (float)n), pmid = pmid * (1.0f / (float)players.size());
        CVector away = mid - pmid;
        away.z = 0;
        float l = away.Length();
        if (l > 0.5f) {
            CVector spot = mid + away * (6.0f / l);
            float z = 0;
            if (FloorNoBars(spot.x, spot.y, mid.z + 0.5f, z) && std::fabs(z - (mid.z - 1.0f)) < 1.5f) {
                CVector to{spot.x, spot.y, z};
                if ((p->GetPosition() - CVector{0, 0, 1.0f} - to).Length2D() > 3.0f) {
                    GoTo(p, e, to, 6);
                    return true;
                }
            }
        }
    }
    if (e.routing || now - e.setOnMs > 2500) {  // there: shoots from where it stands
        e.routing = false, e.setOnMs = now, e.after = t;
        Cmd(0x074D).I(h).I(GetPedRef(t)).I(2500).Run();  // TASK_SHOOT_AT_CHAR
    }
    return true;
}
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
    if (!best) return MedicBack(p, e, t, now);
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
    return MedicBack(p, e, t, now);  // by a hurt one: the healing does it, meanwhile at the back
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
            g_info[np].getUpUntil = NowMs() + 1600;
            Cmd(0x0605).I(h).S("getup_front").S("PED").F(4.0f).I(0).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM: it gets up
            for (CPed*& tw : g_twinPeds)
                if (tw == q) tw = np;
            for (CPed*& fl : g_flamers)
                if (fl == q) fl = np;
            LOGI("Arena: a medic got a fallen one up");
        }
    }
    g_toRaise.clear();
}

// ---- the crack palace's zones and builders (host). Each wave the enemies go for one zone (zone 1, then 2, ...; the
// host's own /arena зона points, or 3 the mod picks itself spread over the floor): they run there first, then fight;
// builders (no weapon, $150 on them) put barricades up round the zone (cover for their own, fences for everyone).
// The builders' barricades break: shots, blows, blasts and the chainsaw's dash take their health off (each game
// tells the host of its own player's, A_EBARHIT; the host's word they are down, A_EBARGONE)
constexpr int kEBarHp = 300;
struct EBar {
    CVector pos;
    float heading = 0;
    int handle = -1;
    int hp = kEBarHp;  // the host's count
    bool gone = false;
};
std::vector<EBar> g_ebars;  // every game's own fences of the enemies' barricades (the same order everywhere)
void ShowEBar(EBar& b) {  // its fence: nothing moves it, but shots smash it (its health then, EBarFrame)
    DelObj(b.handle);
    int model = PickModel({1418, 1408, 1424});
    float lift = 0, height = 0;
    if (model < 0) return;
    b.handle = MakeObj(model, b.pos, b.heading, lift, height);
    if (CObject* o = b.handle >= 0 ? GetObject(b.handle) : nullptr) {
        o->PhysicalFlags() |= (1u << 4) | (1u << 5) | (1u << 6) | (1u << 13);  // no turn, no move, infinite mass, no speed
        o->MoveSpeed() = CVector{};
        o->TurnSpeed() = CVector{};
    }
}
// the floor under a spot, not on top of a barricade (a fence's top was a "floor": barricades hung in the air)
bool FloorNoBars(float x, float y, float fromZ, float& z) {
    CVector a{x, y, fromZ};
    for (int k = 0; k < 6; ++k) {
        CVector b{x, y, fromZ - 3.5f};
        ColPoint cp{};
        CEntity* hit = nullptr;
        if (!Call<0x56BA00, bool>(&a, &b, &cp, &hit, true, false, false, true, false, false, false, false)) return false;  // ProcessLineOfSight
        bool fence = false;
        for (const EBar& e : g_ebars)
            if (!e.gone && e.handle >= 0 && (CEntity*)GetObject(e.handle) == hit) fence = true;
        for (const Bar& e : g_bars)
            if (e.handle >= 0 && (CEntity*)GetObject(e.handle) == hit) fence = true;
        if (!fence) {
            z = cp.point.z;
            return true;
        }
        a = cp.point - CVector{0, 0, 0.05f};
    }
    return false;
}
int EBarsUp() {
    int n = 0;
    for (const EBar& b : g_ebars) n += b.gone ? 0 : 1;
    return n;
}
void AddEBar(const CVector& at, float heading) {
    if (g_ebars.size() >= 40) return;
    EBar b;
    b.pos = at, b.heading = heading;
    ShowEBar(b);
    g_ebars.push_back(b);
}
void ClearEBars() {
    for (EBar& b : g_ebars) DelObj(b.handle);
    g_ebars.clear();
}
void EBarDown(int i) {  // every game: that barricade broke
    if (i < 0 || i >= (int)g_ebars.size() || g_ebars[(size_t)i].gone) return;
    EBar& b = g_ebars[(size_t)i];
    b.gone = true;
    DelObj(b.handle);
    LOGI("Arena: a builder's barricade %d broke", i);
}
void HitEBar(int i, int dmg) {  // this game's player hit it
    if (i < 0 || i >= (int)g_ebars.size() || g_ebars[(size_t)i].gone || dmg <= 0) return;
    if (!g_arena.host) {
        Send(A_EBARHIT, [&](Writer& w) {
            w.U8((uint8_t)i);
            w.U16((uint16_t)std::min(dmg, 60000));
        });
        return;
    }
    EBar& b = g_ebars[(size_t)i];
    b.hp -= dmg;
    if (b.hp > 0) return;
    Send(A_EBARGONE, [&](Writer& w) { w.U8((uint8_t)i); });
    EBarDown(i);
}
// how far a spot is from that fence (along its length, about 2 m each way of its middle)
float EBarDist(const EBar& b, const CVector& at, float& dz) {
    float hr = b.heading / 57.29578f;
    CVector ax{std::cos(hr), std::sin(hr), 0.0f};
    CVector d = at - b.pos;
    dz = d.z;
    float along = std::clamp(d.x * ax.x + d.y * ax.y, -2.0f, 2.0f);
    CVector off{d.x - ax.x * along, d.y - ax.y * along, 0.0f};
    return off.Length2D();
}
int EBarOf(CEntity* e) {
    if (!e) return -1;
    for (size_t i = 0; i < g_ebars.size(); ++i)
        if (!g_ebars[i].gone && g_ebars[i].handle >= 0 && (CEntity*)GetObject(g_ebars[i].handle) == e) return (int)i;
    return -1;
}
int ShotDamage(int w) {
    switch (w) {
        case 22: case 23: return 25;           // pistols
        case 24: return 70;                    // the Desert Eagle
        case 25: case 26: case 27: return 90;  // shotguns
        case 28: case 29: case 32: return 20;  // SMGs
        case 30: case 31: return 30;           // AK, M4
        case 33: case 34: return 80;           // rifles
        case 38: return 40;                    // minigun
        default: return 0;
    }
}
// every frame: this player's shots and blows at the builders' barricades
void EBarFrame(uint64_t now) {
    static int lastType = -1, lastClip = 0;
    static uint64_t meleeMs = 0;
    CPlayerPed* me = FindPlayerPed(0);
    if (g_ebars.empty() || !me || me->Health() <= 0.0f || me->InVehicle() || g_spectating) {
        lastType = -1;
        return;
    }
    const CWeapon& wpn = me->ActiveWeapon();
    int type = wpn.type, clip = wpn.ammoInClip;
    bool shot = type == lastType && clip < lastClip && ShotDamage(type) > 0;
    lastType = type, lastClip = clip;
    static uint64_t shotMs = 0, countedMs = 0;
    bool fireDown = IsForeground() && !ui::TheChat().IsOpen() && !MenuActive() && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
    if (shot || fireDown) shotMs = now;
    // a fence the game smashed (no drawing, no collision): up again at once; this player's doing, its health off
    for (size_t i = 0; i < g_ebars.size(); ++i) {
        EBar& b = g_ebars[i];
        if (b.gone) continue;
        CObject* o = b.handle >= 0 ? GetObject(b.handle) : nullptr;
        if (o && o->IsVisible() && (o->Flags() & 1)) continue;
        ShowEBar(b);
        if (now - shotMs < 500 && now - countedMs > 150) HitEBar((int)i, std::max(ShotDamage(type), 40));
    }
    if (shot) {  // where it went: along the aim
        CVector a = cam::Source(), b = a + cam::Front() * 120.0f;
        ColPoint cp{};
        CEntity* hit = nullptr;
        if (Call<0x56BA00, bool>(&a, &b, &cp, &hit, true, true, false, true, false, false, true, false)) {  // ProcessLineOfSight
            int i = EBarOf(hit);
            if (i >= 0) HitEBar(i, ShotDamage(type)), countedMs = now;
        }
    }
    // blows: fists and melee weapons, the fire button, a barricade just in front
    bool melee = type <= 15;
    bool fire = IsForeground() && !ui::TheChat().IsOpen() && !MenuActive() && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
    uint64_t every = type == 9 ? 200 : 450;
    if (melee && fire && now >= meleeMs) {
        CVector pos = me->GetPosition();
        float hd = me->CurrentRotation();
        CVector f{-std::sin(hd), std::cos(hd), 0.0f};
        for (size_t i = 0; i < g_ebars.size(); ++i) {
            const EBar& b = g_ebars[i];
            if (b.gone) continue;
            float dz = 0;
            float l = EBarDist(b, pos - CVector{0, 0, 1.0f}, dz);
            CVector ahead = pos - CVector{0, 0, 1.0f} + f * 0.8f;
            float dz2 = 0;
            if (std::fabs(dz) > 1.5f || l > 1.6f || EBarDist(b, ahead, dz2) > l + 0.05f) continue;  // near, and facing it
            int dmg = type == 9 ? 25 : type == 0 ? 8 : type == 8 ? 45 : 20;  // the katana cuts
            HitEBar((int)i, dmg);
            meleeMs = now + every;
            break;
        }
    }
}
// a blast of this game's (grenades, molotovs' fire not): the barricades near it
void EBarBlast(const CVector& at) {
    for (size_t i = 0; i < g_ebars.size(); ++i) {
        if (g_ebars[i].gone) continue;
        float dz = 0;
        float d = EBarDist(g_ebars[i], at, dz);
        d = std::sqrt(d * d + dz * dz);
        if (d < 6.0f) HitEBar((int)i, (int)(400.0f * (1.0f - d / 6.0f)) + 60);
    }
}
// the chainsaw's dash: through every barricade on its way
void EBarDash(const CVector& at) {
    for (size_t i = 0; i < g_ebars.size(); ++i) {
        if (g_ebars[i].gone) continue;
        float dz = 0;
        float d = EBarDist(g_ebars[i], at - CVector{0, 0, 1.0f}, dz);
        if (d < 1.2f && std::fabs(dz) < 1.6f) {
            HitEBar((int)i, 10000);
            if (!g_arena.host) EBarDown((int)i);  // out of the way at once (the host's word follows)
        }
    }
}
void AutoZones() {  // 3 spots of the floor far from each other and from the players' middle
    if (!g_zones.empty() || !GridUsable()) return;
    const Place& pl = kPlaces[g_arena.place];
    const std::vector<int>& cells = StandCells();
    for (int z = 0; z < 3; ++z) {
        float best = -1;
        CVector at;
        for (int k : cells) {
            CVector c = g_cells[(size_t)k].p;
            float d = (c - pl.pos).Length2D() < 5.0f ? 0.0f : 1e9f;
            for (const CVector& q : g_zones) d = std::min(d, (c - q).Length());
            if (g_zones.empty()) d = std::min(d, (c - pl.pos).Length() + Rand(0.0f, 6.0f));
            if (d > best) best = d, at = c;
        }
        if (best <= 6.0f) break;
        g_zones.push_back(at);
    }
    LOGI("Arena: %d zones picked", (int)g_zones.size());
}
void ShowZone(int idx, const CVector& at) {  // every game
    g_zoneNow = idx, g_zoneAt = at;
    if (idx >= 0) ui::TheChat().AddSystem(Format(TR("Враги атакуют зону %d!", "The enemies attack zone %d!"), idx + 1));
}
void ZoneWave(int wave) {  // host, as a wave of the crack palace begins
    AutoZones();
    if (g_zones.empty()) return;
    int idx = (wave - 1) % (int)g_zones.size();
    CVector at = g_zones[(size_t)idx];
    Send(A_ZONE, [&](Writer& w) {
        w.I8((int8_t)idx);
        w.Vec({at.x, at.y, at.z});
    });
    ShowZone(idx, at);
    uint64_t now = NowMs();
    for (auto& [q, qe] : g_info) {  // this wave's men run to it first
        if (qe.role != R_GUN && qe.role != R_GRENADIER && qe.role != R_MEDIC) continue;
        float a = Rand(0.0f, 6.2831853f), r = Rand(0.5f, 3.0f);
        CVector sp = at + CVector{std::cos(a) * r, std::sin(a) * r, 0.0f};
        float z = 0;
        qe.zoneSpot = FloorBelow(sp.x, sp.y, at.z + 1.0f, z) && std::fabs(z - at.z) < 1.0f ? CVector{sp.x, sp.y, z} : at;
        qe.zoneUntil = now + 25000;
    }
    const Faction& f = kFactions[g_arena.faction];
    int builders = wave >= 4 ? 2 : 1;
    for (int k = 0; k < builders; ++k) {
        g_squads.push_back(Squad{});
        MakeEnemy(f.models[(size_t)RandInt(0, 3)], 0, 100 + 10 * wave, 0, 50, SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 6.0f),
                  (int)g_squads.size() - 1, R_BUILDER, true);
    }
}
void Builder(CPed* p, Enemy& e, uint64_t now) {  // host, every round of tactics
    if (g_zoneNow < 0) return;
    int h = GetPedRef(p);
    if (!e.hasBuildSpot && !g_buildSpots.empty()) {  // the host's own spots: a free one nearest the zone
        int best = -1;
        float bestD = 1e9f;
        for (size_t k = 0; k < g_buildSpots.size(); ++k) {
            const BuildSpot& bs = g_buildSpots[k];
            bool taken = false;
            for (const EBar& b : g_ebars)
                if (!b.gone && (b.pos - bs.pos).Length() < 1.0f) taken = true;
            for (auto& [q, qe] : g_info)
                if (&qe != &e && qe.role == R_BUILDER && qe.buildIdx == (int)k && q->Health() > 0.0f) taken = true;
            float d = (bs.pos - g_zoneAt).Length();
            if (!taken && d < bestD) bestD = d, best = (int)k;
        }
        if (best < 0) {  // all up: it stays where it is
            e.builds = 3;
            return;
        }
        const BuildSpot& bs = g_buildSpots[(size_t)best];
        e.buildIdx = best;
        // it works from the side away from the players
        float hr = bs.heading / 57.29578f;
        CVector nrm{-std::sin(hr), std::cos(hr), 0.0f};
        CVector mid{};
        std::vector<CVector> pls = PlayerSpots();
        for (const CVector& q : pls) mid = mid + q;
        if (!pls.empty()) mid = mid * (1.0f / (float)pls.size());
        CVector to = mid - bs.pos;
        float side = to.x * nrm.x + to.y * nrm.y > 0 ? -1.0f : 1.0f;
        CVector st = bs.pos + nrm * (1.3f * side);
        float z = 0;
        e.buildSpot = FloorNoBars(st.x, st.y, bs.pos.z + 1.0f, z) && std::fabs(z - bs.pos.z) < 1.0f ? CVector{st.x, st.y, z} : bs.pos;
        e.hasBuildSpot = true;
    }
    if (!e.hasBuildSpot) {
        float a = Rand(0.0f, 6.2831853f), r = Rand(1.0f, 4.0f), z = 0;
        CVector sp = g_zoneAt + CVector{std::cos(a) * r, std::sin(a) * r, 0.0f};
        e.buildSpot = FloorNoBars(sp.x, sp.y, g_zoneAt.z + 1.0f, z) && std::fabs(z - g_zoneAt.z) < 1.0f &&
                              Clear(g_zoneAt + CVector{0, 0, 0.6f}, CVector{sp.x, sp.y, z + 0.6f})
                          ? CVector{sp.x, sp.y, z}
                          : g_zoneAt;
        e.hasBuildSpot = true;
    }
    if (e.builds >= 3) return;  // done: it stays by its barricades
    if ((p->GetPosition() - CVector{0, 0, 1.0f} - e.buildSpot).Length2D() > 1.2f) {
        GoTo(p, e, e.buildSpot, 6);
        e.buildAt = 0;
        return;
    }
    if (!e.buildAt) {  // down on its knees, building
        e.buildAt = now + 2500;
        e.routing = false;
        if (EnsureBlock("BOMBER")) Cmd(0x0605).I(h).S("BOM_Plant_Loop").S("BOMBER").F(4.0f).I(1).I(0).I(0).I(0).I(-1).Run();
        return;
    }
    if (now < e.buildAt) return;
    e.buildAt = 0;
    ++e.builds;
    e.hasBuildSpot = false;
    Cmd(0x0687).I(h).Run();  // CLEAR_CHAR_TASKS
    if (e.buildIdx >= 0 && e.buildIdx < (int)g_buildSpots.size()) {  // the host's own spot: the fence just as it was kept
        BuildSpot bs = g_buildSpots[(size_t)e.buildIdx];
        e.buildIdx = -1;
        float fz = 0;
        if (FloorNoBars(bs.pos.x, bs.pos.y, bs.pos.z + 1.0f, fz) && std::fabs(fz - bs.pos.z) < 1.2f) bs.pos.z = fz;
        Send(A_EBAR, [&](Writer& w) {
            w.Vec({bs.pos.x, bs.pos.y, bs.pos.z});
            w.F32(bs.heading);
        });
        AddEBar(bs.pos, bs.heading);
        return;
    }
    CPed* t = NearestPlayer(p->GetPosition());
    CVector to = t ? t->GetPosition() - p->GetPosition() : CVector{0, 1, 0};
    to.z = 0;
    float l = std::max(0.01f, to.Length());
    to = to * (1.0f / l);
    CVector at = p->GetPosition() - CVector{0, 0, 1.0f} + to * 1.0f;
    float z = 0;
    if (!FloorNoBars(at.x, at.y, at.z + 1.0f, z) || std::fabs(z - at.z) > 1.2f) return;
    at.z = z;
    float heading = std::atan2(-to.x, to.y) * 57.29578f;
    Send(A_EBAR, [&](Writer& w) {
        w.Vec({at.x, at.y, at.z});
        w.F32(heading);
    });
    AddEBar(at, heading);
}

// ---- the katana boss (host): runs at his player, within 7 m winds up (0.8 s), then spins fast (3.5 s) and comes on at
// 5 m/s; whoever he gets to loses half of all its health and armour; while he spins the bullets bounce off him (his
// bullet proof); a player's chainsaw dash into him then stuns him 4 s
void LoseHalf();  // this player
bool SawRay(const CVector& a, const CVector& b, CVector& at, CVector& normal);
void PlayAnim(int h, const char* anim, const char* block, bool loop);
void SendHalve(uint8_t id) {
    Writer w(proto::G_ARENA);
    w.U8(A_HALVE);
    Net().SendTo(id, proto::CH_RELIABLE, w);
}
void Katana(CPed* p, Enemy& e, CPed* t, float d, bool sees, uint64_t now) {
    int h = GetPedRef(p);
    if (e.kStage != 0) return;  // the frame does the rest
    if (sees && d < 7.0f && now >= e.kNextMs) {
        e.kStage = 1;
        e.kUntil = now + 800;
        e.kHit.clear();
        e.routing = false;
        Cmd(0x0687).I(h).Run();  // CLEAR_CHAR_TASKS
        CVector to = t->GetPosition() - p->GetPosition();
        Cmd(0x0173).I(h).F(std::atan2(-to.x, to.y) * 57.29578f).Run();
        Cmd(0x05BA).I(h).I(800).Run();  // TASK_STAND_STILL: the wind-up
        return;
    }
    GoTo(p, e, t->GetPosition() - CVector{0, 0, 1.0f}, 7);  // sprinting at him
}
void KatanaStun(CPed* p, Enemy& e) {
    e.kStage = 4;
    e.kUntil = NowMs() + 4000;
    int h = GetPedRef(p);
    Cmd(0x02AB).I(h).I(0).I(0).I(0).I(0).I(0).Run();  // SET_CHAR_PROOFS: off
    Cmd(0x0792).I(h).Run();
    PlayAnim(h, "IDLE_tired", "PED", true);
    ui::TheChat().AddSystem(TR("Босс оглушён!", "The boss is stunned!"));
}
void KatanaFrame(uint64_t now) {  // host, every frame
    float dt = std::min(0.1f, TimeStep() / 50.0f);
    for (auto& [p, e] : g_info) {
        if (e.role != R_BOSS || !g_bossKatana || !IsPedValid(p) || p->Health() <= 0.0f) continue;
        int h = GetPedRef(p);
        if (e.kStage == 1 && now >= e.kUntil) {
            e.kStage = 2;
            e.kUntil = now + 3500;
            Cmd(0x02AB).I(h).I(1).I(0).I(0).I(0).I(0).Run();  // SET_CHAR_PROOFS: bullets bounce off
            continue;
        }
        if ((e.kStage == 3 || e.kStage == 4) && now >= e.kUntil) {
            e.kStage = 0;
            e.kNextMs = now + 3000;
            Cmd(0x0687).I(h).Run();
            continue;
        }
        if (e.kStage != 2) continue;
        if (now >= e.kUntil) {
            e.kStage = 3;
            e.kUntil = now + 1200;
            Cmd(0x02AB).I(h).I(0).I(0).I(0).I(0).I(0).Run();
            continue;
        }
        p->CurrentRotation() = p->AimingRotation() = std::fmod(p->CurrentRotation() + 16.0f * dt, 6.2831853f);  // the spin
        CPed* t = NearestPlayer(p->GetPosition());
        if (!t) continue;
        CVector pos = p->GetPosition(), to = t->GetPosition() - pos;
        to.z = 0;
        float l = to.Length();
        if (l > 0.3f) {
            CVector dir = to * (1.0f / l), next = pos + dir * std::min(l, 5.0f * dt), hit, n;
            float z = 0;
            if (!SawRay(pos, next + dir * 0.4f, hit, n) && FloorBelow(next.x, next.y, next.z + 0.3f, z) && std::fabs(z + 1.0f - pos.z) < 1.2f) {
                next.z = z + 1.0f;
                p->MoveSpeed() = CVector{};
                SetEntityPosition(p, next);
            }
        }
        auto struck = [&](CPed* q) {
            if (!q || e.kHit.count(q) || q->Health() <= 0.0f) return false;
            CVector dd = q->GetPosition() - p->GetPosition();
            if (dd.Length2D() > 1.5f || std::fabs(dd.z) > 1.6f) return false;
            e.kHit.insert(q);
            return true;
        };
        if (struck(FindPlayerPed(0))) LoseHalf();
        for (auto& [id, rp] : Players::Instance().All())
            if (rp.ped && struck(rp.ped)) SendHalve(id);
    }
}

// The flamethrower man: walks (never runs) at its player; within 7 m it burns, from 9-22 m in sight it throws molotovs
bool Flame(CPed* p, Enemy& e, CPed* t, float d, bool sees, uint64_t now) {
    int h = GetPedRef(p);
    if (e.grenadeUntil) {
        if (now > e.grenadeUntil && !e.throwAt) e.grenadeUntil = 0, e.setOnMs = 0;
        return true;
    }
    // a molotov from 8.5 m to 15 m, over low cover too when the bottle's way is clear (StartThrow looks); when it is
    // not, it tries again half a second on
    if ((sees || d < 12.0f) && d > 8.5f && d < 15.0f && e.grenades > 0 && now >= e.nextThrowMs && StartThrow(p, e, t)) {
        e.nextThrowMs = now + (uint64_t)RandInt(6000, 9000);
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
// the Butcher's saw roaring as he winds up (every game: his copy too): the game's own chainsaw engine
// (CAEWeaponAudioEntity::ReportChainsawEvent, AE_WEAPON_CHAINSAW_ACTIVE on the man's weapon audio, CPed +0x394)
std::map<CPed*, uint64_t> g_sawRoar;
void SawRoar(CPed* p, uint64_t until) { g_sawRoar[p] = until; }
void RoarFrame(uint64_t now) {
    for (auto it = g_sawRoar.begin(); it != g_sawRoar.end();) {
        CPed* p = it->first;
        if (now > it->second || !IsPedValid(p) || p->Health() <= 0.0f) {
            it = g_sawRoar.erase(it);
            continue;
        }
        CallMethod<0x503910>((void*)((uint8_t*)p + 0x394), (CEntity*)p, 154 /* AE_WEAPON_CHAINSAW_ACTIVE */);
        ++it;
    }
}
void SawFrame(uint64_t now) {
    float dt = std::min(0.1f, TimeStep() / 50.0f);
    for (auto& [p, e] : g_info) {
        if (e.role != R_SAW || !IsPedValid(p) || p->Health() <= 0.0f || p->InVehicle() || e.quad >= 0) continue;
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
        // a barricade in the way: the charge goes through it, every board of it off (before the wall test, so that
        // its fence is gone by then)
        for (size_t i = 0; i < g_bars.size(); ++i) {
            const Bar& b = g_bars[i];
            if (b.boards <= 0) continue;
            CVector d = b.pos + CVector{0, 0, 1.0f} - (pos + e.sawDir * (0.8f + step));
            if (d.Length2D() < 1.9f && std::fabs(d.z) < 2.0f) {
                SetBoards((int)i, 0, 0xFF);
                LOGI("Arena: the Butcher's charge smashed barricade %d", (int)i);
            }
        }
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
        // the warning: he stops, the saw up and its engine roaring (1.8 s), then the charge
        e.sawStage = 1;
        e.sawUntil = now + 1800;
        e.after = t;
        e.routing = false;
        Cmd(0x0687).I(h).Run();  // CLEAR_CHAR_TASKS
        if (EnsureBlock("CHAINSAW")) PlayAnim(h, "WEAPON_csaw", "CHAINSAW", true);  // the saw held up, running
        else PlayAnim(h, "IDLE_armed", "PED", true);
        SawRoar(p, now + 1800);
        if (NetPed* np = Peds::Instance().Find(p)) {
            uint16_t id = np->netId;
            Send(A_SAWWIND, [id](Writer& w) { w.U16(id); });
        }
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
        if ((e.role != R_KNIFE && !e.rusher) || !IsPedValid(p) || p->Health() <= 0.0f || p->InVehicle() || p->MoveState() < 6) continue;  // running, sprinting
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
                // the hurt ones of theirs within 20 m come and stand in it (each its own spot round it)
                int n = 0;
                for (auto& [q, qe] : g_info) {
                    if (!IsPedValid(q) || q->Health() <= 0.0f || q->InVehicle() || qe.quad >= 0 || qe.gasUntil > now) continue;
                    if (qe.role == R_SAW || qe.role == R_OFFICER || qe.role == R_SNIPER || qe.role == R_MEDIC) continue;
                    if (q->Health() >= qe.maxHealth * 0.9f || (q->GetPosition() - g.pos).Length() > 20.0f) continue;
                    float a = Rand(0.0f, 6.2831853f), r = Rand(0.8f, 2.6f);
                    CVector spot = g.pos + CVector{std::cos(a) * r, std::sin(a) * r, 0.0f};
                    float z = 0;
                    if (!FloorBelow(spot.x, spot.y, g.pos.z + 1.0f, z) || std::fabs(z - g.pos.z) > 1.0f || !Clear(g.pos + CVector{0, 0, 0.5f}, CVector{spot.x, spot.y, z + 0.5f}))
                        spot = g.pos, z = g.pos.z - 0.1f;
                    qe.gasSpot = CVector{spot.x, spot.y, z};
                    qe.gasUntil = g.until;
                    qe.routing = false;
                    ++n;
                }
                if (n) LOGD("Arena: %d hurt ones go to the healing gas", n);
                g.healMs = now;
                for (auto& [q, qe] : g_info)
                    if (IsPedValid(q) && q->Health() > 0.0f && (q->GetPosition() - g.pos).Length() < 5.0f)
                        q->Health() = std::min(qe.maxHealth, q->Health() + qe.maxHealth * 0.06f);  // a little stronger than it was (4 m, 4%)
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
    if (!e.uglLoaded || !sees || d < 11.0f || d > 40.0f || now < e.nextUglMs) return false;
    CVector to = t->GetPosition() - p->GetPosition();
    // not into his own: none of his men within 6 m of where it lands, nor near its way out of the barrel
    for (auto& [q, qe] : g_info) {
        if (q == p || !IsPedValid(q) || q->Health() <= 0.0f) continue;
        CVector rel = q->GetPosition() - p->GetPosition();
        float along = rel.Dot(to) / std::max(0.1f, to.Length());
        CVector side = rel - to * (along / std::max(0.1f, to.Length()));
        if ((q->GetPosition() - t->GetPosition()).Length() < 6.0f || (along > 0.0f && along < 8.0f && side.Length() < 1.6f)) {
            e.nextUglMs = now + 700;  // a moment, then looks again
            return false;
        }
    }
    if (!Clear(p->GetPosition() + CVector{0, 0, 0.45f}, t->GetPosition())) return false;  // no wall right before him
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
    int gun = me->ActiveWeapon().type;
    bool fits = gun == 30 ? (g_hasUgl || g_modUgl) : g_modUgl && gun >= 22 && gun <= 34;  // the bought one: on any gun
    if (!fits) {
        ui::TheChat().AddError(g_modUgl ? TR("Возьмите оружие в руки.", "Take a gun in hand.") : TR("Возьмите АК-47 в руки.", "Take the AK-47 in hand."));
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
        Cmd(0x01B9).I(GetPedRef(me)).I(gun).Run();
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
    g_bossUgl = false, g_bossKatana = false;
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
        if (e.getUpUntil) {  // raised by a medic: getting up, then straight back into the fight (it stood there idle)
            if (now < e.getUpUntil) continue;
            e.getUpUntil = 0;
            Cmd(0x0687).I(h).Run();  // CLEAR_CHAR_TASKS: the getting up's animation done with
            e.mode = e.role == R_KNIFE ? M_SNEAK : M_FIGHT;
            e.routing = false;
            e.lastMoveMs = now, e.lastPos = p->GetPosition();
            if (e.role != R_KNIFE) SetOn(p, e, t);
            continue;
        }
        if (e.role == R_BUILDER) {
            Builder(p, e, now);
            continue;
        }
        if (e.zoneUntil) {  // to the zone first
            if (now > e.zoneUntil || (p->GetPosition() - CVector{0, 0, 1.0f} - e.zoneSpot).Length2D() < 2.0f || (sees && d < 10.0f)) {
                e.zoneUntil = 0;
                SetOn(p, e, t);
            } else {
                GoTo(p, e, e.zoneSpot, 6);
                continue;
            }
        }
        if (e.quad >= 0 || p->InVehicle()) {  // on its quad bike (or getting off it): nothing else meanwhile
            Ride(p, e, t, now);
            continue;
        }
        if (e.gasUntil) {  // to the healing gas and in it, shooting from there, till it is gone or this one is well
            if (now >= e.gasUntil || p->Health() >= e.maxHealth * 0.98f) {
                e.gasUntil = 0;
                SetOn(p, e, t);
            } else {
                if ((p->GetPosition() - CVector{0, 0, 1.0f} - e.gasSpot).Length2D() > 1.0f) {
                    GoTo(p, e, e.gasSpot, 7);  // sprinting there
                } else if (sees && now - e.setOnMs > 1800) {
                    e.setOnMs = now;
                    e.routing = false;
                    Cmd(0x074D).I(h).I(GetPedRef(t)).I(2000).Run();  // TASK_SHOOT_AT_CHAR: from where it stands
                }
                continue;
            }
        }
        // stuck: no step on in 4 s, the player out of its sight and not next to it
        bool still = e.mode == M_COVER_HOLD || (e.role == R_SNIPER && e.mode != M_RELOCATE) || (e.role == R_KNIFE && e.waitSince) ||
                     (e.mode == M_BREACH && e.tearing) || e.cprUntil || e.role == R_OFFICER || (e.role == R_SAW && e.sawStage != 0) ||
                     e.uglStage == 2;
        if ((p->GetPosition() - e.lastPos).Length() > 0.7f || still || sees || d < 4.0f) {
            e.lastPos = p->GetPosition();
            e.lastMoveMs = now;
        } else if (now - e.lastMoveMs > 4000 && !e.grenadeUntil && !(e.role == R_BOSS && (g_bossUgl || g_bossKatana))) {  // the launcher's and the katana's boss are never moved by hand
            Unstick(p, t);
            e.lastPos = p->GetPosition();
            e.lastMoveMs = now;
            e.routing = false;
            if (e.mode == M_COVER_GO || e.mode == M_COVER_HOLD) e.mode = M_FIGHT;
            if (e.role != R_KNIFE) SetOn(p, e, t);
            continue;
        }
        if (e.rusher && !e.grenadeUntil && !e.cprUntil) {  // the bat event: a barricade in the way torn down, else at the player
            if (e.mode != M_BREACH)
                for (size_t i = 0; i < g_bars.size(); ++i) {
                    CVector bd = g_bars[i].pos - (p->GetPosition() - CVector{0, 0, 1.0f});
                    if (g_bars[i].boards > 0 && bd.Length2D() < 2.2f && std::fabs(bd.z) < 2.0f) {
                        e.mode = M_BREACH, e.barricade = (int)i, e.breachSince = now, e.tearing = false;
                        break;
                    }
                }
            if (e.mode == M_BREACH) {
                Breach(p, e, t, now);
                if (e.mode != M_BREACH) e.mode = M_PUSH;
                continue;
            }
            CVector wp;
            if ((!sees || std::fabs(t->GetPosition().z - p->GetPosition().z) > 2.0f) && d > 3.0f && Waypoint(p, t, wp)) {
                e.after = t;
                GoTo(p, e, wp, 7);  // sprinting
                continue;
            }
            if (t != e.after || e.routing || now - e.setOnMs > 4000) SetOn(p, e, t);
            continue;
        }
        // the player on another floor (up on the first floor, they below it): to it along the floor's cells, up the
        // stairs, not standing under it; the melee ones even when they see it, the guns when they do not
        float upDown = t->GetPosition().z - p->GetPosition().z;
        bool otherFloor = std::fabs(upDown) > 2.0f && GridUsable() && !p->InVehicle();
        bool melee = e.role == R_SAW || (e.role == R_BOSS && g_bossKatana) || e.role == R_FLAMER;
        bool busy = e.grenadeUntil || e.cprUntil || e.mode == M_BREACH || (e.role == R_SAW && e.sawStage != 0) ||
                    (e.role == R_BOSS && g_bossKatana && e.kStage != 0);
        if (otherFloor && !busy && e.role != R_SNIPER && e.role != R_KNIFE && (melee || !sees || d < 7.0f)) {
            CVector wp;
            if (Waypoint(p, t, wp)) {
                e.after = t;
                GoTo(p, e, wp, d > 12.0f ? 7 : 6);
                static uint64_t logMs = 0;
                if (now - logMs > 10000) logMs = now, LOGI("Arena: an enemy goes up or down to a player %.1f m %s it", std::fabs(upDown), upDown > 0 ? "over" : "under");
                continue;
            }
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
        if (e.role == R_BOSS && g_bossUgl && TBone(p, e, t, d, sees, now)) continue;
        if (e.role == R_BOSS && g_bossKatana) {
            Katana(p, e, t, d, sees, now);
            continue;
        }
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
    // no money for the waves: only what was picked up off the enemies
    if (!won && Endless() && g_arena.wave > 1)
        ui::TheChat().AddSystem(Format(TR("Вы продержались %d волн.", "You held out %d waves."), g_arena.wave - 1));
    else if (won) ui::TheChat().AddSystem(TR("Арена пройдена!", "Arena won!"));
    else ui::TheChat().AddSystem(TR("Арена проиграна.", "The arena is lost."));
}

void Clear() {
    if (g_arena.host) ReleaseDead();
    ForgetSpot();
    ClearEBars();
    g_zoneNow = -1;
    ClearDrones();
    ClearQuads();
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
    DelObj(g_ward.handle), DelObj(g_statBox.handle), DelProps();
    g_box = BoxState{};
    ShutDoors(false);
    ForgetSpot();
    g_radioSpots.clear(), g_sodaSpots.clear();
    ClearEBars();
    g_zoneNow = -1;
    g_arena.place = place;
    CPlayerPed* me = FindPlayerPed(0);
    if (me && me->Health() > 0.0f && g_arena.taken) {
        const Place& pl = kPlaces[place];
        CVector at = EntrySpot();
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
        ClearDrones();
        ClearQuads();  // after their riders
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
int32_t g_moneyBefore = 0;  // out of the mode: the save's money while the arena is on
bool g_moneyKept = false;
// CJ as at the game's start every arena: the weapon skills (CStats 69-79) and the body (fat 21, stamina 22, muscle
// 23) from nothing, his body rebuilt (not fat, not muscled); out of the mode the save's own come back after it
const uint16_t kResetStats[] = {21, 22, 23, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79};
constexpr int kResetCount = (int)(sizeof(kResetStats) / sizeof(kResetStats[0]));
float g_skillsBefore[kResetCount] = {};
bool g_skillsKept = false;
void RebuildBody() {
    CPlayerPed* me = FindPlayerPed(0);
    if (me && me->ModelIndex() == 0 && me->ClothesDesc()) RequestRebuild();
}
void ResetSkills(bool keep) {
    for (int i = 0; i < kResetCount; ++i) {
        if (keep) g_skillsBefore[i] = Call<0x558E40, float>(kResetStats[i]);  // CStats::GetStatValue
        Call<0x55A070>(kResetStats[i], 0.0f);                                  // CStats::SetStatValue
    }
    if (keep) g_skillsKept = true;
    RebuildBody();
}
void RestoreSkills() {
    if (!g_skillsKept) return;
    for (int i = 0; i < kResetCount; ++i) Call<0x55A070>(kResetStats[i], g_skillsBefore[i]);
    g_skillsKept = false;
    RebuildBody();
}
void End(bool tell) {
    if (!g_arena.on) return;
    if (tell && g_arena.host) Send(A_END, [](Writer& w) { w.U8(g_arena.won); });
    Clear();
    bool was = g_arena.taken;
    EndSpectate(false);
    CancelDown();
    StopOwnMoves();
    HoldWorld(false);
    PlayerGuns(false, false);
    Cmd(0x055D).I(0).I(0).Run();  // MAKE_PLAYER_FIRE_PROOF off
    if (Ref<CVehicle*>((uintptr_t)&PlayerInfos()[0] + 0xB0)) Call<0x45AE80>(false);  // a Shahed in the air: gone
    if (CPlayerPed* me = FindPlayerPed(0)) Cmd(0x04D7).I(GetPedRef(me)).I(0).Run();
    if (g_arena.host && g_merchant.ped && IsPedValid(g_merchant.ped)) Cmd(0x009B).I(GetPedRef(g_merchant.ped)).Run();
    if (g_merchant.cut) Cmd(0x02A3).I(0).Run(), Cmd(0x015A).Run(), Cmd(0x01B4).I(0).I(1).Run();  // the cut-scene cut short
    g_merchant = MerchantState{};
    ResetShop();
    g_station = -1;
    g_spectators.clear();
    HideSaves(false);
    ShutDoors(false);
    TakeBack();
    if (CPlayerPed* me = FindPlayerPed(0); me && was) me->Armour() = 0.0f;  // the armour of the battle stays there
    if (g_moneyKept) Cash() = g_moneyBefore, g_moneyKept = false;  // the save's money back, the arena's stays there
    RestoreSkills();
    game::challenge::End();
    if (g_modeOn)
        if (CPlayerPed* me2 = FindPlayerPed(0)) Cmd(0x048F).I(GetPedRef(me2)).Run();  // the mode: the save's weapons stay away
    LOGI("Arena over (%s)", g_arena.won ? "won" : "lost");
    g_arena = Arena{};
}

void Start(int faction, int place, int waves, const CVector* spawn) {
    g_arena = Arena{};
    g_arena.on = true;
    g_arena.host = Hosting();
    g_arena.faction = faction;
    g_arena.place = place;
    g_arena.waves = waves;
    g_arena.startMs = NowMs();
    g_arena.nextWaveMs = NowMs() + PrepMs(true);
    {
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof ms;
        if (GlobalMemoryStatusEx(&ms))
            LOGI("Arena start: %u MB of the address space free, the models %u of %u MB", (uint32_t)(ms.ullAvailVirtual >> 20),
                 Ref<uint32_t>(0x8E4CB4) >> 20, Ref<uint32_t>(0x8A5A80) >> 20);
    }
    ResetCod();
    ForgetSpot();
    if (spawn) {  // the players' own spot: in there straight away (TakeIn), not to the middle first
        g_spotOwn = true, g_spotAt = *spawn;
        g_spotApplied = true;  // nobody taken there a second time when the floor is known
    }
    g_station = -1;
    g_bossUgl = g_bossKatana = false;
    g_radioSpots.clear(), g_sodaSpots.clear();
    // every arena from the same money: what was left of the last one is gone (out of the mode the save's own money
    // waits aside and comes back when the arena is over)
    if (!g_modeOn) g_moneyBefore = Cash(), g_moneyKept = true;
    Cash() = kStartPoints;
    ResetSkills(!g_modeOn && !g_skillsKept);
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
        }
    if (CPlayerPed* me = FindPlayerPed(0)) me->Armour() = 100.0f;  // everyone starts in a vest
    TakeIn();
    SendScore();
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

void ShowHelp(const std::string& text, uint64_t ms);
void Buy(int i) {
    if (i < 0 || i >= kShopCount) return;
    const ShopItem& it = kShop[i];
    if (Owned(it)) return;
    if (Cash() < it.price) {
        ui::TheChat().AddError(Format(TR("Нужно $%d.", "You need $%d."), it.price));
        return;
    }
    AddPoints(-it.price);
    if (it.kind == 0) g_armourKind = it.which;
    else if (it.kind == 1) g_gadget = it.which, g_shahedCharges = it.which == 3 ? 1 : g_shahedCharges;
    else (it.which == 1 ? g_modUgl : it.which == 2 ? g_modBayonet : g_modSpark) = true;
    if (it.kind == 2 && it.which == 1) g_uglLoaded = true;
    CallMethod<0x506EA0>((void*)0xB6BC90, 40 /* AE_FRONTEND_PURCHASE_WEAPON: the till */, 0.0f, 1.0f);
    if (CPlayerPed* me = FindPlayerPed(0)) {
        CVector at = me->GetPosition();
        Cmd(0x018C).F(at.x).F(at.y).F(at.z).I(1054).Run();  // ADD_ONE_OFF_SOUND: SOUND_BUY
    }
    LOGI("Arena: bought %s", it.name[1]);
    ShowHelp(Format("%s: %s", kShop[i].name[Lang()], kShop[i].what[Lang()]), 6000);
}

// ---- the merchant comes (host) and goes; every game shows the words and knows him
void SendMerchant(bool on, uint16_t id) {  // host: him (his id once known), and where he came in (the cut-scene's place)
    CVector at = g_merchant.from;
    Send(A_MERCHANT, [&](Writer& w) {
        w.U8(on ? 1 : 0);
        w.U16(id);
        w.Vec({at.x, at.y, at.z});
    });
}
void ShowMerchant(bool on, uint16_t id) {  // every game
    bool coming = on && !g_merchant.on;  // the first word of him (the next ones only bring his id)
    g_merchant.on = on, g_merchant.id = id;
    if (coming) g_merchant.textUntil = NowMs() + 4500, g_merchant.cutFrom = g_merchant.textUntil - 400, g_merchant.cutUntil = g_merchant.textUntil + 5500;
    else SetShopOpen(false), g_merchant.ped = nullptr;
}
void StartMerchant() {  // host
    if (!LoadModelNow(MODEL_MERCHANT)) return;
    CVector at = g_hasMerchantSpot ? g_merchantSpot + CVector{0, 0, 1.0f} : SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 6.0f);
    g_merchant.from = at;
    int h = -1;
    Cmd(0x009A).I(4 /* PED_TYPE_CIVMALE */).I(MODEL_MERCHANT).F(at.x).F(at.y).F(at.z - 1.0f).Out(&h).Run();  // CREATE_CHAR
    CPed* p = h >= 0 ? GetPed(h) : nullptr;
    if (!p) return;
    Peds::Instance().Share(p);
    Cmd(0x0860).I(h).I(kPlaces[g_arena.place].area).Run();     // SET_CHAR_AREA_VISIBLE
    Cmd(0x02AB).I(h).I(1).I(1).I(1).I(1).I(1).Run();             // SET_CHAR_PROOFS: nothing hurts him
    Cmd(0x0223).I(h).I(1000).Run();
    Cmd(0x060B).I(h).I(kEmptyDecisionMaker).Run();              // no fear, no fight
    Cmd(0x087E).I(h).I(0).Run();
    g_merchant.ped = p, g_merchant.wave = g_arena.wave, g_merchant.id = 0;
    // to the boxes: beside the weapon box, on the players' side of it
    CVector to = g_box.placed ? g_box.pos + CVector{0, 0, 0} : kPlaces[g_arena.place].pos - CVector{0, 0, 1.0f};
    if (g_box.placed) {
        float r = g_box.heading / 57.29578f;
        to = to + CVector{-std::sin(r), std::cos(r), 0.0f} * 1.8f;
    }
    if (g_hasMerchantTo) to = g_merchantTo;  // the host's own spot for him (/торговец цель)
    g_merchant.to = to, g_merchant.walking = false;  // he sets off once the words are gone (MerchantFrame)
    g_merchant.on = true;
    g_merchant.textUntil = NowMs() + 4500;
    g_merchant.cutFrom = g_merchant.textUntil - 400, g_merchant.cutUntil = g_merchant.textUntil + 5500;
    LOGI("Arena: the merchant comes after wave %d", g_arena.wave);
}
void EndMerchant() {  // host
    if (g_merchant.ped && IsPedValid(g_merchant.ped)) Cmd(0x009B).I(GetPedRef(g_merchant.ped)).Run();  // DELETE_CHAR
    g_merchant.ped = nullptr;
    SendMerchant(false, 0);
    ShowMerchant(false, 0);
}
// every game: the merchant's coming in, a short cut-scene once the words are gone: the bars, a fixed camera beside where
// he came in following him, the player held still; then everything as it was
void MerchantCut(uint64_t now) {
    CPlayerPed* me = FindPlayerPed(0);
    bool want = g_merchant.on && now >= g_merchant.cutFrom && now < g_merchant.cutUntil && me && me->Health() > 0.0f && !g_spectating &&
                !Players::Instance().LocalDowned() && arena::On();
    if (want && !g_merchant.cut) {
        g_merchant.cut = true;
        Cmd(0x01B4).I(0).I(0).Run();  // SET_PLAYER_CONTROL off
        Cmd(0x02A3).I(1).Run();       // SWITCH_WIDESCREEN on
    }
    if (want) {
        CVector look = g_merchant.ped && IsPedValid(g_merchant.ped) ? g_merchant.ped->GetPosition() : g_merchant.from;
        CVector cam = g_merchant.from + CVector{2.6f, -2.6f, 1.4f};
        CVector hit;
        if (Ray(look + CVector{0, 0, 0.4f}, cam, hit)) cam = hit + (look - hit) * (0.3f / std::max(0.3f, (look - hit).Length()));  // not in a wall
        Cmd(0x015F).F(cam.x).F(cam.y).F(cam.z).F(0.0f).F(0.0f).F(0.0f).Run();  // SET_FIXED_CAMERA_POSITION
        Cmd(0x0160).F(look.x).F(look.y).F(look.z + 0.3f).I(2).Run();            // POINT_CAMERA_AT_POINT
    } else if (g_merchant.cut) {
        g_merchant.cut = false;
        Cmd(0x02A3).I(0).Run();       // SWITCH_WIDESCREEN off
        Cmd(0x015A).Run();            // RESTORE_CAMERA_JUMPCUT
        Cmd(0x01B4).I(0).I(1).Run();  // SET_PLAYER_CONTROL on
    }
}
void MerchantFrame(uint64_t now) {  // every game
    MerchantCut(now);
    if (!g_merchant.on) return;
    // nobody to see till the words are gone ("Но никто не пришел."): then he comes in
    if (g_merchant.ped && IsPedValid(g_merchant.ped)) Cmd(0x0337).I(GetPedRef(g_merchant.ped)).I(now >= g_merchant.textUntil ? 1 : 0).Run();  // SET_CHAR_VISIBLE
    if (g_arena.host) {
        CPed* p = g_merchant.ped;
        if (p && IsPedValid(p)) {
            uint16_t id = 0;
            if (NetPed* np = Peds::Instance().Find(p)) id = np->netId;
            if (id != g_merchant.id) {  // its id at last: the partners told
                g_merchant.id = id;
                SendMerchant(true, id);
            }
            if (!g_merchant.walking && now >= g_merchant.textUntil) {  // the words gone: on his way, walking
                g_merchant.walking = true;
                CVector to = g_merchant.to;
                Cmd(0x05D3).I(GetPedRef(p)).F(to.x).F(to.y).F(to.z + 1.0f).I(4).I(20000).Run();  // TASK_GO_STRAIGHT_TO_COORD
                g_merchant.walkUntil = now + 20000;
            }
            if (g_merchant.walkUntil && now > g_merchant.walkUntil && g_box.placed) {  // stuck on the way: there
                g_merchant.walkUntil = 0;
                SetEntityPosition(p, (g_hasMerchantTo ? g_merchantTo : g_box.pos + CVector{1.8f, 0, 0}) + CVector{0, 0, 1.0f});
            }
        }
    } else if (!g_merchant.ped || !IsPedValid(g_merchant.ped)) {
        int h = g_merchant.id ? Peds::Instance().HandleOf(g_merchant.id) : -1;
        g_merchant.ped = h >= 0 ? GetPed(h) : nullptr;
    }
    CPlayerPed* me = FindPlayerPed(0);
    if (g_shopOpen && (!me || !g_merchant.ped || (me->GetPosition() - g_merchant.ped->GetPosition()).Length() > 3.5f)) SetShopOpen(false);
}
bool NearMerchant() {
    CPlayerPed* me = FindPlayerPed(0);
    return g_merchant.on && g_merchant.ped && IsPedValid(g_merchant.ped) && me &&
           (me->GetPosition() - g_merchant.ped->GetPosition()).Length() < 2.6f;
}
// The merchant's screen, as Ammu-Nation's: three tabs (armour, gadgets, mods), a card each item; W/S or the arrows pick,
// A/D the tab, Enter, Space or a click buys, Esc, Backspace or E leave. The mouse is free while it is open.
int g_shopTab = 0, g_shopSel = 0;
uint64_t g_shopOpenedMs = 0;
float g_shopMouseX = -1, g_shopMouseY = -1;
void SetShopOpen(bool on) {
    if (on == g_shopOpen) return;
    g_shopOpen = on;
    ui::In().SetKeyboardCapture(on);
    ui::In().SetMouseCapture(on);
    if (on) g_shopSel = 0, g_shopOpenedMs = NowMs();
}
void DrawShop() {
    auto& g = ui::Gfx();
    float s = g.Scale();
    uint64_t now = NowMs();
    if (g_merchant.on && now < g_merchant.textUntil) {  // "nobody came", over the whole screen
        float a = std::min(1.0f, (float)(g_merchant.textUntil - now) / 800.0f);
        g.Rect(0, 0, g.Width(), g.Height(), ui::WithAlpha(0xA0000000, a));
        g.Text(ui::FONT_BIG, g.Width() * 0.5f, g.Height() * 0.45f, TR("Но никто не пришел.", "But nobody came."), ui::WithAlpha(0xFFE0E0E0, a),
               ui::TEXT_CENTER | ui::TEXT_OUTLINE);
    }
    if (g_merchant.on && g_merchant.ped && IsPedValid(g_merchant.ped))
        Label(g_merchant.ped->GetPosition() + CVector{0, 0, 1.2f}, TR("Торговец  [E]", "Merchant  [E]"), 0xFFE1C35A, 40.0f, true);
    if (!g_shopOpen) return;
    // this tab's items
    std::vector<int> items;
    for (int i = 0; i < kShopCount; ++i)
        if (kShop[i].kind == g_shopTab) items.push_back(i);
    auto& st = ui::In().State();
    bool buy = false;
    for (int vk : st.keyDowns) {
        if (vk == 'E' && now - g_shopOpenedMs < 500) continue;  // the E that opened it (and its repeats)
        if (vk == VK_ESCAPE || vk == VK_BACK || vk == 'E') return SetShopOpen(false);
        if (vk == 'A' || vk == VK_PRIOR) g_shopTab = (g_shopTab + 2) % 3, g_shopSel = 0;
        if (vk == 'D' || vk == VK_NEXT || vk == VK_TAB) g_shopTab = (g_shopTab + 1) % 3, g_shopSel = 0;
        if (vk == 'W' || vk == VK_UP || vk == VK_LEFT) g_shopSel = std::max(0, g_shopSel - 1);
        if (vk == 'S' || vk == VK_DOWN || vk == VK_RIGHT) g_shopSel = std::min((int)items.size() - 1, g_shopSel + 1);
        if (vk == VK_RETURN || vk == VK_SPACE) buy = true;
    }
    items.clear();
    for (int i = 0; i < kShopCount; ++i)
        if (kShop[i].kind == g_shopTab) items.push_back(i);
    g_shopSel = std::clamp(g_shopSel, 0, std::max(0, (int)items.size() - 1));
    float W = 1000 * s, H = 600 * s;
    float x = (g.Width() - W) * 0.5f, y = (g.Height() - H) * 0.5f;
    g.Rect(0, 0, g.Width(), g.Height(), 0x70000000);
    g.Rect(x, y, W, H, 0xF0000000);
    g.RectOutline(x, y, W, H, 0xFF26303B, std::max(1.0f, s));
    g.GameText(ui::GAME_FONT_GOTHIC, x + 40 * s, y + 14 * s, "Merchant", 56 * s, 0xFFACCBF1);  // the menus' own title
    g.GameText(ui::GAME_FONT_PRICEDOWN, x + W - 40 * s, y + 24 * s, Format("$%d", Cash()), 40 * s, 0xFF36682C, ui::TEXT_RIGHT);
    auto hover = [&](float hx, float hy, float hw, float hh) { return st.mouseX >= hx && st.mouseX < hx + hw && st.mouseY >= hy && st.mouseY < hy + hh; };
    // the tabs
    const char* tabs[3] = {TR("[ БРОНЯ ]", "[ ARMOUR ]"), TR("[ ГАДЖЕТЫ ]", "[ GADGETS ]"), TR("[ МОДИФИКАЦИИ ]", "[ MODS ]")};
    float ty = y + 90 * s, tw = (W - 80 * s) / 3.0f, th = g.LineHeight(ui::FONT_MENU) + 14 * s;
    for (int t = 0; t < 3; ++t) {
        float tx = x + 40 * s + tw * (float)t;
        bool hv = hover(tx, ty, tw, th);
        if (hv && st.lmbPressed && t != g_shopTab) g_shopTab = t, g_shopSel = 0, st.lmbPressed = false;
        bool act = t == g_shopTab;
        if (act) g.Rect(tx + 4 * s, ty, tw - 8 * s, th, 0x40ACCBF1);
        g.Text(ui::FONT_MENU, tx + tw * 0.5f, ty + 7 * s, tabs[t], act ? 0xFFACCBF1 : hv ? 0xFF8D9CAD : 0xFF4A5A6B, ui::TEXT_CENTER | ui::TEXT_SHADOW);
    }
    // the cards
    float cy = ty + th + 24 * s, gap = 20 * s;
    float cw = (W - 80 * s - gap * 2) / 3.0f, chh = H - (cy - y) - 70 * s;
    for (size_t k = 0; k < items.size() && k < 3; ++k) {
        const ShopItem& it = kShop[items[k]];
        float cx = x + 40 * s + (cw + gap) * (float)k;
        bool hv = hover(cx, cy, cw, chh);
        if (hv && (st.mouseX != g_shopMouseX || st.mouseY != g_shopMouseY)) g_shopSel = (int)k;  // the mouse moved onto it
        bool sel = g_shopSel == (int)k;
        bool own = Owned(it), can = Cash() >= it.price;
        g.Rect(cx, cy, cw, chh, sel ? 0xE0182230 : 0xC0101418);
        g.RectOutline(cx, cy, cw, chh, sel ? 0xFFACCBF1 : 0xFF26303B, std::max(1.0f, s) * (sel ? 2.5f : 1.0f));
        float py = cy + 16 * s;
        for (auto& line : g.Wrap(ui::FONT_MENU, ui::Widen(it.name[Lang()]), cw - 32 * s)) {
            g.TextW(ui::FONT_MENU, cx + cw * 0.5f, py, line, sel ? 0xFFACCBF1 : 0xFFFFFFFF, ui::TEXT_CENTER | ui::TEXT_SHADOW);
            py += g.LineHeight(ui::FONT_MENU);
        }
        py += 10 * s;
        if (own) g.Text(ui::FONT_MENU, cx + cw * 0.5f, py, it.kind == 0 ? TR("НАДЕТО", "WORN") : TR("КУПЛЕНО", "BOUGHT"), 0xFF5FA04E, ui::TEXT_CENTER | ui::TEXT_SHADOW);
        else g.GameText(ui::GAME_FONT_PRICEDOWN, cx + cw * 0.5f, py, Format("$%d", it.price), 34 * s, can ? 0xFF36682C : 0xFFB4191D, ui::TEXT_CENTER);
        py += 46 * s;
        for (auto& line : g.Wrap(ui::FONT_SMALL, ui::Widen(it.what[Lang()]), cw - 32 * s)) {
            g.TextW(ui::FONT_SMALL, cx + 16 * s, py, line, 0xFFC8C8C8, ui::TEXT_SHADOW);
            py += g.LineHeight(ui::FONT_SMALL);
        }
        if (hv && st.lmbPressed) buy = true, g_shopSel = (int)k, st.lmbPressed = false;
    }
    g_shopMouseX = st.mouseX, g_shopMouseY = st.mouseY;
    if (buy && g_shopSel < (int)items.size()) Buy(items[(size_t)g_shopSel]);
    g.Text(ui::FONT_SMALL, x + W * 0.5f, y + H - 34 * s,
           TR("W/S, стрелки — выбор    A/D — вкладка    Enter, Пробел, ЛКМ — купить    Esc, E — выйти",
              "W/S, arrows: pick    A/D: tab    Enter, Space, click: buy    Esc, E: leave"),
           0xFF8D9CAD, ui::TEXT_CENTER | ui::TEXT_SHADOW);
    ui::w::DrawCursor();
}

// ---- the armours (while there is armour on): the damage it takes, speed, clips, fire, the gifts each wave
uint32_t ArmourColour() { return g_armourKind == 1 ? 0xFF5A46C8 : g_armourKind == 2 ? 0xFFFF8C1A : g_armourKind == 3 ? 0xFF3C9A3C : 0xFFE1E1E1; }
const char* ArmourName() {
    return g_armourKind == 1 ? TR("Броня Джаггернаута", "Juggernaut armour") : g_armourKind == 2 ? TR("Броня Поджигателя", "Firestarter armour")
           : g_armourKind == 3 ? TR("Броня Солдата", "Soldier armour") : "";
}
bool ArmourOn() {
    CPlayerPed* me = FindPlayerPed(0);
    return g_armourKind && me && me->Armour() >= 1.0f;
}
// the player's own weapon kinds (the poor and the hitman skill: the enemies use the middle one): clips, accuracy, range
struct InfoBackup {
    void* info;
    uint16_t clip;
    float acc, range;
};
std::vector<InfoBackup> g_infoBackup;
void PlayerGuns(bool bigClips, bool sharp) {
    for (InfoBackup& b : g_infoBackup) Field<uint16_t>(b.info, 0x20) = b.clip, Field<float>(b.info, 0x38) = b.acc, Field<float>(b.info, 0x08) = b.range;
    g_infoBackup.clear();
    if (!bigClips && !sharp) return;
    for (int w = 22; w <= 34; ++w)
        for (uint8_t skill : {(uint8_t)0, (uint8_t)2}) {
            void* info = Call<0x743C60, void*>(w, skill);  // CWeaponInfo::GetWeaponInfo
            if (!info) continue;
            InfoBackup b{info, Field<uint16_t>(info, 0x20), Field<float>(info, 0x38), Field<float>(info, 0x08)};
            g_infoBackup.push_back(b);
            if (bigClips && b.clip > 1) Field<uint16_t>(info, 0x20) = (uint16_t)(b.clip * 3 / 2);
            if (sharp) Field<float>(info, 0x38) = b.acc * 1.6f, Field<float>(info, 0x08) = b.range * 1.3f;
        }
}
void SpeedFactor(float f) {  // the running's animations: faster or slower (the body moves with them)
    CPlayerPed* me = FindPlayerPed(0);
    void* clump = me ? me->RwObject() : nullptr;
    static float last = 1.0f;
    if (!clump || (std::fabs(f - 1.0f) < 0.01f && std::fabs(last - 1.0f) < 0.01f)) return;  // as the game has it
    last = f;
    for (uint32_t anim : {0u, 1u, 2u})  // ANIM_ID_WALK, RUN, SPRINT
        if (void* assoc = Call<0x4D68B0, void*>(clump, anim)) Ref<float>((uintptr_t)assoc + 0x24) = f;
}
int OutgoingDamageArena(int weapon, int damage) {
    if (g_rageUntil && NowMs() < g_rageUntil && weapon <= 15) return damage * 2;  // the rage: blows twice as hard
    return damage;
}
int ArmourDamage(int weapon, int damage) {  // the damage to this player while its armour is on
    if (!ArmourOn()) return damage;
    if (g_armourKind == 2 && (weapon == 37 || weapon == 18)) return 0;  // fire does not burn
    if (g_armourKind == 1) return std::max(1, damage * 2 / 3);         // the armour lasts half as long again
    if (g_armourKind == 3) return damage * 4 / 3;                       // a quarter less
    return damage;
}
void ArmourWave() {  // each wave: the Firestarter's gifts
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || g_armourKind != 2) return;
    int h = GetPedRef(me);
    if (!SlotTaken(me, WEAPON_GRENADE) || SlotTaken(me, WEAPON_MOLOTOV)) Give(h, WEAPON_MOLOTOV, 2);
    if (!SlotTaken(me, 38) && !SlotTaken(me, 35) && !SlotTaken(me, 36)) Give(h, 37, 200);  // not over a heavier gun
}
void DrawArmourBar() {  // the game's own bar has the armour's colour (HookRenderArmorBar); its name under the money
    if (!g_armourKind) return;
    auto& g = ui::Gfx();
    float s = g.Scale();
    g.Text(ui::FONT_SMALL, g.Width() - 40 * s, g.Height() * 0.285f, ArmourName(), ArmourColour() | 0xFF000000, ui::TEXT_RIGHT | ui::TEXT_OUTLINE);
}

// ---- the gadgets (X)
void RampageOn() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me) return;
    g_rageStunUntil = 0;
    g_rageUntil = NowMs() + 25000;
    g_rageTextUntil = NowMs() + 2500;
    me->Health() = 200.0f;
    Cmd(0x01B4).I(0).I(1).Run();  // SET_PLAYER_CONTROL: back
    Cmd(0x0687).I(GetPedRef(me)).Run();
    CallMethod<0x506EA0>((void*)0xB6BC90, 32 /* a frontend sting */, 0.0f, 1.0f);
}
void UseGadget() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || !g_gadget) return;
    uint64_t now = NowMs();
    int h = GetPedRef(me);
    if (g_gadget == 1) {  // the visor on and off
        if (!g_visorOn && (g_break || now - g_arena.waveMs < 3000)) {
            ShowHelp(TR("Визор включается через 3 с после начала волны.", "The visor comes on 3 s into a wave."), 3000);
            return;
        }
        if (!g_visorOn && g_visorCharge <= 0.5f) {
            ShowHelp(TR("Визор разряжен: убийства заряжают его.", "The visor is flat: kills charge it."), 3000);
            return;
        }
        g_visorOn = !g_visorOn;
        CallMethod<0x506EA0>((void*)0xB6BC90, 1, 0.0f, 1.0f);
        return;
    }
    if (g_gadget == 2) {  // the rage dose
        if (g_arena.wave < g_rageReadyWave || g_rageStunUntil || (g_rageUntil && now < g_rageUntil)) {
            ShowHelp(Format(TR("Рейдж-доза снова с волны %d.", "The rage dose again from wave %d."), g_rageReadyWave), 3000);
            return;
        }
        g_rageReadyWave = g_arena.wave + 2;
        if (Players::Instance().LocalDowned()) CancelDown();  // wounded on the floor: it gets him up
        Cmd(0x01B4).I(0).I(0).Run();  // SET_PLAYER_CONTROL off: the fits
        if (EnsureBlock("CRACK")) Cmd(0x0605).I(h).S("crckdeth2").S("CRACK").F(4.0f).I(1).I(0).I(0).I(0).I(3000).Run();  // on the floor, shaking
        g_rageStunUntil = now + 3000;
        return;
    }
    if (g_gadget == 3) {  // the Shahed
        if (Ref<CVehicle*>((uintptr_t)&PlayerInfos()[0] + 0xB0)) return;  // one flying already
        if (g_shahedCharges <= 0) {
            ShowHelp(TR("Нет заряда: нужно 2 груды хлама со сбитых дронов.", "No charge: 2 heaps of scrap off downed drones are needed."), 3500);
            return;
        }
        --g_shahedCharges;
        // in front of him and high (15 m, or under the ceiling of the place), never on him: he stood on it and rode it
        float hd = me->CurrentRotation();
        CVector base = me->GetPosition() + CVector{-std::sin(hd), std::cos(hd), 0.0f} * 3.0f;
        CVector top;
        float up = Ray(base, base + CVector{0, 0, 10.0f}, top) ? std::max(2.5f, top.z - base.z - 2.0f) : 8.0f;
        CVector at = base + CVector{0, 0, std::min(8.0f, up)};  // a little lower than it was (15 m)
        Cmd(0x04D7).I(h).I(1).Run();  // FREEZE_CHAR_POSITION: CJ stays on the floor while he steers
        LoadModelNow(501);  // the RC Goblin
        Cmd(0x046E).I(0).F(at.x).F(at.y).F(at.z).F(me->CurrentRotation() * 57.29578f).I(501).Run();  // GIVE_REMOTE_CONTROLLED_MODEL_TO_PLAYER
        if (CVehicle* rc = Ref<CVehicle*>((uintptr_t)&PlayerInfos()[0] + 0xB0))
            Cmd(0x0840).I(GetVehicleRef(rc)).I(kPlaces[g_arena.place].area).Run();  // SET_VEHICLE_AREA_VISIBLE
        g_shahedSince = now;
    }
}
void ShahedBoom(CVehicle* rc) {
    CVector at = rc->GetPosition();
    Call<0x45AE80>(false);  // CRemote::TakeRemoteControlledCarFromPlayer: the control back to CJ
    if (CPlayerPed* me = FindPlayerPed(0)) Cmd(0x04D7).I(GetPedRef(me)).I(0).Run();  // FREEZE_CHAR_POSITION off
    Cmd(0x015A).Run();  // RESTORE_CAMERA_JUMPCUT: the camera back on CJ (it stayed on the blast)
    Cmd(0x0373).Run();  // SET_CAMERA_BEHIND_PLAYER
    Call<0x736A50>((CEntity*)nullptr, (CEntity*)FindPlayerPed(0), 2 /* EXPLOSION_ROCKET */, at, (uint32_t)0, (uint8_t)1, -1.0f, (uint8_t)0);  // AddExplosion: his
    g_shahedSince = 0;
}
// the visor on: each shot of this player's goes into the head of the nearest enemy it can see (the game's own damage
// with the head as the piece hit: a headshot; the hit goes to the man's own game as any other)
void VisorShots() {
    static int lastType = -1, lastClip = 0;
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || me->Health() <= 0.0f) return;
    const CWeapon& w = me->ActiveWeapon();
    bool shot = w.type == lastType && w.ammoInClip < lastClip && w.type >= 22 && w.type <= 38;
    lastType = w.type, lastClip = w.ammoInClip;
    if (!shot || !g_visorOn) return;
    CVector eye = me->GetPosition() + CVector{0, 0, 0.6f};
    CPed* best = nullptr;
    float bestD = 80.0f;
    CPoolHeader* pool = PedPool();
    for (int i = 0; pool && i < pool->size; ++i) {
        if (pool->byteMap[i] < 0) continue;
        auto* q = (CPed*)(pool->objects + (size_t)i * kPedPoolElemSize);
        if (q->IsPlayer() || q->Health() <= 0.0f || !arena::IsEnemy(q) || q == g_merchant.ped) continue;
        float d = (q->GetPosition() - me->GetPosition()).Length();
        if (d < bestD && Clear(eye, q->GetPosition() + CVector{0, 0, 0.65f})) bestD = d, best = q;
    }
    if (!best) return;
    void* info = Call<0x743C60, void*>(w.type, (uint8_t)0);  // CWeaponInfo::GetWeaponInfo
    int damage = info ? std::max<int>(1, Field<int16_t>(info, 0x22)) : 25;
    Call<0x73A530, bool>(best, (CEntity*)me, w.type, damage, 9 /* PED_PIECE_HEAD */, 0);  // CWeapon::GenerateDamageEvent
}
void GadgetFrame(uint64_t now) {
    VisorShots();
    CPlayerPed* me = FindPlayerPed(0);
    static uint64_t last = now;
    float dt = (float)std::min<uint64_t>(now - last, 200) / 1000.0f;
    last = now;
    if (!me) return;
    if (g_visorOn) {
        g_visorCharge -= 3.0f * dt;
        if (g_visorCharge <= 0.0f || g_break) g_visorCharge = std::max(0.0f, g_visorCharge), g_visorOn = false;
    }
    if (g_rageStunUntil && now >= g_rageStunUntil) RampageOn();
    if (g_rageUntil && now >= g_rageUntil) {
        g_rageUntil = 0;
        if (me->Health() > MaxHealth()) me->Health() = MaxHealth();
    }
    if (g_shahedSince && !Ref<CVehicle*>((uintptr_t)&PlayerInfos()[0] + 0xB0) && now - g_shahedSince > 1000) {  // gone some other way
        g_shahedSince = 0;
        Cmd(0x04D7).I(GetPedRef(me)).I(0).Run();  // FREEZE_CHAR_POSITION off
    }
    if (CVehicle* rc = Ref<CVehicle*>((uintptr_t)&PlayerInfos()[0] + 0xB0)) {  // the Shahed: into a man, onto the floor, or 25 s
        bool hit = now - g_shahedSince > 25000;
        CVector at = rc->GetPosition();
        float z = 0;
        if (FloorNoBars(at.x, at.y, at.z + 0.2f, z) && at.z - z < 0.7f && now - g_shahedSince > 1500) hit = true;
        CPoolHeader* pool = PedPool();
        for (int i = 0; pool && i < pool->size && !hit; ++i) {
            if (pool->byteMap[i] < 0) continue;
            auto* q = (CPed*)(pool->objects + (size_t)i * kPedPoolElemSize);
            if (q->IsPlayer() || q->Health() <= 0.0f || !arena::IsEnemy(q) || q == g_merchant.ped) continue;
            if ((q->GetPosition() - at).Length() < 2.2f) hit = true;
        }
        if (hit) ShahedBoom(rc);
    }
    // the armour's effects while it is on (and the rage's)
    bool on = ArmourOn();
    bool rage = g_rageUntil && now < g_rageUntil;
    static int applied = -1;
    int want = (on && g_armourKind == 1 ? 1 : 0) | ((on && g_armourKind == 3) || rage ? 2 : 0);
    if (want != applied) applied = want, PlayerGuns(want & 1, (want & 2) != 0);
    static bool fireproof = false;
    bool fp = on && g_armourKind == 2;
    if (fp != fireproof) fireproof = fp, Cmd(0x055D).I(0).I(fp ? 1 : 0).Run();  // MAKE_PLAYER_FIRE_PROOF
    float speed = rage ? 1.25f : !on ? 1.0f : g_armourKind == 1 ? 0.9f : g_armourKind == 2 ? 0.85f : 1.07f;
    SpeedFactor(speed);
    // the twin mags: every second reload fast (the first one after the buying too)
    static bool reloading = false;
    static int reloads = 0;
    CWeapon& w = me->ActiveWeapon();
    bool now2 = w.state == 2;  // WEAPONSTATE_RELOADING
    if (g_modSpark && now2 && !reloading) {
        ++reloads;
        if (reloads % 2 == 1) {
            uint32_t t = TimeMs();
            if (w.timeForNextShot > t) w.timeForNextShot = t + (w.timeForNextShot - t) / 2;
        }
    }
    if (g_modSpark && now2 && reloads % 2 == 1)
        if (void* clump = me->RwObject())
            for (int anim : {226, 227})
                if (void* assoc = Call<0x4D68B0, void*>(clump, (uint32_t)anim)) Ref<float>((uintptr_t)assoc + 0x24) = 2.0f;
    reloading = now2;
}
void DrawGadgets(uint64_t now) {
    auto& g = ui::Gfx();
    float s = g.Scale();
    if (g_visorOn) {  // the night-vision's green and the men through the walls
        g.Rect(0, 0, g.Width(), g.Height(), 0x3020FF40);
        std::vector<CVector> at;
        CPoolHeader* pool = PedPool();
        for (int i = 0; pool && i < pool->size; ++i) {
            if (pool->byteMap[i] < 0) continue;
            auto* q = (CPed*)(pool->objects + (size_t)i * kPedPoolElemSize);
            if (!q->IsPlayer() && q->Health() > 0.0f && arena::IsEnemy(q) && q != g_merchant.ped) at.push_back(q->GetPosition());
        }
        ui::DrawWorldMarks(at, 0xE060FF60);
    }
    if (g_rageStunUntil) g.Rect(0, 0, g.Width(), g.Height(), 0x40FF0000);
    if (now < g_rageTextUntil) {  // RAMPAGE over the whole screen
        float k = (float)(g_rageTextUntil - now) / 2500.0f;
        float hgt = (150.0f + 60.0f * k) * s;
        g.GameText(ui::GAME_FONT_PRICEDOWN, g.Width() * 0.5f, g.Height() * 0.4f, "RAMPAGE", hgt, ui::WithAlpha(0xFFC8282D, std::min(1.0f, k * 2)), ui::TEXT_CENTER);
    }
    if (g_gadget) {  // the gadget's state under the armour's bar
        std::string t = g_gadget == 1   ? Format(TR("Визор: %.0f%%%s", "Visor: %.0f%%%s"), g_visorCharge, g_visorOn ? TR(" (вкл)", " (on)") : "")
                        : g_gadget == 2 ? (g_arena.wave >= g_rageReadyWave ? std::string(TR("Рейдж-доза: готова", "Rage dose: ready"))
                                                                           : Format(TR("Рейдж-доза: с волны %d", "Rage dose: from wave %d"), g_rageReadyWave))
                                        : Format(TR("Shahed: %d  (хлам %d/2)", "Shahed: %d  (scrap %d/2)"), g_shahedCharges, g_scrap);
        g.Text(ui::FONT_SMALL, g.Width() - 40 * s, g.Height() * 0.285f + g.LineHeight(ui::FONT_SMALL) + 2 * s, t, 0xFFFFFFFF, ui::TEXT_RIGHT | ui::TEXT_OUTLINE);
    }
}

// ---- the mods (N): the bayonet's thrust; the launcher on any gun goes through PlayerUgl
bool Bayonet() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || !g_modBayonet) return false;
    int w = me->ActiveWeapon().type;
    if (w < 22 || w > 34) return false;
    float hd = me->CurrentRotation();
    CVector f{-std::sin(hd), std::cos(hd), 0.0f};
    CPed* best = nullptr;
    float bestD = 2.4f;
    CPoolHeader* pool = PedPool();
    for (int i = 0; pool && i < pool->size; ++i) {
        if (pool->byteMap[i] < 0) continue;
        auto* q = (CPed*)(pool->objects + (size_t)i * kPedPoolElemSize);
        if (q->IsPlayer() || q->Health() <= 0.0f || !arena::IsEnemy(q) || q == g_merchant.ped) continue;
        CVector d = q->GetPosition() - me->GetPosition();
        if (std::fabs(d.z) > 1.2f) continue;
        d.z = 0;
        float l = d.Length();
        if (l < bestD && (l < 0.4f || (d.x * f.x + d.y * f.y) / l > 0.5f)) bestD = l, best = q;
    }
    if (!best) return false;
    // the thrust: CJ's own punch (the fist fighting's first blow) over the upper body, the gun staying in his hand
    Cmd(0x0A1A).I(GetPedRef(me)).S("FIGHTA_1").S("PED").F(8.0f).I(0).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM_SECONDARY
    uint16_t id = 0;
    if (NetPed* np = Peds::Instance().Find(best)) id = np->netId;
    if (g_arena.host) BayonetHit(best);
    else if (id) Send(A_BAYONET, [id](Writer& wr) { wr.U16(id); });
    return true;
}

void BayonetHit(CPed* p) {  // host: the thrust got to him: hurt, and down on the floor a moment
    auto it = g_info.find(p);
    if (it == g_info.end() || p->Health() <= 0.0f) return;
    Field<int8_t>(p, 0x760) = 4;  // CPed::m_nLastWeaponDamage: the knife (a melee kill: double money)
    int h = GetPedRef(p);
    if (p->Health() <= 45.0f) {
        Cmd(0x0223).I(h).I(0).Run();
        return;
    }
    p->Health() -= 45.0f;
    if (it->second.role == R_TWIN) return;  // the Twins are not knocked down
    Cmd(0x0687).I(h).Run();
    Cmd(0x0605).I(h).S("KO_skid_back").S("PED").F(8.0f).I(0).I(0).I(0).I(1).I(-1).Run();  // knocked onto his back
    it->second.getUpUntil = NowMs() + 2200;
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
    if (r.Ok() && op == A_GUARDNAME) {  // another game's homie: its name, for its copy here
        uint16_t id = r.U16();
        uint8_t name = r.U8();
        uint8_t owner = r.U8();
        if (!r.Ok() || name >= 5) return;
        g_guardIds.push_back({id, name});
        if (owner == Net().MyId()) ui::TheChat().AddSystem(Format(TR("Кент %s с вами.", "%s, a homie, is with you."), kGuardNames[name][Lang()]));
        return;
    }
    if (r.Ok() && op == A_MAGNET) {  // host: a partner's magnet took a kill's money
        uint16_t id = r.U16();
        if (!r.Ok() || !g_arena.host) return;
        int h = Peds::Instance().HandleOf(id);
        if (CPed* p = h >= 0 ? GetPed(h) : nullptr) Magnet(p, from);
        return;
    }
    if (r.Ok() && op == A_BAYONET) {  // host: a partner's bayonet
        uint16_t id = r.U16();
        int h = r.Ok() && g_arena.host ? Peds::Instance().HandleOf(id) : -1;
        if (CPed* p = h >= 0 ? GetPed(h) : nullptr) BayonetHit(p);
        return;
    }
    if (r.Ok() && op == A_EBARHIT) {  // host: a partner hit a builder's barricade
        uint8_t i = r.U8();
        uint16_t dmg = r.U16();
        if (r.Ok() && g_arena.host) HitEBar(i, dmg);
        return;
    }
    if (r.Ok() && op == A_SAWHIT) {  // host: a partner's chainsaw dash got to one of the arena's people
        uint16_t id = r.U16();
        if (!r.Ok() || !g_arena.host) return;
        int h = Peds::Instance().HandleOf(id);
        if (CPed* p = h >= 0 ? GetPed(h) : nullptr) SawKill(p);
        return;
    }
    if (op == A_HALVE) {  // the host's katana boss got to this player
        if (from == Net().HostId() && g_arena.on && g_arena.taken) LoseHalf();
        return;
    }
    if (r.Ok() && op == A_GUARDREQ) {  // host: a partner took a homie from the box
        if (g_arena.on && g_arena.host) SpawnBodyguard(from);
        return;
    }
    if (op == A_DRONEHIT) {  // host: a partner hit a helicopter's copy
        uint16_t id = r.U16();
        float dmg = r.F32();
        if (!r.Ok() || !g_arena.host || dmg <= 0.0f) return;
        NetVehicle* nv = Vehicles::Instance().Find(id);
        for (Drone& d : g_drones)
            if (nv && GetVehicle(d.handle) == nv->veh) d.hp -= std::min(dmg, 200.0f);
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
        uint8_t mode = r.U8();
        if (r.Ok() && mode) g_modeOn = true;
        uint8_t own = r.U8();
        Vec3 sp = r.Vec();
        CVector spawn{sp.x, sp.y, sp.z};
        bool hasSpawn = r.Ok() && own;
        if (g_arena.on) End(false);
        if (g_lobby) OpenLobby(false);
        Start(f, p, w, hasSpawn ? &spawn : nullptr);
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
    } else if (op == A_PROPS) {
        std::vector<Prop> lists[2];
        for (auto& list : lists) {
            uint8_t n = r.U8();
            for (int k = 0; k < n && k < 4; ++k) {
                Prop pr;
                Vec3 v = r.Vec();
                pr.pos = {v.x, v.y, v.z};
                pr.heading = r.F32();
                list.push_back(pr);
            }
        }
        if (!r.Ok()) return;
        g_radioSpots = lists[0], g_sodaSpots = lists[1];
        ShowProps();
    } else if (op == A_ZONE) {
        int8_t idx = r.I8();
        Vec3 v = r.Vec();
        if (r.Ok()) ShowZone(idx, {v.x, v.y, v.z});
    } else if (op == A_MERCHANT) {  // the merchant comes (his id once it is known) or goes
        uint8_t on = r.U8();
        uint16_t id = r.U16();
        Vec3 at = r.Vec();
        if (!r.Ok()) return;
        if (on && !g_merchant.on) g_iVoted = false, g_merchant.from = {at.x, at.y, at.z};
        ShowMerchant(on != 0, id);
    } else if (op == A_SAWWIND) {  // the Butcher winds up (his copy here roars too)
        uint16_t id = r.U16();
        int h = r.Ok() ? Peds::Instance().HandleOf(id) : -1;
        if (CPed* p = h >= 0 ? GetPed(h) : nullptr) SawRoar(p, NowMs() + 1800);
    } else if (op == A_NEXTBOSS) {
        uint8_t b = r.U8();
        if (r.Ok() && b < kBossCount) g_nextBoss = b;
    } else if (op == A_EVENT) {
        uint8_t kind = r.U8();
        if (r.Ok()) ShowEvent(kind);
    } else if (op == A_MVP) {
        uint8_t id = r.U8();
        uint16_t kills = r.U16();
        if (r.Ok()) ShowMvp(id, kills);
    } else if (op == A_EBARGONE) {
        uint8_t i = r.U8();
        if (r.Ok()) EBarDown(i);
    } else if (op == A_EBAR) {
        Vec3 v = r.Vec();
        float hd = r.F32();
        if (r.Ok()) AddEBar({v.x, v.y, v.z}, hd);
    } else if (op == A_SPOT) {
        bool own = r.U8() != 0;
        Vec3 at = r.Vec();
        float minZ = r.F32(), lowZ = r.F32();
        uint8_t n = r.U8();
        std::vector<Blocker> blockers;
        for (int k = 0; k < n && k < 40; ++k) {
            Blocker b;
            Vec3 v = r.Vec();
            b.pos = {v.x, v.y, v.z};
            b.heading = r.F32();
            b.stack = std::clamp<int>(r.U8(), 1, 5);
            blockers.push_back(b);
        }
        if (!r.Ok()) return;
        bool same = blockers.size() == g_blockers.size();
        for (size_t i = 0; same && i < blockers.size(); ++i) same = (blockers[i].pos - g_blockers[i].pos).Length() < 0.1f;
        if (!same) ClearBlockers(), g_blockers = blockers;
        g_spotOwn = own, g_spotAt = {at.x, at.y, at.z}, g_minZ = minZ, g_lowZ = lowZ;
        ApplySpot();
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
        if (r.Ok() && d.type <= D_SCRAP && d.weapon <= 46) AddDrop(d);
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
        CVector at{v.x, v.y, v.z};
        bool same = g_box.placed == (placed != 0) && (g_box.pos - at).Length() < 0.05f && std::fabs(g_box.heading - heading) < 0.5f;
        bool shown = !g_box.placed || (g_box.handle >= 0 && GetObject(g_box.handle));
        if (same && shown) return;  // the host's word now and then (one who came back in has it): as it is
        g_box.placed = placed != 0;
        g_box.pos = at;
        g_box.heading = heading;
        g_box.rolling = g_box.offered = false;
        g_box.moveAt = 0;
        ShowBox();
        LOGI("Arena: the box %s", g_box.placed ? "shown" : "gone");
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
// The save disks are switched off, not taken away: each one "waiting to come back" (bDisabled) till a time that never
// comes, its object gone. They were made PICKUP_NONE before, and a free slot like that was given to the next pickup
// made (the arena's weapons): the disk's type put back after the arena landed on another pickup.
void HideSaves(bool hide) {
    constexpr uintptr_t kPickups = 0x9788C0;  // CPickups::aPickUps[620], 0x20 bytes each
    int16_t saveModel = Ref<int16_t>(0x8CD5E4);
    if (!hide) {
        for (const HiddenSave& h : g_hiddenSaves) {
            uintptr_t pk = kPickups + (uintptr_t)h.index * 0x20;
            if (Ref<int16_t>(pk + 0x18) != saveModel || Ref<uint8_t>(pk + 0x1C) == 0) continue;  // not that disk any more
            Ref<uint32_t>(pk + 0x0C) = h.regen;
            Ref<uint8_t>(pk + 0x1D) = (uint8_t)((Ref<uint8_t>(pk + 0x1D) & ~1u) | (h.flags & 1u));
        }
        g_hiddenSaves.clear();
        return;
    }
    if (saveModel <= 0) return;
    for (int i = 0; i < 620; ++i) {
        uintptr_t pk = kPickups + (uintptr_t)i * 0x20;
        if (Ref<int16_t>(pk + 0x18) != saveModel || Ref<uint8_t>(pk + 0x1C) == 0) continue;
        bool known = false;
        for (const HiddenSave& h : g_hiddenSaves) known |= h.index == i;
        if (known) continue;
        g_hiddenSaves.push_back({i, Ref<uint32_t>(pk + 0x0C), Ref<uint8_t>(pk + 0x1D)});
        Ref<uint32_t>(pk + 0x0C) = 0xFFFFFF00u;  // m_nRegenerationTime: never
        Ref<uint8_t>(pk + 0x1D) |= 1u;           // bDisabled
        CallMethod<0x454CF0>((void*)pk);         // CPickup::GetRidOfObjects
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

// ======================================================================= block 1: the arena's look and its helpers
// The help box (the game's own look: a dark box at the top left, white letters), one at a time; off with the hints
struct HelpBox {
    std::string text;
    uint64_t until = 0;
} g_helpBox;
void ShowHelp(const std::string& text, uint64_t ms = 7000) {
    if (!Cfg().tips) return;
    g_helpBox.text = text, g_helpBox.until = NowMs() + ms;
    CallMethod<0x506EA0>((void*)0xB6BC90, 1 /* a frontend blip */, 0.0f, 1.0f);
}
void DrawHelpBox(uint64_t now) {
    if (!Cfg().tips || now >= g_helpBox.until || g_helpBox.text.empty()) return;
    auto& g = ui::Gfx();
    float s = g.Scale();
    float w = 470 * s, pad = 14 * s, lh = g.LineHeight(ui::FONT_NORMAL);
    auto lines = g.Wrap(ui::FONT_NORMAL, ui::Widen(g_helpBox.text), w - pad * 2);
    float h = lh * (float)lines.size() + pad * 2;
    float x = (g.Width() - w) * 0.5f, y = g.Height() * 0.80f - h;  // the middle, low, over the subtitles
    g.Rect(x, y, w, h, 0xB4000000);
    for (size_t i = 0; i < lines.size(); ++i) g.TextW(ui::FONT_NORMAL, x + pad, y + pad + lh * (float)i, lines[i], 0xFFFFFFFF, ui::TEXT_SHADOW);
}
// the first melee weapon in hand (once a game): a word on the double money
void MeleeHint() {
    static bool told = false;
    CPlayerPed* me = FindPlayerPed(0);
    if (told || !me || !Cfg().tips || !arena::On()) return;
    int w = me->ActiveWeapon().type;
    if (w < 1 || w > 9) return;
    told = true;
    ShowHelp(TR("За убийство оружием ближнего боя вы получаете больше денег.", "A kill with a melee weapon pays more money."));
}
// the keys, PUBG's way: on the right, each key in its frame and what it does
void DrawKeys() {
    if (!Cfg().tips) return;
    struct K {
        const char* key;
        const char* what;
    };
    CPlayerPed* me = FindPlayerPed(0);
    std::vector<K> keys = {{"E", TR("Взаимодействие", "Use")}, {"H", TR("Баррикада (держать)", "Barricade (hold)")},
                           {"Y", TR("Пропустить паузу", "Skip the break")}, {"Tab", TR("Счёт", "Score")},
                           {"J", TR("Положить оружие", "Put the gun down")}, {"R", TR("Перезарядка", "Reload")}};
    if (me && me->ActiveWeapon().type == 9) keys.push_back({"N", TR("Рывок бензопилой", "Chainsaw dash")});
    else if (me && ((me->ActiveWeapon().type == 30 && g_hasUgl) || g_modUgl || g_modBayonet))
        keys.push_back({"N", g_modBayonet && !g_modUgl && !g_hasUgl ? TR("Штык-нож", "Bayonet")
                             : g_modBayonet                         ? TR("Подствольник / штык", "Launcher / bayonet")
                                                                    : TR("Подствольник", "Launcher")});
    if (g_gadget) keys.push_back({"X", g_gadget == 1 ? TR("Визор", "Visor") : g_gadget == 2 ? TR("Рейдж-доза", "Rage dose") : TR("Shahed", "Shahed")});
    auto& g = ui::Gfx();
    float s = g.Scale();
    float lh = g.LineHeight(ui::FONT_SMALL), right = g.Width() - 24 * s, y = g.Height() * 0.70f;
    for (const K& k : keys) {
        float kw = std::max(lh * 1.3f, g.TextWidth(ui::FONT_SMALL, k.key) + 12 * s), kh = lh + 6 * s;
        float kx = right - kw;
        g.Rect(kx, y, kw, kh, 0x90000000);
        g.RectOutline(kx, y, kw, kh, 0xD0FFFFFF, std::max(1.0f, s));
        g.Text(ui::FONT_SMALL, kx + kw * 0.5f, y + 3 * s, k.key, 0xFFFFFFFF, ui::TEXT_CENTER);
        g.Text(ui::FONT_SMALL, kx - 10 * s, y + 3 * s, k.what, 0xE0FFFFFF, ui::TEXT_RIGHT | ui::TEXT_OUTLINE);
        y += kh + 6 * s;
    }
}
// the waves to the next boss: a bar at the top, a cell a wave, the boss's name at its end
void DrawWaveBar(float y) {
    if (g_arena.endAt || !g_break) return;  // only between the waves
    int from = 0, to = 0;  // the cells: the ten waves up to the next boss
    if (Endless()) {
        int w = std::max(1, g_arena.wave + 1);
        to = ((w + 9) / 10) * 10, from = to - 10;
    } else {
        from = 0, to = g_arena.waves;
    }
    int n = to - from;
    if (n <= 0 || n > 12) return;
    auto& g = ui::Gfx();
    float s = g.Scale();
    float cw = 34 * s, gap = 4 * s, ch = 9 * s;
    std::string boss = g_nextBoss >= 0 ? BossName(g_nextBoss) : std::string("?");
    float bw = g.TextWidth(ui::FONT_SMALL, boss) + 16 * s;
    float total = (float)n * cw + (float)(n - 1) * gap + 10 * s + bw;
    float x = (g.Width() - total) * 0.5f;
    for (int i = 0; i < n; ++i) {
        int wave = from + i + 1;
        bool done = wave < g_arena.wave || (wave == g_arena.wave && g_break);
        bool cur = wave == g_arena.wave && !g_break;
        g.Rect(x, y, cw, ch, 0xA0000000);
        uint32_t c = done ? 0xFFACCBF1 : cur ? 0xFFFFD060 : 0x00000000;
        if (c) g.Rect(x + 1 * s, y + 1 * s, cw - 2 * s, ch - 2 * s, c);
        if (BossWave(wave)) g.RectOutline(x, y, cw, ch, 0xFFC8282D, std::max(1.0f, s) * 2.0f);
        else if (HardWave(wave)) g.RectOutline(x, y, cw, ch, 0xFFFF9020, std::max(1.0f, s) * 2.0f);
        x += cw + gap;
    }
    x += 10 * s - gap;
    g.Text(ui::FONT_SMALL, x, y + ch * 0.5f - g.LineHeight(ui::FONT_SMALL) * 0.5f, boss, 0xFFFF6050, ui::TEXT_OUTLINE);
}
// the arena's world stands still: midday and one weather (the weather let go after it); no radar
bool g_worldHeld = false;
void HoldWorld(bool on) {
    if (on) {
        Ref<uint8_t>(0xB70153) = 12, Ref<uint8_t>(0xB70152) = 0;  // CClock: 12:00
        Ref<uint32_t>(0xB70158) = Ref<uint32_t>(0xB7CB84);        // CClock::ms_nLastClockTick = CTimer::m_snTimeInMilliseconds: no minute passes
        if (!g_worldHeld) Cmd(0x01B6).I(0).Run();                 // FORCE_WEATHER_NOW: sunny
        Cmd(0x0581).I(0).Run();                                    // DISPLAY_RADAR off
        g_worldHeld = true;
    } else if (g_worldHeld) {
        g_worldHeld = false;
        Cmd(0x01B7).Run();       // RELEASE_WEATHER
        Cmd(0x0581).I(1).Run();  // DISPLAY_RADAR
    }
}
// ---- the skills box ($600, by the weapon box): one of CJ's own skills up by a random share
constexpr int kStatBoxCost = 300;
void ShowStatBox() {  // every game
    DelObj(g_statBox.handle);
    if (!g_box.placed) return;
    int model = PickModel({2912, 964, 1271});  // a wooden crate
    if (model < 0) return;
    float r = g_box.heading / 57.29578f;
    CVector right{std::cos(r), std::sin(r), 0.0f};
    CVector at = g_box.pos - right * 2.6f;
    if (g_ward.handle >= 0 && (g_ward.pos - at).Length2D() < 1.5f) at = g_box.pos + right * 5.2f;
    float z = 0;
    if (FloorNoBars(at.x, at.y, at.z + 1.2f, z) && std::fabs(z - g_box.pos.z) < 1.0f) at.z = z;
    float lift = 0, height = 0;
    g_statBox.pos = at;
    g_statBox.handle = MakeObj(model, at, g_box.heading, lift, height);
    if (g_statBox.handle >= 0) Cmd(0x01BC).I(g_statBox.handle).F(at.x).F(at.y).F(at.z + std::max(0.0f, lift - 0.45f)).Run();  // down onto the floor
    LOGI("Arena: the skills box %d (lift %.2f, height %.2f)", model, lift, height);
    Anchor(g_statBox.handle);
}
void RebuildBody();
void BuyStats() {
    if (Cash() < kStatBoxCost) {
        ui::TheChat().AddError(Format(TR("Нужно $%d.", "You need $%d."), kStatBoxCost));
        return;
    }
    AddPoints(-kStatBoxCost);
    static const int kSkillWeapon[] = {22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 34};  // stats 69..79
    int kind = RandInt(0, 3), pct = RandInt(5, 25);
    uint16_t stat = kind == 0 ? 21 : kind == 1 ? 23 : kind == 2 ? 22 : (uint16_t)(69 + RandInt(0, 10));
    float was = Call<0x558E40, float>(stat);                            // CStats::GetStatValue
    Call<0x55A070>(stat, std::min(1000.0f, was + 10.0f * (float)pct));  // CStats::SetStatValue: 1000 the most
    std::string what = kind == 0   ? Format(TR("Жир +%d%%", "Fat +%d%%"), pct)
                       : kind == 1 ? Format(TR("Мускулы +%d%%", "Muscle +%d%%"), pct)
                       : kind == 2 ? Format(TR("Выносливость +%d%%", "Stamina +%d%%"), pct)
                                   : Format(TR("Навык: %s +%d%%", "Skill: %s +%d%%"), WeaponName(kSkillWeapon[stat - 69]), pct);
    if (kind <= 1) RebuildBody();  // fat and muscle show on him
    g_popups.push_back({what, NowMs(), 0xFF80C0FF});
    ShowHelp(Format(TR("Ящик навыков: %s", "The skills box: %s"), what.c_str()), 5000);
    CallMethod<0x506EA0>((void*)0xB6BC90, 40 /* AE_FRONTEND_PURCHASE_WEAPON */, 0.0f, 1.0f);
}
// a bone's place now (CPed::m_apBones' frame in the skin's world matrices)
bool BoneAt(CPed* p, int node, CVector& at) {
    auto* clump = (uint8_t*)p->RwObject();
    void* nd = Field<void*>(p, 0x488 + node * 4);                                       // CPed::m_apBones
    void* hier = clump && clump[0] == 2 ? Call<0x734A40, void*>(clump) : nullptr;      // GetAnimHierarchyFromSkinClump
    int i = nd && hier ? Call<0x7C51A0, int>(hier, Field<int32_t>(nd, 0x14)) : -1;     // RpHAnimIDGetIndex
    auto* bones = hier ? Call<0x7C5120, uint8_t*>(hier) : nullptr;                     // RpHAnimHierarchyGetMatrixArray
    if (i < 0 || !bones) return false;
    at = ((const CMatrix*)(bones + (size_t)i * 0x40))->pos;
    return true;
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
    if (g_arena.taken) HoldWorld(true);
    MerchantFrame(now);
    GadgetFrame(now);
    GuardsFrame(now);
    MeleeHint();
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
    // the clothes' fast reload: the game's own (SET_PLAYER_FAST_RELOAD) only cut the wait for the clip, the reload
    // animation (standing, crouched) played on at its pace and the gun waited for it: it plays twice as fast
    if (HasPerk(O_RELOAD) && !g_spectating)
        if (CPlayerPed* me = FindPlayerPed(0); me && me->Health() > 0.0f)
            if (void* clump = me->RwObject())
                for (int anim : {226, 227})  // ANIM_ID_RELOAD, ANIM_ID_CROUCHRELOAD
                    if (void* assoc = Call<0x4D68B0, void*>(clump, (uint32_t)anim))  // RpAnimBlendClumpGetAssociation
                        Ref<float>((uintptr_t)assoc + 0x24) = 2.0f;                  // CAnimBlendAssociation::m_Speed
    Rescue(now);
    GuardCopies();
    DashFrame(now);
    HoloFrame(now);
    EBarFrame(now);
    if (!g_break && g_arena.left > 0 && g_arena.left <= 3) {
        if (!g_fewSince) g_fewSince = now;
    } else {
        g_fewSince = 0;
    }
    if (g_arena.host) KatanaFrame(now);
    SodaFrame(now);
    {  // no enemy drops its gun (the game's own pickups) in any game: the copies too (the host made its own so); the
       // flamethrower man, the Butcher and the bosses leave theirs as the arena's drops
        static uint64_t dropsMs = 0;
        if (now - dropsMs > 500) {
            dropsMs = now;
            CPoolHeader* pool = PedPool();
            for (int i = 0; pool && i < pool->size; ++i) {
                if (pool->byteMap[i] < 0) continue;
                auto* q = (CPed*)(pool->objects + (size_t)i * kPedPoolElemSize);
                if (q->IsPlayer() || q->Health() <= 0.0f || !arena::IsEnemy(q)) continue;
                Cmd(0x087E).I(GetPedRef(q)).I(0).Run();  // SET_CHAR_DROPS_WEAPONS_WHEN_DEAD: none
            }
            // and whatever the game dropped anyway (a copy's death in a partner's game, a SWAT man's): the weapon
            // pickups dead men leave (PICKUP_ONCE_TIMEOUT, _SLOW) in the place are switched off at once; the arena's
            // own (PICKUP_ONCE) stay
            const CVector mid = kPlaces[g_arena.place].pos;
            for (int i = 0; i < 620; ++i) {
                uintptr_t pk = 0x9788C0 + (uintptr_t)i * 0x20;  // CPickups::aPickUps
                uint8_t type = Ref<uint8_t>(pk + 0x1C);
                int16_t model = Ref<int16_t>(pk + 0x18);
                if ((type != 4 && type != 5) || (Ref<uint8_t>(pk + 0x1D) & 1u) || model < 321 || model > 372) continue;
                CVector at{Ref<int16_t>(pk + 0x10) / 8.0f, Ref<int16_t>(pk + 0x12) / 8.0f, Ref<int16_t>(pk + 0x14) / 8.0f};  // CompressedLargeVector
                if ((at - mid).Length() > 150.0f) continue;
                Ref<uint32_t>(pk + 0x0C) = 0xFFFFFF00u;  // m_nRegenerationTime: never
                Ref<uint8_t>(pk + 0x1D) |= 1u;           // bDisabled
                CallMethod<0x454CF0>((void*)pk);         // CPickup::GetRidOfObjects
                LOGI("Arena: a dropped weapon (model %d) taken away", model);
            }
        }
    }
    {  // the enemies' grenades: 1.5 s longer on the floor before they go off (time to run)
        static std::vector<std::pair<int, int32_t>> stretched;  // projectile slot, the destroy time it was given
        for (int i = 0; i < 32; ++i) {
            uintptr_t pi = 0xC891A8 + (uintptr_t)i * 0x24;  // gaProjectileInfo
            if (!Ref<uint8_t>(pi + 0x10) || Ref<uint32_t>(pi) != 16 /* WEAPON_GRENADE */) continue;
            auto* by = Ref<CEntity*>(pi + 0x04);
            if (!by || !by->IsPed() || ((CPed*)by)->IsPlayer() || !arena::IsEnemy((CPed*)by)) continue;
            int32_t& until = Ref<int32_t>(pi + 0x0C);
            bool done = false;
            for (auto& [slot, t] : stretched) done |= slot == i && t == until;
            if (done) continue;
            until += 1500;
            for (auto it = stretched.begin(); it != stretched.end();) it = it->first == i ? stretched.erase(it) : std::next(it);
            stretched.push_back({i, until});
        }
    }
    static uint64_t scoreMs = 0;  // the score now and then too: one that came in late has everyone's
    if (now - scoreMs > 3000) {
        scoreMs = now, SendScore();
        if (g_arena.host && g_box.placed && !g_box.moveAt) SendBox();  // the box for one who came back in
    }
    if (g_box.placed && !g_box.moveAt && arena::On() && (g_box.handle < 0 || !GetObject(g_box.handle))) {  // lost here: made again
        static uint64_t boxFixMs = 0;
        if (now - boxFixMs > 2000) boxFixMs = now, ShowBox();
    }
    if (g_arena.host) DronesFrame(now);
    else DroneCopies();
    if (g_arena.host) {  // an enemy gone off far (300 m and more from every player: fell out of the place, flung off): in again
        static uint64_t farCheckMs = 0;
        if (now - farCheckMs > 2000) {
            farCheckMs = now;
            std::vector<CVector> players = PlayerSpots();
            for (auto& [q, qe] : g_info) {
                if (!IsPedValid(q) || q->Health() <= 0.0f || q->InVehicle() || players.empty()) continue;
                float nearest = 1e9f;
                for (const CVector& pl : players) nearest = std::min(nearest, (q->GetPosition() - pl).Length());
                if (nearest < 300.0f) continue;
                CVector at = SpawnSpot(Rand(0.0f, 6.2831853f), 3.2f, 6.0f);
                Cmd(0x0687).I(GetPedRef(q)).Run();  // CLEAR_CHAR_TASKS
                q->MoveSpeed() = CVector{};
                SetEntityPosition(q, at);
                Cmd(0x0860).I(GetPedRef(q)).I(kPlaces[g_arena.place].area).Run();  // SET_CHAR_AREA_VISIBLE
                qe.routing = false, qe.setOnMs = 0, qe.lastMoveMs = now, qe.lastPos = at;
                LOGI("Arena: an enemy %.0f m off the players brought back in", nearest);
            }
        }
    }
    if (g_arena.host && g_minZ > -1e8f && GridUsable()) {  // an enemy that fell under the upper floors: back up, far from the players
        static uint64_t enemyCheckMs = 0;
        if (now - enemyCheckMs > 1000) {
            enemyCheckMs = now;
            std::vector<CVector> players = PlayerSpots();
            for (auto& [q, qe] : g_info) {
                if (!IsPedValid(q) || q->Health() <= 0.0f || q->InVehicle() || q->GetPosition().z >= g_minZ - 2.5f) continue;
                CVector best = g_spotAt;
                float most = -1;
                for (int k : StandCells()) {
                    float d = DistToPlayers(g_cells[(size_t)k].p, players) + Rand(0.0f, 4.0f);
                    if (d > most) most = d, best = g_cells[(size_t)k].p;
                }
                SetEntityPosition(q, best + CVector{0, 0, 1.0f});
                LOGI("Arena: an enemy under the upper floors put back up");
            }
        }
    }
    static uint64_t regenMs = 0;  // the clothes: 1 or 2 health a second
    if (int regen = PerkCount(O_REGEN1) + 2 * PerkCount(O_REGEN2); regen && now - regenMs > 1000 && !g_spectating) {
        regenMs = now;
        CPlayerPed* me = FindPlayerPed(0);
        float top = MaxHealth();
        if (me && me->Health() > 0.0f && me->Health() < top) me->Health() = std::min(top, me->Health() + (float)regen);
    }
    if (g_spectating) SpectateFrame(now);
    if (g_uglReadyMs && now >= g_uglReadyMs) g_uglReadyMs = 0, g_uglLoaded = true;
    TagCopies();
    RoarFrame(now);  // every game
    if (g_arena.host) {
        SawFrame(now);
        KnifeFrame();
    }
    CheckSmashed();
    FindNear();
    TwinCopies();
    for (auto it = g_popups.begin(); it != g_popups.end();) it = now - it->at > 4000 ? g_popups.erase(it) : std::next(it);
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
            AddPoints(-Cash() / 10);
        }
    }
    if (!g_arena.host) return;
    if (!g_gridDone && now - g_arena.startMs > 1500) GridStep();
    if (!g_placed && (g_gridDone || now - g_arena.startMs > 6000)) {
        g_placed = true;
        ClearBars();  // no barricades of the mod's own: the players build them (H), the host keeps its own (/arena баррикада)
        PlaceBox();
        bool own = LoadSpots();  // the host's own spots for this place (/arena ящик, баррикада)
        ShowProps();
        SendProps();
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
                 : qe.role == R_FLAMER || qe.role == R_BUILDER ? (qe.role == R_BUILDER ? 150 : 200) : qe.role == R_SNIPER ? 100
                 : qe.role == R_KNIFE || qe.role == R_MEDIC ? 80 : 50;
        cash += RandInt(0, 4) * 5;
        if (int8_t by = Field<int8_t>(q, 0x760); by >= 0 && by <= 15) cash *= 2;  // CPed::m_nLastWeaponDamage: a melee kill pays double
        SpawnDrop(D_CASH, at + CVector{Rand(-0.4f, 0.4f), Rand(-0.4f, 0.4f), 0}, 0, (uint16_t)cash);
        if (!g_drops.empty() && g_drops.back().type == D_CASH) {
            g_drops.back().from = q;
            if (auto mg = g_magnets.find(q); mg != g_magnets.end()) {
                uint8_t by = mg->second;
                g_magnets.erase(mg);
                TakeDrop(g_drops.back().id, by);
            }
        }
        bool special = qe.role == R_FLAMER || qe.role == R_TWIN || qe.role == R_BOSS || qe.role == R_SAW || qe.role == R_OFFICER;
        if (qe.role == R_MEDIC) special = RandInt(0, 99) < 35;  // a medic: now and then
        if (!special) {
            if (RandInt(0, 99) < 5) SpawnDrop(D_SMALL, at);  // a common enemy: now and then a few clips, never its gun
            if (!RcPlace() && RandInt(0, 99) < 10) SpawnDrop(D_SCRAP, at + CVector{0.5f, 0.3f, 0});  // no drones here: scrap off men
            continue;
        }
        if (qe.role == R_BOSS && !g_bossKatana) SpawnDrop(D_GUN, at + CVector{0, 0.7f, 0}, 30, 300);  // his AK (a plain one)
        if (qe.role == R_BOSS && g_bossKatana) SpawnDrop(D_GUN, at + CVector{0, 0.7f, 0}, 8, 1);  // the katana
        if (qe.role == R_SAW) SpawnDrop(D_GUN, at + CVector{0.7f, 0, 0}, 9, 1);  // the Butcher's chainsaw: a dash of its own
        if (qe.role == R_FLAMER) SpawnDrop(D_GUN, at + CVector{0, -0.7f, 0}, 37, 500);  // its flamethrower
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
    // (a wounded one lying there counts as out: everyone down at once ends it)
    if (CPlayerPed* me = FindPlayerPed(0))
        anyIn |= !g_spectating && me->Health() > 0.0f && me->AreaCode() == area && !Players::Instance().LocalDowned();
    for (auto& [id, rp] : Players::Instance().All())
        if (rp.hasState && !Spectator(id))
            anyIn |= !(rp.state.flags & (OnFootState::F_DEAD | OnFootState::F_DOWNED)) && rp.state.area == area && now - rp.stateRecvMs < 5000;
    if (!anyIn && now - g_arena.startMs > 8000) {
        if (!g_arena.outSince) g_arena.outSince = now;
        if (now - g_arena.outSince > 300) {  // at once (a moment for the states to come)
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
            if (g_arena.wave > 0) PickMvp();
            Finish(true);
            Send(A_END, [](Writer& w) { w.U8(1); });
            return;
        }
        if (!g_arena.nextWaveMs) {
            int secs = (int)(PrepMs(false) / 1000);
            g_arena.nextWaveMs = now + PrepMs(false);
            g_arena.itemsFor = g_arena.wave + 1;
            Send(A_PREP, [secs](Writer& w) { w.U8((uint8_t)secs); });
            if (g_arena.wave > 0) PickMvp();
        }
        if (now >= g_arena.nextWaveMs) {
            if (g_merchant.on) {
                EndMerchant();  // the trade is over: the next wave
            } else if (MerchantWave(g_arena.wave) && g_merchant.wave != g_arena.wave) {
                StartMerchant();  // nobody comes: the merchant does
                if (g_merchant.on) {
                    g_arena.nextWaveMs = now + 60000;
                    g_votes.clear(), g_voteCount = 0, g_iVoted = false;
                    SendVotes();
                    Send(A_PREP, [](Writer& w) { w.U8(60); });
                    SendMerchant(true, 0);
                    return;
                }
            }
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
    rows.push_back({Net().Online() ? Cfg().nickname : std::string(TR("Вы", "You")), {Cash(), g_kills, g_falls}, true, g_spectating});
    if (Net().Online())  // every player in the game, its score once it has sent one ($500 and nothing yet before that)
        for (auto& [id, peer] : Net().Peers()) {
            if (id == Net().MyId()) continue;
            auto it = g_scores.find(id);
            Score sc = it != g_scores.end() ? it->second : Score{kStartPoints, 0, 0};
            rows.push_back({Net().PeerName(id), sc, false, Spectator(id)});
        }
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
// ---- this player's own moves (every game)
// The katana's spin got to this player: half of all its health and armour (the armour first), never dead of it
void LoseHalf() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || me->Health() <= 0.0f || g_spectating) return;
    float total = me->Health() + me->Armour(), lose = total * 0.5f;
    float fromArmour = std::min(me->Armour(), lose);
    me->Armour() -= fromArmour;
    me->Health() = std::max(1.0f, me->Health() - (lose - fromArmour));
    Call<0x50A970>((void*)addr::TheCamera, 0.25f);  // CCamera::CamShakeNoPos
    ui::TheChat().AddSystem(TR("Катана босса: -50% здоровья и брони!", "The boss's katana: -50% health and armour!"));
}

// The chainsaw dash (the Butcher's saw in hand): the right button held shows the way (straight on where the camera
// looks, up to 14 m, to the first wall or edge), the left one then sends this player along it at 17 m/s; everyone of
// the arena's people it passes dies (the host's game does it: A_SAWHIT), the katana boss spinning is stunned instead.
// 3 s between two dashes.
constexpr int WEAPON_CHAINSAW = 9;
bool g_dashAim = false, g_dashing = false;
CVector g_dashDir, g_dashEnd;
float g_dashLeft = 0;
uint64_t g_dashReadyMs = 0;
int g_dashCharges = 0;  // the Butcher's chainsaw: 6 dashes in it (a new one picked up: 6 again)
std::set<CPed*> g_dashHit;
bool g_fireHeld = false;  // we hold the game's own fire back (the dash aim, the barricade's hologram)
uint8_t g_fireWas = 0;
void HoldFire(bool on) {
    CPad* pad = GetPad(0);
    if (on && !g_fireHeld) g_fireWas = pad->bDisablePlayerFireWeapon, pad->bDisablePlayerFireWeapon = 1, g_fireHeld = true;
    else if (!on && g_fireHeld) pad->bDisablePlayerFireWeapon = g_fireWas, g_fireHeld = false;
}
void SawKill(CPed* p) {  // host: the dash got to one of the arena's people
    auto it = g_info.find(p);
    if (it == g_info.end() || p->Health() <= 0.0f) return;
    if (it->second.role == R_BOSS && g_bossKatana && it->second.kStage == 2) return KatanaStun(p, it->second);
    Field<int8_t>(p, 0x760) = WEAPON_CHAINSAW;  // CPed::m_nLastWeaponDamage: a melee kill (double money)
    Cmd(0x0223).I(GetPedRef(p)).I(0).Run();     // SET_CHAR_HEALTH 0
}
bool DashFrom(const CVector& from, const CVector& dir, CVector& end) {  // the way: true when it goes anywhere
    CVector pos = from, hit, n;
    float went = 0;
    for (; went < 14.0f; went += 0.4f) {
        CVector next = pos + dir * 0.4f;
        float z = 0;
        if (SawRay(pos + CVector{0, 0, 0.2f}, next + dir * 0.3f + CVector{0, 0, 0.2f}, hit, n) ||
            SawRay(pos - CVector{0, 0, 0.5f}, next + dir * 0.3f - CVector{0, 0, 0.5f}, hit, n) ||
            !FloorBelow(next.x, next.y, next.z + 0.3f, z) || std::fabs(z + 1.0f - pos.z) > 1.2f)
            break;
        next.z = z + 1.0f;
        pos = next;
    }
    end = pos;
    return went >= 1.0f;
}
void DashFrame(uint64_t now) {
    CPlayerPed* me = FindPlayerPed(0);
    bool can = me && me->Health() > 0.0f && !me->InVehicle() && !g_spectating && !Players::Instance().LocalDowned() &&
               me->ActiveWeapon().type == WEAPON_CHAINSAW && IsForeground() && !MenuActive() && !ui::TheChat().IsOpen();
    float dt = std::min(0.1f, TimeStep() / 50.0f);
    if (g_dashing && (!me || me->Health() <= 0.0f || me->InVehicle() || g_spectating || Players::Instance().LocalDowned() ||
                      PlayerInfos()[0].playerState != 0)) {  // killed or downed mid-dash: no more moving him (it crashed)
        g_dashing = g_dashAim = false;
        HoldFire(false);
        return;
    }
    if (g_dashing && me) {
        CVector pos = me->GetPosition(), next = pos + g_dashDir * std::min(g_dashLeft, 17.0f * dt), hit, n;
        EBarDash(pos + g_dashDir * 0.9f);  // the builders' barricades: through them
        float z = 0;
        bool stop = g_dashLeft <= 0.05f || SawRay(pos + CVector{0, 0, 0.2f}, next + g_dashDir * 0.3f + CVector{0, 0, 0.2f}, hit, n) ||
                    !FloorBelow(next.x, next.y, next.z + 0.3f, z) || std::fabs(z + 1.0f - pos.z) > 1.2f;
        if (stop) {
            g_dashing = false;
            Cmd(0x0687).I(GetPedRef(me)).Run();
        } else {
            g_dashLeft -= (next - pos).Length();
            next.z = z + 1.0f;
            me->MoveSpeed() = CVector{};
            SetEntityPosition(me, next);
            CPoolHeader* pool = PedPool();
            for (int i = 0; pool && i < pool->size; ++i) {
                if (pool->byteMap[i] < 0) continue;
                auto* p = (CPed*)(pool->objects + (size_t)i * kPedPoolElemSize);
                if (p->IsPlayer() || p->Health() <= 0.0f || g_dashHit.count(p) || !arena::IsEnemy(p)) continue;
                CVector d = p->GetPosition() - next;
                if (d.Length2D() > 1.4f || std::fabs(d.z) > 1.6f) continue;
                g_dashHit.insert(p);
                if (g_arena.host) SawKill(p);
                else if (NetPed* np = Peds::Instance().Find(p)) {
                    uint16_t id = np->netId;
                    Send(A_SAWHIT, [id](Writer& w) { w.U16(id); });
                }
            }
        }
        HoldFire(true);
        return;
    }
    // N held: the aim (the way shown); let go: the dash
    bool held = can && (GetAsyncKeyState('N') & 0x8000);
    static bool heldWas = false;
    bool release = heldWas && !held && can && g_dashAim;
    heldWas = held;
    HoldFire(false);
    if (!held && !release) {
        g_dashAim = false;
        return;
    }
    CVector f = cam::Front();
    f.z = 0;
    float l = f.Length();
    g_dashDir = l > 0.01f ? f * (1.0f / l) : CVector{0, 1, 0};
    bool way = DashFrom(me->GetPosition(), g_dashDir, g_dashEnd) || (g_dashEnd - me->GetPosition()).Length() > 0.3f;
    g_dashAim = held;
    static uint64_t aimLogMs = 0;
    if (held && now - aimLogMs > 2000) aimLogMs = now, LOGI("Chainsaw dash aimed: %.1f m clear, ready %d", (g_dashEnd - me->GetPosition()).Length(), now >= g_dashReadyMs ? 1 : 0);
    if (release && g_dashCharges <= 0) {
        ui::TheChat().AddError(TR("В бензопиле больше нет рывков.", "No dashes left in the chainsaw."));
        return;
    }
    if (release && way && now >= g_dashReadyMs) {
        --g_dashCharges;
        LOGI("Chainsaw dash: %.1f m", (g_dashEnd - me->GetPosition()).Length());
        g_dashing = true;
        g_dashLeft = (g_dashEnd - me->GetPosition()).Length();
        g_dashReadyMs = now + 3000;
        g_dashHit.clear();
        int h = GetPedRef(me);
        Cmd(0x0173).I(h).F(std::atan2(-g_dashDir.x, g_dashDir.y) * 57.29578f).Run();
        PlayAnim(h, "sprint_civi", "PED", true);
    }
}
void DrawDash() {
    if (!g_dashAim || g_dashing) return;
    CPlayerPed* me = FindPlayerPed(0);
    if (!me) return;
    auto& g = ui::Gfx();
    float s = g.Scale();
    bool ready = NowMs() >= g_dashReadyMs;
    uint32_t c = ready ? 0xE0FF4020 : 0x80A0A0A0;
    CVector from = me->GetPosition() - CVector{0, 0, 0.9f}, to = g_dashEnd - CVector{0, 0, 0.9f};
    float px = 0, py = 0;
    bool prev = false;
    for (int k = 0; k <= 20; ++k) {
        float x, y, depth;
        bool on = WorldToScreen(from + (to - from) * ((float)k / 20.0f), x, y, depth) && depth > 0.2f;
        if (on && prev && k % 2 == 0) g.Line(px, py, x, y, 3.0f * s, c);
        prev = on, px = x, py = y;
    }
    float x, y, depth;
    if (WorldToScreen(to, x, y, depth) && depth > 0.2f) g.Ellipse(x, y, 7 * s, 4 * s, c);
    g.Text(ui::FONT_NORMAL, g.Width() * 0.5f, g.Height() * 0.66f, Format(TR("Рывков: %d", "Dashes: %d"), g_dashCharges),
           g_dashCharges > 0 ? 0xFFFFFFFF : 0xFFFF6060, ui::TEXT_CENTER | ui::TEXT_OUTLINE);
}

// The barricade's hologram: H pressed, a see-through fence in front of this player that only it sees, following its
// look; H pressed again (a tap), it is gone; H held 1.5 s where it can go (the bar fills), it is built there
bool g_holo = false, g_holoOk = false;
bool g_holoArmed = false;    // H let go since the hologram came up: the next press is a tap or a hold
uint64_t g_holoPress = 0;    // ms the current press lasts
int g_holoObj = -1;
CVector g_holoAt;
float g_holoHeading = 0;
uint64_t g_holoHeld = 0, g_holoLastMs = 0;  // ms held where it can go
constexpr uint64_t kHoloMs = 1500;
void HoloOff() {
    // its see-through look is the model's own (every fence of that model shares it): back to whole before it goes
    if (CObject* o = g_holoObj >= 0 ? GetObject(g_holoObj) : nullptr) CallMethod<0x5332C0>((void*)o, 255);
    DelObj(g_holoObj);
    g_holo = false;
    g_holoHeld = 0;
}
bool HoloKey() {  // H pressed
    if (g_holo) return true;
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || me->Health() <= 0.0f || me->InVehicle() || me->AreaCode() != kPlaces[g_arena.place].area || g_arena.endAt) return false;
    if ((int)g_bars.size() >= kMaxBars) {
        ui::TheChat().AddError(TR("Баррикад уже слишком много.", "There are too many barricades already."));
        return true;
    }
    if (Cash() < kBuildCost) {
        ui::TheChat().AddError(Format(TR("Нужно $%d.", "You need $%d."), kBuildCost));
        return true;
    }
    int model = PickModel({1418, 1408, 1424});
    float lift = 0, height = 0;
    if (model < 0) return true;
    g_holoObj = MakeObj(model, me->GetPosition(), 0.0f, lift, height);
    if (g_holoObj < 0) return true;
    Cmd(0x0382).I(g_holoObj).I(0).Run();  // SET_OBJECT_COLLISION: none
    g_holo = true;
    g_holoHeld = 0;
    g_holoArmed = false;
    g_holoPress = 0;
    g_holoLastMs = NowMs();
    return true;
}
void HoloFrame(uint64_t now) {
    if (!g_holo) return;
    CPlayerPed* me = FindPlayerPed(0);
    CObject* o = g_holoObj >= 0 ? GetObject(g_holoObj) : nullptr;
    bool held = IsForeground() && (GetAsyncKeyState('H') & 0x8000) && !ui::TheChat().IsOpen() && !MenuActive();
    if (!me || !o || me->Health() <= 0.0f || me->InVehicle() || g_spectating || g_arena.endAt ||
        me->AreaCode() != kPlaces[g_arena.place].area)
        return HoloOff();
    if (!held) {
        if (g_holoArmed && g_holoPress > 0 && g_holoPress < 300) return HoloOff();  // a tap: put away
        g_holoArmed = true;
        g_holoPress = 0;
        g_holoHeld = 0;
    }
    float lift = 0, height = 0;
    ColOf(o->ModelIndex(), lift, height);
    // the ghost out of the way of its own tests (its fence stood where the floor was looked for)
    o->SetPositionRaw(me->GetPosition() - CVector{0, 0, 60.0f});
    CVector f = cam::Front();
    f.z = 0;
    float l = f.Length();
    f = l > 0.01f ? f * (1.0f / l) : CVector{0, 1, 0};
    CVector front = me->GetPosition() + f * 1.6f;
    float z = 0;
    g_holoOk = FloorBelow(front.x, front.y, front.z + 0.5f, z) && std::fabs(z - (me->GetPosition().z - 1.0f)) < 1.0f && Clear(me->GetPosition(), front);
    if (g_holoOk) g_holoAt = {front.x, front.y, z};
    else g_holoAt = me->GetPosition() - CVector{0, 0, 1.0f} + f * 1.6f;
    g_holoHeading = std::atan2(-f.x, f.y) * 57.29578f;
    Cmd(0x01BC).I(g_holoObj).F(g_holoAt.x).F(g_holoAt.y).F(g_holoAt.z + lift).Run();  // SET_OBJECT_COORDINATES
    Cmd(0x0177).I(g_holoObj).F(g_holoHeading).Run();
    CallMethod<0x5332C0>((void*)o, g_holoOk ? 110 : 45);  // CEntity::SetRwObjectAlpha: see-through (fainter where it cannot go)
    uint64_t dt = std::min<uint64_t>(now - g_holoLastMs, 100);
    g_holoLastMs = now;
    bool building = held && g_holoArmed;
    if (building) g_holoPress += dt;
    g_holoHeld = building && g_holoOk ? g_holoHeld + dt : 0;
    if (g_holoHeld < kHoloMs) return;
    CVector at = g_holoAt;  // built
    float heading = g_holoHeading;
    HoloOff();
    if (Cash() < kBuildCost || (int)g_bars.size() >= kMaxBars) return;
    AddPoints(-kBuildCost);
    if (g_arena.host) BuildBar(at, heading, Net().MyId());
    else Send(A_BUILD, [&](Writer& w) {
        w.Vec({at.x, at.y, at.z});
        w.F32(heading);
    });
}
void DrawHolo(uint64_t now) {
    if (!g_holo) return;
    auto& g = ui::Gfx();
    float s = g.Scale(), w = 260 * s, h = 14 * s, x = (g.Width() - w) * 0.5f, y = g.Height() * 0.62f;
    float k = std::clamp((float)g_holoHeld / (float)kHoloMs, 0.0f, 1.0f);
    g.Rect(x, y, w, h, 0xA0000000);
    g.Rect(x, y, w * k, h, g_holoOk ? 0xE060D060 : 0xE0D06060);
    g.Text(ui::FONT_SMALL, g.Width() * 0.5f, y - g.LineHeight(ui::FONT_SMALL),
           !g_holoOk ? TR("Здесь не поставить", "It cannot go here")
           : g_holoHeld ? TR("Стройка баррикады...", "Building a barricade...")
                        : TR("Держите H: построить, нажмите H: убрать", "Hold H: build, press H: put away"), 0xFFFFFFFF,
           ui::TEXT_CENTER | ui::TEXT_OUTLINE);
}

void StopOwnMoves() {  // the arena is over: no dash, no hologram
    if (g_holo) HoloOff();
    g_dashing = g_dashAim = false;
    HoldFire(false);
}

// The last 3 enemies of a wave: after 6 s an arrow over each, seen through walls
uint64_t g_fewSince = 0;  // the last 3 enemies left since
void DrawArrows(uint64_t now) {
    if (!g_fewSince || now - g_fewSince < 6000) return;
    CPoolHeader* pool = PedPool();
    if (!pool) return;
    auto& g = ui::Gfx();
    float s = g.Scale(), bob = std::sin((float)(now % 1000) / 1000.0f * 6.2831853f) * 4 * s;
    int area = kPlaces[g_arena.place].area;
    for (int i = 0; i < pool->size; ++i) {
        if (pool->byteMap[i] < 0) continue;
        auto* p = (CPed*)(pool->objects + (size_t)i * kPedPoolElemSize);
        if (p->IsPlayer() || p->Health() <= 0.0f || p->AreaCode() != area || !arena::IsEnemy(p)) continue;
        float x, y, depth;
        if (!WorldToScreen(p->GetPosition() + CVector{0, 0, 1.4f}, x, y, depth) || depth < 0.2f) continue;
        y += bob;
        g.Tri(x - 11 * s, y - 16 * s, x + 11 * s, y - 16 * s, x, y, 0xFF000000);
        g.Tri(x - 8 * s, y - 14 * s, x + 8 * s, y - 14 * s, x, y - 3 * s, 0xFFFF3030);
    }
}

void DrawCod() {
    auto& g = ui::Gfx();
    float s = g.Scale();
    uint64_t now = NowMs();
    DrawDash();
    DrawHolo(now);
    DrawArrows(now);
    DrawMvp(now);
    DrawArmourBar();
    DrawGadgets(now);
    DrawShop();
    DrawHelpBox(now);
    DrawKeys();
    if (g_zoneNow >= 0)
        Label(g_zoneAt + CVector{0, 0, 1.3f}, Format(TR("ЗОНА %d", "ZONE %d"), g_zoneNow + 1), 0xFFFFA040, 300.0f, true);
    for (auto& [h, name] : g_guardNames)  // the homies' names
        if (CPed* gp = GetPed(h); gp && gp->Health() > 0.0f) Label(gp->GetPosition() + CVector{0, 0, 1.15f}, kGuardNames[name][Lang()], 0xFF80FF80, 30.0f);
    if (g_break && !g_arena.endAt && g_breakEndMs > now) {  // the break: one line on the right
        std::string t1 = Format(TR("Пауза %d с", "Break %d s"), (int)((g_breakEndMs - now + 999) / 1000));
        std::string t2 = Format(TR("[Y] пропустить %d/%d", "[Y] skip %d/%d"), g_voteCount, g_voteNeed);
        float lh = g.LineHeight(ui::FONT_NORMAL);
        float tw = std::max(g.TextWidth(ui::FONT_NORMAL, t1), g.TextWidth(ui::FONT_NORMAL, t2)) + 36 * s;
        float px = g.Width() - tw - 24 * s, py = g.Height() * 0.36f;
        g.Rect(px, py, tw, lh * 2 + 20 * s, 0xA0000000);
        g.Text(ui::FONT_NORMAL, px + tw * 0.5f, py + 8 * s, t1, 0xFFFFFFFF, ui::TEXT_CENTER | ui::TEXT_SHADOW);
        g.Text(ui::FONT_NORMAL, px + tw * 0.5f, py + 12 * s + lh, t2, 0xFFFFFFFF, ui::TEXT_CENTER | ui::TEXT_SHADOW);
    }
    float x = g.Width() - 40 * s, y = g.Height() * 0.24f;  // under the game's own money
    for (const Popup& p : g_popups) {
        float t = (float)(now - p.at) / 4000.0f;
        bool money = p.text.size() > 1 && (p.text[0] == '+' || p.text[0] == '-') && p.text[1] == '$';
        if (money)  // the money as the game's own: its Pricedown letters in its money green (red when spent)
            g.GameText(ui::GAME_FONT_PRICEDOWN, x, y + t * 30 * s, p.text, 34 * s, ui::WithAlpha(p.text[0] == '+' ? kGtaMoney : 0xFFB4191D, 1.0f - t), ui::TEXT_RIGHT);
        else
            g.Text(ui::FONT_NORMAL, x, y + t * 30 * s, p.text, ui::WithAlpha(p.color, 1.0f - t), ui::TEXT_RIGHT | ui::TEXT_OUTLINE);
    }
    y = g.Height() * 0.62f;
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
            Label(g_box.pos + CVector{0, 0, g_box.height + 0.5f}, Format("$%d", BoxCost()), 0xFF60D0FF, 8.0f);
        }
    }
    if (g_ward.handle >= 0) Label(g_ward.pos + CVector{0, 0, 1.2f}, Format("$%d", kWardCost), 0xFFE0A0FF, 8.0f);
    if (g_statBox.handle >= 0) Label(g_statBox.pos + CVector{0, 0, 1.2f}, Format("$%d", kStatBoxCost), 0xFF80C0FF, 8.0f);
    DrawScoreboard();
}

// the host starts one (the command, or the arena's game mode): every game hears of it
void Launch(int faction, int place, int waves) {
    if (g_lobby) OpenLobby(false);
    Send(A_START, [&](Writer& w) {
        w.U8((uint8_t)faction);
        w.U8((uint8_t)place);
        w.U8((uint8_t)waves);
        w.U8(g_modeOn);  // the mode: a partner that missed A_STAGE plays it too (from nothing)
        CVector sp;
        bool own = OwnSpawnOf(place, sp);
        w.U8(own);  // the players' own spot: everyone straight there, not first to the middle
        w.Vec({sp.x, sp.y, sp.z});
    });
    CVector sp;
    bool own = OwnSpawnOf(place, sp);
    Start(faction, place, waves, own ? &sp : nullptr);
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
        skin::Wear(0);
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
    if (InHouse() || arena::On()) return nullptr;  // none in CJ's house in the mode, none in an arena
    return g_origPlaceMarker(id, type, pos, size, r, g, b, a, pulse, frac, rot, normal, zCheck);
}
using PlaceConeFn = void(__cdecl*)(uint32_t, CVector*, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, float, int32_t, uint32_t);
PlaceConeFn g_origPlaceCone = nullptr;
void __cdecl HookPlaceCone(uint32_t id, CVector* pos, float size, uint32_t r, uint32_t g, uint32_t b, uint32_t a, uint32_t pulse, float frac,
                           int32_t rot, uint32_t coll) {
    if (InHouse() || arena::On()) return;
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

// The places' own music and crowds (the stadiums': the ambience track of the audio zone the camera is in) are off
// while this player is in the arena: the game finds no audio zone then, and its track fades out
using AmbienceFn = void(__fastcall*)(void*, void*);
AmbienceFn g_origAmbience = nullptr;
// The radio by the box: the game's own on-foot radio of a club's audio zone (zones 52..62 play a station), here the
// first audio box made that zone for this one update, so the game itself plays the chosen station (and stops it).
void __fastcall HookAmbience(void* self, void* edx) {
    if (!arena::On()) return g_origAmbience(self, edx);
    uint32_t& boxes = Ref<uint32_t>(0xB6DCBC);    // CAudioZones::m_NumActiveBoxes
    uint32_t& spheres = Ref<uint32_t>(0xB6DCC0);  // CAudioZones::m_NumActiveSpheres
    int32_t& active0 = Ref<int32_t>(0xB6DC6C);    // CAudioZones::m_aActiveBoxes[0]
    int16_t& zone0 = Ref<int16_t>(0xB6DCD0 + 8);  // CAudioZones::m_aBoxes[0].m_nAudioZone
    uint32_t b = boxes, sp = spheres;
    int32_t a0 = active0;
    int16_t z0 = zone0;
    spheres = 0;
    if (g_station >= 0 && Ref<uint32_t>(0xB6DCC4) > 0) boxes = 1, active0 = 0, zone0 = (int16_t)(52 + g_station);
    else boxes = 0;
    g_origAmbience(self, edx);
    boxes = b, spheres = sp, active0 = a0, zone0 = z0;
}

// The game raises a weapon's skill itself on hits (CStats::UpdateStatsWhenWeaponHit): in the arena only its kills
// do (GrowSkill), slowly
using WeaponHitStatsFn = void(__cdecl*)(int);
WeaponHitStatsFn g_origWeaponHitStats = nullptr;
// CHud::RenderArmorBar draws the bar in HUD_COLOUR_LIGHT_GRAY (CHudColours at 0xBAB22C, 4 bytes an entry): that colour
// is the armour's for the bar alone (the same grey is the game's text too: it is back right after)
using ArmorBarFn = void(__cdecl*)(int, int, int);
ArmorBarFn g_origArmorBar = nullptr;
void __cdecl HookRenderArmorBar(int playerId, int x, int y) {
    if (!g_armourKind || !g_arena.on) return g_origArmorBar(playerId, x, y);
    uint8_t* c = (uint8_t*)(0xBAB22C + 4 * 4);  // HUD_COLOUR_LIGHT_GRAY: r, g, b, a
    uint8_t was[4] = {c[0], c[1], c[2], c[3]};
    uint32_t argb = ArmourColour();
    c[0] = (uint8_t)(argb >> 16), c[1] = (uint8_t)(argb >> 8), c[2] = (uint8_t)argb;
    g_origArmorBar(playerId, x, y);
    memcpy(c, was, 4);
}
void __cdecl HookWeaponHitStats(int weapon) {
    if (g_arena.on) return;
    g_origWeaponHitStats(weapon);
}

class ArenaModule : public Module {
public:
    const char* Name() const override { return "Arena"; }
    void OnInstallHooks() override {
        Net().SetHandler(proto::G_ARENA, &Handle);
        mem::HookFn(0x55CEB0, &HookWeaponHitStats, &g_origWeaponHitStats, "CStats::UpdateStatsWhenWeaponHit");
        mem::HookFn(0x5890A0, &HookRenderArmorBar, &g_origArmorBar, "CHud::RenderArmorBar");
        mem::HookFn(0x4D6E60, &HookAmbience, &g_origAmbience, "CAEAmbienceTrackManager::UpdateAmbienceTrackAndVolume");
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
        g_blockers.clear();  // their objects, vehicles went with the game too
        g_guardGroups.clear();
        g_guardNames.clear();
        g_guardIds.clear();
        g_drones.clear();
        g_quads.clear();
        g_dronesLeft = 0;
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
        g_maxHealthBefore = 0, g_clothesSaved = false;
        for (int& w : g_worn) w = -1;
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
        RebuildWhenSafe();
        // the mouse as fast up and down as left and right (the game's vertical sensitivity was its own, slower)
        Ref<float>(0xB6EC18) = Ref<float>(0xB6EC1C);  // CCamera::m_fMouseAccelVertical = m_fMouseAccelHorzntl
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
        DrawWaveBar(y + 4 * s);
        y += 18 * s;
        if (BossWave(g_arena.wave) && g_arena.boss >= 0 && !g_arena.endAt && g_bossPed && IsPedValid(g_bossPed) && g_bossPed->Health() > 0.0f) {
            g.Text(ui::FONT_SMALL, g.Width() * 0.5f, y, Format("%s  %.0f", BossName(g_arena.boss).c_str(), g_bossPed->Health()), 0xFFFF7060,
                   ui::TEXT_CENTER | ui::TEXT_OUTLINE);
            y += g.LineHeight(ui::FONT_SMALL);
        }
        DrawCod();
    }
    bool OnCommand(const std::string& cmd, const std::vector<std::string>& args) override {
        std::string c = LowerRu(cmd);
        if (c == "торговец" || c == "setmerchant" || c == "merchant") {  // /торговец: where he comes in; /торговец цель: where he goes
            std::string a1 = args.empty() ? "" : LowerRu(args[0]);
            bool to = a1 == "цель" || a1 == "куда" || a1 == "to";
            bool reset = a1 == "сброс" || a1 == "reset";
            if (!g_arena.on || !g_arena.taken) ui::TheChat().AddError(TR("Сначала начните арену в этом месте.", "Start an arena at that place first."));
            else if (reset) SaveSpot("merchantreset"), SaveSpot("merchanttoreset");
            else SaveSpot(to ? "merchantto" : "merchant");
            return true;
        }
        if (c == "spawnmerchant" || c == "вызватьторговца") {  // the merchant now (a test; the host)
            if (!g_arena.on || !g_arena.host) ui::TheChat().AddError(TR("Только хост во время арены.", "Only the host, in an arena."));
            else if (!g_merchant.on) {
                StartMerchant();
                if (g_merchant.on && g_break && g_arena.nextWaveMs) g_arena.nextWaveMs = std::max(g_arena.nextWaveMs, NowMs() + 60000), Send(A_PREP, [](Writer& w) { w.U8(60); });
                SendMerchant(true, 0);
            }
            return true;
        }
        if (c == "подсказки" || c == "tips") {  // the arena's hints on and off (F9 too)
            Cfg().tips = !Cfg().tips;
            flow::SaveConnectSettings();
            ui::TheChat().AddSystem(Cfg().tips ? TR("Подсказки включены.", "Hints on.") : TR("Подсказки выключены.", "Hints off."));
            return true;
        }
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
        if (a0 == "где" || a0 == "pos" || a0 == "where") {  // where this player stands
            if (CPlayerPed* me = FindPlayerPed(0)) {
                CVector p = me->GetPosition();
                ui::TheChat().AddSystem(Format(TR("Вы стоите: %.2f %.2f %.2f, поворот %.1f, интерьер %d", "You stand at: %.2f %.2f %.2f, heading %.1f, interior %d"),
                                               p.x, p.y, p.z, me->CurrentRotation() * 57.29578f, (int)me->AreaCode()));
                LOGI("Position: %.2f %.2f %.2f, heading %.1f, interior %d", p.x, p.y, p.z, me->CurrentRotation() * 57.29578f, (int)me->AreaCode());
            }
            return true;
        }
        const char* spot = a0 == "ящик" || a0 == "box"               ? "box"
                           : a0 == "спавн" || a0 == "spawn"         ? "spawn"
                           : a0 == "торговец" || a0 == "merchant"   ? "merchant"
                           : a0 == "враги" || a0 == "enemy"         ? "enemy"
                           : a0 == "зона" || a0 == "zone"           ? "zone"
                           : a0 == "радио" || a0 == "radio"         ? "radio"
                           : a0 == "газировка" || a0 == "автомат" || a0 == "soda" ? "soda"
                           : a0 == "баррикада" || a0 == "barricade" ? "bar"
                           : a0 == "стройка" || a0 == "строитель" || a0 == "build" ? "build"
                           : a0 == "стена" || a0 == "wall"          ? "wall"
                           : a0 == "сброс" || a0 == "reset"         ? "reset"
                                                                    : nullptr;
        std::string what = spot ? spot : "";
        if (spot && what != "reset" && what != "wall" && args.size() > 1) {  // /arena газировка сброс: that kind alone
            std::string a1 = LowerRu(args[1]);
            if (a1 == "сброс" || a1 == "reset") what += "reset";
        }
        if (spot) {
            if (!g_arena.on || !g_arena.taken) ui::TheChat().AddError(TR("Сначала начните арену в этом месте.", "Start an arena at that place first."));
            else SaveSpot(what);
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

bool ShopOpen() { return g_shopOpen; }
int OutgoingDamage(CEntity* creator, int weapon, int damage) { return On() ? OutgoingDamageArena(weapon, damage) : damage; }
void BarricadeBlast(const CVector& at) {
    if (g_arena.on && g_arena.taken && !g_ebars.empty()) EBarBlast(at);
}
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
    int out = damage;
    if (weapon == WEAPON_FLAMETHROWER) out = std::max(1, damage * 35 / 100);  // the flamethrower's and the fire's damage
    // the Twin with two "eagles": two pistols at the pro skill, each shot as hard as an eagle's (70 to the pistol's 25)
    else if (weapon == WEAPON_PISTOL && creator && creator->IsPed() && IsEnemy((CPed*)creator) && ((CPed*)creator)->WeaponSkill() == 2)
        out = damage * 14 / 5;
    out = ArmourDamage(weapon, out);  // the merchant's armours
    if (out <= 0) return 0;
    // the clothes
    float f = 1.0f;
    for (int n = PerkCount(O_TOUGH); n > 0; --n) f *= 0.85f;
    for (int n = PerkCount(O_GREEDY); n > 0; --n) f *= 1.10f;
    if (CPlayerPed* me = FindPlayerPed(0); me && PerkCount(O_LASTSTAND) && me->Health() < MaxHealth() * 0.25f)
        for (int n = PerkCount(O_LASTSTAND); n > 0; --n) f *= 0.6f;
    if (weapon == WEAPON_FLAMETHROWER || weapon == WEAPON_MOLOTOV)
        for (int n = PerkCount(O_FIREPROOF); n > 0; --n) f *= 0.5f;
    if (weapon == WEAPON_GRENADE || weapon == 19 || weapon == 35 || weapon == 36 || weapon == 39 || weapon == 51)
        for (int n = PerkCount(O_BLASTPROOF); n > 0; --n) f *= 0.5f;
    if (f != 1.0f) out = std::max(1, (int)((float)out * f + 0.5f));
    return out;
}

// is it an enemy of the arena: the host's own, or a copy of one (an armed man of the arena's place, no player)
bool IsEnemy(CPed* p) {
    if (!g_arena.on || !p || p->IsPlayer() || Players::Instance().FindByPed(p)) return false;
    if (g_arena.host) return g_enemies.count(p) != 0;
    return p->AreaCode() == kPlaces[g_arena.place].area;
}

void OnLocalHit(CPed* victim, int weapon, int damage, int piece) {  // money now falls off the dead (D_CASH); the clothes' lifesteal
    if (!On() || !IsEnemy(victim) || damage <= 0) return;
    int n = PerkCount(O_LIFESTEAL);
    CPlayerPed* me = FindPlayerPed(0);
    if (!n || !me || me->Health() <= 0.0f || me->Health() >= MaxHealth()) return;
    static uint64_t lastMs = 0;  // a shotgun's pellets: one roll a shot
    uint64_t now = NowMs();
    if (now - lastMs < 120) return;
    lastMs = now;
    if (RandInt(0, 99) < 10 * n) me->Health() = std::min(MaxHealth(), me->Health() + 5.0f);
}

// Weapon skills grow with kills in the arena: the game's own skill stats (the pistol's, the AK's...: poor, gangster,
// hitman at 1000), +2.5 a kill, +4 a headshot (the game's own growth on hits is off there); a step up shows
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
    float now = std::min(1000.0f, was + (headshot ? 4.0f : 2.5f) * (float)(1 + PerkCount(O_SKILL)));  // a hitman after some 350-400 kills
    Call<0x55A070>((uint16_t)stat, now);                 // CStats::SetStatValue
    if ((was < 200.0f && now >= 200.0f) || (was < 999.0f && now >= 999.0f))
        g_popups.push_back({Format("%s: %s", WeaponName(weapon), SkillName(now)), NowMs(), 0xFF80C0FF});
}

void OnLocalKill(CPed* victim, int weapon, bool headshot) {
    if (!On() || !IsEnemy(victim) || g_killed.count(victim)) return;
    g_killed.insert(victim);
    ++g_kills;
    if (g_gadget == 1) g_visorCharge = std::min(100.0f, g_visorCharge + 3.0f);  // the visor: +3% a kill
    SendScore();  // at once: the host counts the wave's best player by it
    GrowSkill(weapon, headshot);
    if (CPlayerPed* me = FindPlayerPed(0); me && me->Health() > 0.0f) {  // the clothes: health, armour a kill
        if (int n = PerkCount(O_KILLHEAL)) me->Health() = std::min(MaxHealth(), me->Health() + 5.0f * (float)n);
        if (int n = PerkCount(O_KILLARMOUR)) me->Armour() = std::min(100.0f, me->Armour() + 5.0f * (float)n);
        if (int n = PerkCount(O_SCAVENGER); n && RandInt(0, 99) < 15 * n) {
            MoreAmmo(me, 0.35f);
            g_popups.push_back({TR("+обойма", "+clip"), NowMs(), 0xFF80C0FF});
        }
    }
    if (int bonus = 5 * PerkCount(O_BOUNTY) + 15 * PerkCount(O_GREEDY)) AddPoints(bonus);
    if (int n = PerkCount(O_MAGNET); n && RandInt(0, 99) < std::min(100, 25 * n)) {  // the kill's money straight in the pocket
        uint8_t me = Net().Online() ? Net().MyId() : 0;
        if (g_arena.host) Magnet(victim, me);
        else if (NetPed* np = Peds::Instance().Find(victim)) {
            uint16_t id = np->netId;
            Send(A_MAGNET, [id](Writer& w) { w.U16(id); });
        }
    }
    SendScore();
}

bool IsArenaEnemy(CPed* p) { return IsEnemy(p); }
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
    if (On() && vk == 'X' && g_gadget) {  // the gadget (the rage dose wounded on the floor too)
        if (!repeat) UseGadget();
        return true;
    }
    if (!On() || !ReviveText(hint).empty()) return false;
    if (g_shopOpen) return true;  // the merchant's screen reads its keys itself (DrawShop)
    if (vk == 'E' && NearMerchant()) {
        if (!repeat) SetShopOpen(true);
        return true;
    }
    if (vk == 'H') return repeat ? g_holo : HoloKey();  // held: the hologram (HoloFrame)
    if (vk == 'J') return !repeat && (PutDownGun(), true);
    if (vk == 'N') {  // T-Bone's launcher, the chainsaw's dash (DashFrame); without them N goes on (a side activity's "no")
        if (CPlayerPed* me = FindPlayerPed(0); me && me->ActiveWeapon().type == WEAPON_CHAINSAW) return true;
        if (!repeat && g_modBayonet && Bayonet()) return true;  // an enemy right before him: the bayonet
        if (!g_hasUgl && !g_modUgl) return g_modBayonet;
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
        if (Cash() < RepairCost()) {
            ui::TheChat().AddError(Format(TR("Нужно $%d.", "You need $%d."), RepairCost()));
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
    if (g_nearKind == N_STATS) {
        BuyStats();
        return true;
    }
    if (g_nearKind == N_RADIO) {  // the next station, then off
        g_station = g_station >= 10 ? -1 : g_station + 1;
        ui::TheChat().AddSystem(g_station < 0 ? std::string(TR("Радио выключено.", "The radio is off."))
                                              : Format(TR("Радио: %s", "Radio: %s"), kStations[g_station]));
        return true;
    }
    if (g_nearKind == N_SODA) {
        CPlayerPed* me = FindPlayerPed(0);
        float top = std::max<float>(PlayerInfos()[0].maxHealth, 100.0f);
        if (!me || me->Health() >= top) return true;
        if (int wait = SodaWait()) {
            ui::TheChat().AddError(Format(TR("Автомат: не больше 2 раз в минуту, ещё %d с.", "The machine: twice a minute at most, %d s more."), wait));
            return true;
        }
        if (Cash() < kSodaCost) {
            ui::TheChat().AddError(Format(TR("Нужно $%d.", "You need $%d."), kSodaCost));
            return true;
        }
        AddPoints(-kSodaCost);
        g_sodaUses.push_back(NowMs());
        // a drink as the game's own machines give it: turned to the machine, the can to the mouth; 20% of the health
        // back once it is drunk
        int h = GetPedRef(me);
        if (g_nearIdx >= 0 && g_nearIdx < (int)g_sodas.size()) {
            CVector to = g_sodas[(size_t)g_nearIdx].pos - me->GetPosition();
            Cmd(0x0173).I(h).F(std::atan2(-to.x, to.y) * 57.29578f).Run();  // SET_CHAR_HEADING
        }
        // as the game's own machines: the coin in (VEND_Use), the can out and drunk (VEND_Drink2_P), then the health
        if (EnsureBlock("VENDING")) {
            Cmd(0x0605).I(h).S("VEND_Use").S("VENDING").F(4.0f).I(0).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM
            g_sodaDrinkAt = NowMs() + 1200;
        }
        CallMethod<0x506EA0>((void*)0xB6BC90, 40 /* AE_FRONTEND_PURCHASE_WEAPON: the coin */, 0.0f, 1.0f);
        {
            CVector at = me->GetPosition();
            Cmd(0x018C).F(at.x).F(at.y).F(at.z).I(1054).Run();  // ADD_ONE_OFF_SOUND: SOUND_BUY (the coin in)
        }
        g_sodaHealAt = NowMs() + 2800;
        CallMethod<0x506EA0>((void*)0xB6BC90, 40 /* AE_FRONTEND_PURCHASE_WEAPON */, 0.0f, 1.0f);
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
        if (Cash() < BoxCost()) {
            ui::TheChat().AddError(Format(TR("Нужно $%d.", "You need $%d."), BoxCost()));
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
        hint = RepairCost() > 0 ? Format(TR("[E] Прибить доску  $%d", "[E] Nail a board  $%d"), RepairCost())
                                : std::string(TR("[E] Прибить доску", "[E] Nail a board"));
        return TR("Баррикада", "Barricade");
    }
    if (g_nearKind == N_WARD) {
        hint = Format(TR("[E] Случайная одежда  $%d", "[E] Random clothes  $%d"), kWardCost);
        return TR("Одежда", "Clothes");
    }
    if (g_nearKind == N_STATS) {
        hint = Format(TR("[E] Случайный навык  $%d", "[E] A random skill  $%d"), kStatBoxCost);
        return TR("Ящик навыков", "The skills box");
    }
    if (g_nearKind == N_RADIO) {
        hint = TR("[E] Другая станция", "[E] Next station");
        return g_station < 0 ? std::string(TR("Радио (выключено)", "Radio (off)")) : std::string(kStations[g_station]);
    }
    if (g_nearKind == N_SODA) {
        int wait = SodaWait();
        hint = wait ? Format(TR("Подождите %d с", "Wait %d s"), wait) : Format(TR("[E] Выпить  $%d", "[E] Drink  $%d"), kSodaCost);
        return TR("Газировка: +20% здоровья", "Soda: +20% health");
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
    hint = Format(TR("[E] Открыть  $%d", "[E] Open  $%d"), BoxCost());
    return TR("Ящик с оружием", "Weapon box");
}

}  // namespace arena

}  // namespace coop
