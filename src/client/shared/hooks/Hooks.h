#pragma once
// What one slice needs of another one beside it or above it (the arena of the damage, the revive of the arena): the
// app sets these at its start (app/core: WireHooks), so that no slice includes another's header. Each call does
// nothing (false, 0, the damage as it was) while it is not set.
#include <cstdint>
#include <string>

namespace game {
struct CPed;
struct CEntity;
struct CVector;
}

namespace coop::hooks {

struct Table {
    // the arena (features/arena)
    bool (*arenaOn)() = nullptr;
    bool (*arenaRunning)() = nullptr;
    bool (*arenaUnlimitedRevives)() = nullptr;
    bool (*arenaSpectating)() = nullptr;
    void (*arenaFall)() = nullptr;
    bool (*arenaQuickRevive)() = nullptr;
    bool (*arenaRageReady)() = nullptr;  // the rage dose can be taken now (it gets a wounded one up)
    void (*arenaUseQuickRevive)() = nullptr;
    void (*arenaOnLocalHit)(game::CPed*, int, int, int) = nullptr;
    void (*arenaOnLocalKill)(game::CPed*, int, bool) = nullptr;
    int (*arenaIncomingDamage)(game::CEntity*, int, int) = nullptr;
    int (*arenaOutgoingDamage)(game::CEntity*, int, int) = nullptr;
    bool (*arenaIsEnemy)(game::CPed*) = nullptr;
    void (*arenaBarricadeBlast)(const game::CVector&) = nullptr;
    // the wounds (features/revive)
    std::string (*reviveText)(std::string&) = nullptr;
    void (*knockDown)() = nullptr;
    void (*arenaWound)() = nullptr;
    void (*cancelDown)() = nullptr;
    // the throws (features/throw)
    bool (*explosionBehindWall)(game::CPed*) = nullptr;
    // the characters (features/progress)
    bool (*skinPicked)() = nullptr;
    uint16_t (*skinChosenModel)() = nullptr;
    void (*skinWear)(uint16_t) = nullptr;
};
inline Table& Get() {
    static Table t;
    return t;
}

inline bool ArenaOn() { return Get().arenaOn && Get().arenaOn(); }
inline bool ArenaRunning() { return Get().arenaRunning && Get().arenaRunning(); }
inline bool ArenaUnlimitedRevives() { return Get().arenaUnlimitedRevives && Get().arenaUnlimitedRevives(); }
inline bool ArenaSpectating() { return Get().arenaSpectating && Get().arenaSpectating(); }
inline void ArenaFall() { if (Get().arenaFall) Get().arenaFall(); }
inline bool ArenaQuickRevive() { return Get().arenaQuickRevive && Get().arenaQuickRevive(); }
inline bool ArenaRageReady() { return Get().arenaRageReady && Get().arenaRageReady(); }
inline void ArenaUseQuickRevive() { if (Get().arenaUseQuickRevive) Get().arenaUseQuickRevive(); }
inline void ArenaOnLocalHit(game::CPed* v, int weapon, int damage, int piece) { if (Get().arenaOnLocalHit) Get().arenaOnLocalHit(v, weapon, damage, piece); }
inline void ArenaOnLocalKill(game::CPed* v, int weapon, bool headshot) { if (Get().arenaOnLocalKill) Get().arenaOnLocalKill(v, weapon, headshot); }
inline int ArenaIncomingDamage(game::CEntity* c, int weapon, int damage) { return Get().arenaIncomingDamage ? Get().arenaIncomingDamage(c, weapon, damage) : damage; }
inline int ArenaOutgoingDamage(game::CEntity* c, int weapon, int damage) { return Get().arenaOutgoingDamage ? Get().arenaOutgoingDamage(c, weapon, damage) : damage; }
inline bool ArenaIsEnemy(game::CPed* p) { return Get().arenaIsEnemy && Get().arenaIsEnemy(p); }
inline void ArenaBarricadeBlast(const game::CVector& at) { if (Get().arenaBarricadeBlast) Get().arenaBarricadeBlast(at); }
inline std::string ReviveText(std::string& hint) { return Get().reviveText ? Get().reviveText(hint) : std::string(); }
inline void KnockDown() { if (Get().knockDown) Get().knockDown(); }
inline void ArenaWound() { if (Get().arenaWound) Get().arenaWound(); }
inline void CancelDown() { if (Get().cancelDown) Get().cancelDown(); }
inline bool ExplosionBehindWall(game::CPed* v) { return Get().explosionBehindWall && Get().explosionBehindWall(v); }
inline bool SkinPicked() { return Get().skinPicked && Get().skinPicked(); }
inline uint16_t SkinChosenModel() { return Get().skinChosenModel ? Get().skinChosenModel() : 0; }
inline void SkinWear(uint16_t model) { if (Get().skinWear) Get().skinWear(model); }

}  // namespace coop::hooks
