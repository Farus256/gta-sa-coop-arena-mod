#include "features/progress/SkinChoice.h"

#include <cstring>
#include <iterator>

#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/lib/memory/Memory.h"
#include "shared/ui/chat/Chat.h"

namespace coop::skin {

namespace {
using namespace game;
using game::script::Cmd;

// one card a kind; its looks (the game's models of that kind) one after another on a second click
Choice kChoices[] = {
    {0, TRP("Карл Джонсон (CJ)", "Carl Johnson (CJ)"), 0xFFF2F2F2, 0xFF3A5A8C, 0xFF151515, TOP_TANK, HEAD_HAIR, 0xFF1A1A1A},
    {105, TRP("Грув-стрит (fam)", "Grove Street (fam)"), 0xFF2E8B3E, 0xFF2B2B2B, 0xFF2E8B3E, TOP_TEE, HEAD_CAP, 0xFFE8E8E8, {105, 106, 107}, 3},
    {102, TRP("Баллас (ballas)", "Ballas"), 0xFF7A3FA0, 0xFF2B2B2B, 0xFF5A2A7A, TOP_TEE, HEAD_CAP, 0xFFE8E8E8, {102, 103, 104}, 3},
    {108, TRP("Вагос (vag)", "Vagos (vag)"), 0xFFE8C030, 0xFF3A3A3A, 0xFFE8C030, TOP_TEE, HEAD_BANDANA, 0xFF202020, {108, 109, 110}, 3},
    {114, TRP("Ацтекас (vla)", "Aztecas (vla)"), 0xFF30B8C8, 0xFF2B3A5A, 0xFF30B8C8, TOP_TANK, HEAD_BANDANA, 0xFF202020, {114, 115, 116}, 3},
    {280, TRP("Полицейский", "Police officer"), 0xFF2A3A6A, 0xFF1A2040, 0xFF1A2040, TOP_LONG, HEAD_CAP, 0xFF101010, {280, 281, 282}, 3},
    {285, TRP("Спецназ / ФБР", "SWAT / FBI"), 0xFF202428, 0xFF202428, 0xFF202428, TOP_LONG, HEAD_CAP, 0xFF101010, {285, 286}, 2},
    {287, TRP("Солдат", "Soldier"), 0xFF4A5A30, 0xFF4A5A30, 0xFF3A4A28, TOP_LONG, HEAD_CAP, 0xFF2A2018},
};
bool g_profileKnown = false, g_newcomer = false, g_picked = false;
int g_choice = 0;
uint64_t g_shopNoteMs = 0;

// ---- no clothes on another body
using RebuildFn = void(__cdecl*)(CPlayerPed*, bool);
RebuildFn g_origRebuild = nullptr;
void __cdecl HookRebuildPlayer(CPlayerPed* ped, bool keep) {
    if (ped && ped == FindPlayerPed(0) && ped->ModelIndex() != 0) return;  // a Families member wears what he wears
    g_origRebuild(ped, keep);
    LastClothesRebuildMs() = NowMs();
}

bool ClothesPlace(const char* name) {
    static const char* kPrefixes[] = {"CS", "LACS", "CLOTH", "BARB", "TATT"};  // the shops' interiors (CSCHP, LACS1...)
    for (const char* p : kPrefixes)
        if (!_strnicmp(name, p, strlen(p))) return true;
    return false;
}

using TransitionFn = bool(__fastcall*)(void*, void*, CPed*);
TransitionFn g_origTransition = nullptr;
bool __fastcall HookTransitionStarted(void* enex, void* edx, CPed* ped) {
    if (ped && ped == FindPlayerPed(0) && ped->ModelIndex() != 0 && ped->AreaCode() == 0) {
        char name[9] = {};
        memcpy(name, enex, 8);  // CEntryExit::m_szName
        if (ClothesPlace(name)) {
            if (NowMs() - g_shopNoteMs > 5000) {
                g_shopNoteMs = NowMs();
                ui::TheChat().AddSystem(TR("Сюда может зайти только CJ: другой персонаж не переодевается.",
                                           "Only CJ can go in here: another character does not change clothes."));
            }
            return false;
        }
    }
    return g_origTransition(enex, edx, ped);
}

class SkinChoice : public Module {
public:
    const char* Name() const override { return "SkinChoice"; }
    void OnInstallHooks() override {
        mem::HookFn(0x5A82C0, &HookRebuildPlayer, &g_origRebuild, "CClothes::RebuildPlayer");
        mem::HookFn(0x43FFD0, &HookTransitionStarted, &g_origTransition, "CEntryExit::TransitionStarted");
    }
    void OnSessionEnd() override {
        g_profileKnown = g_newcomer = g_picked = false;
        g_choice = 0;
    }
};

SkinChoice g_module;
struct Reg {
    Reg() { core::RegisterModule(&g_module); }
} g_reg;
}  // namespace

const Choice* Choices(int& count) {
    count = (int)std::size(kChoices);
    return kChoices;
}

void OnProfile(bool empty, uint16_t worn) {
    if (!g_profileKnown || empty) g_newcomer = empty;  // the first one of the session says; an empty one later: a new game
    g_profileKnown = true;
    if (!g_picked)  // the lobby shows the one it has
        for (int i = 0; i < (int)std::size(kChoices); ++i) {
            Choice& c = kChoices[i];
            uint16_t want = empty ? 0 : worn;
            if (c.model == want) g_choice = i;
            for (uint8_t v = 0; v < c.variants; ++v)
                if (c.alt[v] && c.alt[v] == want) g_choice = i, c.variant = v, c.model = want;
        }
}

bool Newcomer() { return g_newcomer; }

void SetChoice(int index) {
    if (index >= 0 && index < (int)std::size(kChoices)) g_choice = index, g_picked = true;
}

void NextVariant(int index) {
    if (index < 0 || index >= (int)std::size(kChoices)) return;
    Choice& c = kChoices[index];
    if (c.variants > 1) c.variant = (uint8_t)((c.variant + 1) % c.variants), c.model = c.alt[c.variant];
    g_choice = index, g_picked = true;
}

bool Picked() { return g_picked; }

int ChosenIndex() { return g_choice; }

void ApplyChoice() {
    const Choice& c = kChoices[g_choice];
    Wear(c.model);
    ui::TheChat().AddSystem(Format(TR("Вы играете за: %s.", "You play as: %s."), TR(c.name.ru, c.name.en)));
    if (c.model)
        ui::TheChat().AddSystem(TR("Магазины одежды, парикмахерские и тату-салоны открыты только для CJ.",
                                   "Clothes shops, barbers and tattoo parlours are for CJ only."));
    g_newcomer = g_picked = false;
    LOGI("Character from the lobby: model %d", c.model);
}

void Wear(uint16_t model) {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || me->InVehicle() || me->ModelIndex() == model) return;
    if (model && !LoadModelNow(model)) return;
    Cmd(0x09C7).I(0).I(model).Run();        // SET_PLAYER_MODEL
    if (!model) Call<0x5A82C0>(me, false);  // CClothes::RebuildPlayer: CJ in his own clothes again
    if (model) MarkModelNotNeeded(model);   // the player holds it now
    LOGI("Player model %d", model);
}

uint16_t Worn() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me) return 0;
    uint16_t m = me->ModelIndex();
    for (const Choice& c : kChoices) {
        if (c.model == m) return m;
        for (uint8_t v = 0; v < c.variants; ++v)
            if (c.alt[v] == m) return m;
    }
    return 0;
}

}  // namespace coop::skin
