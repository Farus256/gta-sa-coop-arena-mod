#pragma once
// Throwing by hand with the arc shown (Throw.cpp), and the blasts of grenades and rockets kept behind walls.
namespace game {
struct CPed;
}

namespace coop {

// a blast hurting this ped now: true when the projectile that went off near it was on the other side of a wall (or
// a floor) from it, so it hurts nobody there
bool ExplosionBehindWall(game::CPed* victim);

}  // namespace coop
