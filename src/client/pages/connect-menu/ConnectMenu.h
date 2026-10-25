#pragma once

namespace coop::ui {

// In-game "SA:COOP" window (F9 / shown automatically in the main menu): nickname, address,
// port, password, "Connect" and "Create server" (with save slot selection).
void SetConnectMenuVisible(bool visible);
bool IsConnectMenuVisible();
void ProcessConnectMenu();  // per frame, before rendering
void RenderConnectMenu();

}  // namespace coop::ui
