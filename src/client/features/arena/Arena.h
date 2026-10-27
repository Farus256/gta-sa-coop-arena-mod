#pragma once
// What the other parts of the mod need of the ground battle (/arena): the zombies mode of Call of Duty brought into it
// (points, barricades, the mystery box, the perks, the power-ups), the revives without a limit there.
#include <string>

namespace game {
struct CPed;
struct CEntity;
struct CVector;
}

namespace coop::arena {

bool On();                 // an arena is on and this player is in it
bool UnlimitedRevives();   // in the arena a wounded player can be revived any number of times
bool Spectating();         // this player fell in the arena and watches a team mate till the next wave
void Fall();               // ... it falls now (instead of dying: no hospital)
bool QuickRevive();        // this player drank Quick Revive: it gets up by itself, and revives the others twice as fast
void UseQuickRevive();     // it got up by itself: the perk is gone
float ReloadFactor();      // Speed Cola: reloads take this part of their time (1: no perk)

// this game's player hit a ped / killed one (points for the enemies of the arena)
void OnLocalHit(game::CPed* victim, int weapon, int damage, int piece);
void OnLocalKill(game::CPed* victim, int weapon, bool headshot);

// the damage that comes to this player: the flamethrower's fire weaker in the arena
int IncomingDamage(game::CEntity* creator, int weapon, int damage);

// the interaction key (E; B builds a barricade): a barricade to repair, the mystery box, a perk machine; true when it was used here
bool KeyDown(int vk, bool repeat);
// the arena's game mode (F9: "Режим: Арена"): its choices for the menu; after a lost arena everyone is in its lobby
// (the F9 window): the host starts the next one from there
bool InLobby();
void LobbyStart();  // host
bool Running();     // an arena is on now
bool IsArenaEnemy(game::CPed* p);
void BarricadeBlast(const game::CVector& at);
// host: a hit on one of the arena's men that its gear takes (a shield man's shield from the front): true, no damage
bool EnemyHitBlocked(game::CPed* victim, game::CEntity* by, int weapon);  // a blast of this game's: the builders' barricades near it break  // one of the arena's enemies (the host's, or a copy)
void StartNow();    // host: the next arena now (F9)
int FactionCount();
std::string FactionTitle(int i);
int PlaceCount();
std::string PlaceTitle(int i);  // -1: any

// the box in the middle of the screen: what can be done here ("E — починить баррикаду (+10)"); empty: nothing
std::string PromptText(std::string& hint);

}  // namespace coop::arena
