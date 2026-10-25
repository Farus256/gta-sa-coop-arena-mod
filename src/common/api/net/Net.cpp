#include "common/api/net/Net.h"

#include <atomic>

namespace coop::net {

static std::atomic<int> g_refs{0};

bool Init() {
    if (g_refs++ == 0) {
        if (enet_initialize() != 0) {
            g_refs--;
            return false;
        }
    }
    return true;
}

void Shutdown() {
    if (--g_refs == 0) enet_deinitialize();
}

uint32_t FlagsForChannel(uint8_t channel) {
    switch (channel) {
        case proto::CH_RELIABLE:
        case proto::CH_BULK:
            return ENET_PACKET_FLAG_RELIABLE;
        case proto::CH_VOICE:
            return ENET_PACKET_FLAG_UNSEQUENCED;
        default:
            return 0;  // unreliable, sequenced
    }
}

}  // namespace coop::net
