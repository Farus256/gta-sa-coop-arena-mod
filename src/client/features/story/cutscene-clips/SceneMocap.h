#pragma once
#include <cstddef>

namespace coop::scene {

// The partner listens in the host's cutscenes as their actors do: clips of CJ standing and listening (the weight on one
// leg, a hand to the chin, a look round), cut from the game's own cutscene animations (anim\cuts.img) into an animation
// block of ours. They are made on the player's machine from the game's own files: the mod ships no animation data.
constexpr const char* kMocapBlock = "COOPCS";
size_t MocapCount();              // the clips in the block (0 until it is made, or when it could not be)
const char* MocapClip(size_t i);  // an animation name in the block
// CJ's walk in the cutscenes, two steps on the spot that loop (the ped is moved along as fast as the person went)
size_t MocapWalkCount();
const char* MocapWalk(size_t i);
float MocapWalkSpeed(size_t i);   // m/s

}  // namespace coop::scene
