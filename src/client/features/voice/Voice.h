#pragma once
#include <cstdint>

namespace coop {

bool VoiceIsSpeaking(uint8_t playerId);  // a voice frame of this player was played in the last moment
bool VoiceIsTransmitting();              // the local microphone is being sent

}  // namespace coop
