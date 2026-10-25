// coop.dll / coop.asi — SA:COOP game client.
// Loaded either by the ASI loader of the game (coop.asi in the game folder) or injected by the launcher.
#include <windows.h>

#include "shared/lib/core/Core.h"

namespace coop {
void LogShutdownState();
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        coop::core::Init(hinst);
    } else if (reason == DLL_PROCESS_DETACH) {
        coop::LogShutdownState();
    }
    return TRUE;
}
