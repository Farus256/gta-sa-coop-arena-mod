#pragma once
// What the other parts of the mod need of the ground battle (/arena): the zombies mode of Call of Duty brought into it
// (points, barricades, the mystery box, the perks, the power-ups), the revives without a limit there.
#include <string>

namespace game {
struct CPed;
}

namespace coop::arena {

bool On();                 // an arena is on and this player is in it
bool UnlimitedRevives();   // in the arena a wounded player can be revived any number of times
bool QuickRevive();        // this player drank Quick Revive: it gets up by itself, and revives the others twice as fast
void UseQuickRevive();     // it got up by itself: the perk is gone
float ReloadFactor();      // Speed Cola: reloads take this part of their time (1: no perk)

// this game's player hit a ped / killed one (points for the enemies of the arena)
void OnLocalHit(game::CPed* victim, int weapon, int damage, int piece);
void OnLocalKill(game::CPed* victim, int weapon, bool headshot);

// the interaction key (E): a barricade to repair, the mystery box, a perk machine; true when it was used here
bool KeyDown(int vk, bool repeat);
// the box in the middle of the screen: what can be done here ("E — починить баррикаду (+10)"); empty: nothing
std::string PromptText(std::string& hint);

}  // namespace coop::arena
