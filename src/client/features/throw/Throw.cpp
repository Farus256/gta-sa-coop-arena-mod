// Throwing grenades, tear gas and molotovs by hand, with the arc shown: the fire button held with one of them in the
// hand draws where it will fly (a dotted arc from the hand to where it lands, a ring there), and the longer it is held
// the farther (up to 1.2 s); let go, the player throws it along that arc. Where the camera looks sets the direction
// and the height. The game's own throw is held back meanwhile (CPad::bDisablePlayerFireWeapon), the projectile is the
// game's own (CProjectileInfo::AddProjectile in the player's name, its speed set as it is made: Fire sends it to the
// other games on that arc), one of the weapon's ammo goes with it.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "features/throw/Throw.h"

#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "shared/game/Challenge.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"
#include "shared/ui/renderer/Renderer.h"

namespace coop {

namespace {
using namespace game;
using game::script::Cmd;

constexpr int WEAPON_GRENADE = 16, WEAPON_TEARGAS = 17, WEAPON_MOLOTOV = 18;
constexpr float kGravity = 20.0f;               // m/s/s: the game's 0.008 a frame squared
constexpr float kMinSpeed = 8.0f, kMaxSpeed = 24.0f;
constexpr uint64_t kFullMs = 1200;              // held this long: the farthest throw
constexpr uint64_t kReleaseDelayMs = 300;       // the arm comes over: the grenade leaves the hand then

bool Throwable(int w) { return w == WEAPON_GRENADE || w == WEAPON_TEARGAS || w == WEAPON_MOLOTOV; }
int ModelOf(int w) { return w == WEAPON_GRENADE ? 342 : w == WEAPON_TEARGAS ? 343 : 344; }

struct ColPoint {
    CVector point;
    uint8_t rest[0x2C - 12];
};
bool Ray(const CVector& a, const CVector& b, CVector& at) {
    ColPoint cp{};
    CEntity* hit = nullptr;
    bool struck = Call<0x56BA00, bool>(&a, &b, &cp, &hit, true, true, false, true, false, false, false, false);  // ProcessLineOfSight
    if (struck) at = cp.point;
    return struck;
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

bool g_holding = false;      // the button held with a throwable
uint64_t g_holdStart = 0;
uint64_t g_releaseAt = 0;    // thrown: it leaves the hand then
int g_weapon = 0;
CVector g_from, g_vel;       // the throw as it will leave (the last frame's aim)
bool g_disabled = false;     // we hold the game's own throw back
uint8_t g_disabledWas = 0;
std::vector<CVector> g_arc;  // where it flies (for the drawing)
CVector g_land;
bool g_landed = false;

bool CanThrow(CPlayerPed* me) {
    return me && me->Health() > 0.0f && !me->InVehicle() && !MenuActive() && !ui::TheChat().IsOpen() && IsForeground() &&
           !(GetPad(0)->DisablePlayerControls) && !cam::WideScreenOn() && Throwable(me->ActiveWeapon().type) &&
           me->ActiveWeapon().ammoTotal > 0;
}

// the hand, the direction the camera looks (a little up), the speed by how long it is held
void Aim(CPlayerPed* me, uint64_t now) {
    CVector f = cam::Front();
    float len = f.Length();
    f = len > 0.01f ? f * (1.0f / len) : CVector{0, 1, 0};
    CVector flat{f.x, f.y, 0.0f};
    float fl = flat.Length();
    flat = fl > 0.01f ? flat * (1.0f / fl) : CVector{0, 1, 0};
    CVector right{flat.y, -flat.x, 0.0f};
    g_from = me->GetPosition() + flat * 0.45f + right * 0.25f + CVector{0, 0, 0.75f};
    CVector dir = f + CVector{0, 0, 0.25f};
    dir = dir * (1.0f / std::max(0.01f, dir.Length()));
    float power = std::clamp((float)(now - g_holdStart) / (float)kFullMs, 0.0f, 1.0f);
    g_vel = dir * (kMinSpeed + (kMaxSpeed - kMinSpeed) * power);
    // the arc: steps of 1/30 s until it strikes something (3 s at most)
    g_arc.clear();
    g_landed = false;
    CVector pos = g_from, vel = g_vel;
    constexpr float dt = 1.0f / 30.0f;
    g_arc.push_back(pos);
    for (float t = 0; t < 3.0f; t += dt) {
        CVector prev = pos;
        vel.z -= kGravity * dt;
        pos = pos + vel * dt;
        CVector hit;
        if (Ray(prev, pos, hit)) {
            g_arc.push_back(hit);
            g_land = hit;
            g_landed = true;
            break;
        }
        g_arc.push_back(pos);
    }
}

void Release(CPlayerPed* me) {
    g_releaseAt = 0;
    CWeapon& w = me->ActiveWeapon();
    if (me->Health() <= 0.0f || w.type != g_weapon || w.ammoTotal <= 0 || !LoadModelNow(ModelOf(g_weapon))) return;
    CVector dir = g_vel * (1.0f / std::max(0.01f, g_vel.Length()));
    game::challenge::SetNextThrow(true, g_from, g_vel * (1.0f / 50.0f));  // metres a frame; Fire sets it on the projectile
    bool ok = Call<0x737C80, bool>((CEntity*)me, g_weapon, g_from, 1.0f, &dir, (CEntity*)nullptr);  // AddProjectile
    CVector f, sp;
    game::challenge::NextThrow(f, sp);  // left over (the hook did not take it): cleared
    if (!ok) return;
    --w.ammoTotal;
    w.ammoInClip = std::min(w.ammoInClip, w.ammoTotal);
    if (w.ammoTotal <= 0) Cmd(0x0555).I(GetPedRef(me)).I(g_weapon).Run();  // REMOVE_WEAPON_FROM_CHAR: the last one went
}

void HoldBack(bool on) {
    CPad* pad = GetPad(0);
    if (on && !g_disabled) {
        g_disabledWas = pad->bDisablePlayerFireWeapon;
        pad->bDisablePlayerFireWeapon = 1;
        g_disabled = true;
    } else if (!on && g_disabled) {
        pad->bDisablePlayerFireWeapon = g_disabledWas;
        g_disabled = false;
    }
}

void Update() {
    CPlayerPed* me = core::IsPlayerReady() ? FindPlayerPed(0) : nullptr;
    uint64_t now = NowMs();
    if (me && g_releaseAt && now >= g_releaseAt) Release(me);
    bool can = CanThrow(me);
    HoldBack(can || g_releaseAt);  // the game throws none of its own while one of ours is in the hand
    if (!can) {
        g_holding = false;
        g_arc.clear();
        return;
    }
    bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || GetPad(0)->NewState.ButtonCircle != 0;
    if (down && !g_holding && !g_releaseAt) {
        g_holding = true;
        g_holdStart = now;
        g_weapon = me->ActiveWeapon().type;
    }
    if (g_holding && down) Aim(me, now);
    if (g_holding && !down) {  // let go: the throw
        g_holding = false;
        Aim(me, now);
        g_arc.clear();
        int h = GetPedRef(me);
        CVector f = cam::Front();
        Cmd(0x0173).I(h).F(std::atan2(-f.x, f.y) * 57.29578f).Run();  // SET_CHAR_HEADING: where it goes
        if (EnsureBlock("GRENADE")) Cmd(0x0605).I(h).S("WEAPON_throw").S("GRENADE").F(8.0f).I(0).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM
        g_releaseAt = now + kReleaseDelayMs;
    }
}

void Draw() {
    if (g_arc.size() < 2 || MenuActive()) return;
    auto& g = ui::Gfx();
    float s = g.Scale();
    uint32_t color = g_weapon == WEAPON_MOLOTOV ? 0xE0FF8030 : g_weapon == WEAPON_TEARGAS ? 0xE0C0E0FF : 0xE0FFFFFF;
    float px = 0, py = 0;
    bool havePrev = false;
    for (size_t i = 0; i < g_arc.size(); ++i) {
        float x, y, depth;
        bool on = WorldToScreen(g_arc[i], x, y, depth);
        if (on && havePrev && (i % 2 == 0)) g.Line(px, py, x, y, std::max(2.0f, 3.0f * s), color);  // a dotted arc
        havePrev = on;
        px = x, py = y;
    }
    if (!g_landed) return;
    // the ring where it lands: a grenade's blast, a molotov's fire
    float r = g_weapon == WEAPON_MOLOTOV ? 2.0f : 3.0f;
    float lx = 0, ly = 0;
    bool lprev = false;
    for (int k = 0; k <= 24; ++k) {
        float a = 6.2831853f * (float)k / 24.0f, x, y, depth;
        bool on = WorldToScreen(g_land + CVector{std::cos(a) * r, std::sin(a) * r, 0.1f}, x, y, depth);
        if (on && lprev) g.Line(lx, ly, x, y, std::max(2.0f, 2.5f * s), color);
        lprev = on;
        lx = x, ly = y;
    }
}

// ---- blasts behind walls: where each grenade, rocket and satchel charge was last seen (kept a second after it is
// gone, it went off there); a blast hurting the player is dropped when the nearest of them within 12 m is behind a
// wall or a floor from both its chest and its head (the game's own blast went through walls)
constexpr uintptr_t kProjectileInfos = 0xC891A8;  // gaProjectileInfo[32], 0x24 bytes: m_eWeaponType +0, m_bActive +0x10
constexpr uintptr_t kProjectiles = 0xC89110;      // CProjectileInfo::ms_apProjectile[32]
struct Seen {
    CVector at;
    uint64_t ms = 0;
    bool live = false;
};
Seen g_seen[32];
void TrackProjectiles() {
    uint64_t now = NowMs();
    for (int i = 0; i < 32; ++i) {
        uintptr_t info = kProjectileInfos + (uintptr_t)i * 0x24;
        int type = Ref<int32_t>(info);
        bool blast = type == 16 || type == 19 || type == 20 || type == 39;  // grenade, rocket, heat-seeker, satchel
        auto* obj = Ref<CEntity*>(kProjectiles + (uintptr_t)i * 4);
        if (Ref<uint8_t>(info + 0x10) && blast && obj) {
            g_seen[i].at = obj->GetPosition();
            g_seen[i].ms = now;
            g_seen[i].live = true;
        } else {
            g_seen[i].live = false;
        }
    }
}
bool WallBetween(const CVector& a, const CVector& b) {
    return !Call<0x56A490, bool>(&a, &b, true, false, false, false, false, false, false);  // GetIsLineOfSightClear: buildings
}

class ThrowModule : public Module {
public:
    const char* Name() const override { return "Throw"; }
    void OnPreProcess() override {
        if (core::IsPlayerReady()) TrackProjectiles();
    }
    void OnPostProcess() override {
        Update();
        if (core::IsPlayerReady()) TrackProjectiles();
    }
    void OnRender() override { Draw(); }
    void OnGameShutdown() override {
        g_holding = false;
        g_releaseAt = 0;
        g_arc.clear();
        g_disabled = false;
    }
};

ThrowModule g_module;
struct Reg {
    Reg() { core::RegisterModule(&g_module); }
} g_reg;
}  // namespace

bool ExplosionBehindWall(CPed* victim) {
    uint64_t now = NowMs();
    CVector body = victim->GetPosition();
    const Seen* best = nullptr;
    float bestD = 12.0f;
    for (const Seen& s : g_seen) {
        if (!s.ms || now - s.ms > 1000) continue;
        float d = (s.at - body).Length();
        if (d < bestD) bestD = d, best = &s;
    }
    if (!best) return false;
    CVector from = best->at + CVector{0, 0, 0.3f};
    return WallBetween(from, body + CVector{0, 0, 0.3f}) && WallBetween(from, body + CVector{0, 0, 0.7f});
}

}  // namespace coop
