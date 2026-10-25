// Startup diagnostics (enabled with [debug] verbose_log = 1): logs skeleton events and the result
// of the critical initialisation steps, which makes "the game silently quits" problems traceable.
#include <windows.h>

#include "app/crash-handler/CrashHandler.h"
#include "common/lib/log/Log.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"
#include "shared/lib/memory/Memory.h"

namespace coop {

namespace {
using EventFn = int(__cdecl*)(int, void*);
EventFn g_origEvent = nullptr;
int g_eventLogBudget = 400;
int g_lastState = -1;

int __cdecl HookRsEventHandler(int ev, void* param) {
    Heartbeat();
    int st = game::GameState();
    if (st != g_lastState) {
        LOGD("gGameState %d -> %d", g_lastState, st);
        g_lastState = st;
    }
    int r = g_origEvent(ev, param);
    // rsIDLE (26) and rsFRONTENDIDLE (27) fire every frame; log only the rare events
    if (g_eventLogBudget > 0 && ev != 26 && ev != 27) {
        --g_eventLogBudget;
        LOGD("RsEventHandler(%d) -> %d (quit=%d)", ev, r, game::Ref<uint8_t>(0xC17050));
    }
    return r;
}

using BoolFn = bool(__cdecl*)();
BoolFn g_origEssentials = nullptr;
bool __cdecl HookInitialiseEssentialsAfterRW() {
    bool r = g_origEssentials();
    LOGI("CGame::InitialiseEssentialsAfterRW -> %d", r);
    return r;
}

class DiagModule : public Module {
public:
    const char* Name() const override { return "Diagnostics"; }
    void OnInstallHooks() override {
        if (!Cfg().verboseLog) return;
        mem::HookFn(0x619B60, &HookRsEventHandler, &g_origEvent, "RsEventHandler");
        mem::HookFn(0x5BA160, &HookInitialiseEssentialsAfterRW, &g_origEssentials, "CGame::InitialiseEssentialsAfterRW");
    }
};

DiagModule g_diag;
struct Reg {
    Reg() { core::RegisterModule(&g_diag); }
} g_reg;
}  // namespace

void LogShutdownState() {
    LOGI("Process detach: gGameState=%d RsGlobal.quit=%d", game::GameState(), game::Ref<uint8_t>(0xC17050));
}

}  // namespace coop
