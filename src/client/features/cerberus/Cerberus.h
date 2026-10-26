#pragma once
// Cerberus (Manhunt's special forces man): the mod brings his model itself (coop\cerberus.img, added to the game's
// archives as it starts) and keeps it in the special characters' slot 8 (model 297) of every game, so that model 297
// is Cerberus everywhere: the arena's police and SWAT knifemen, and a character a player can wear (the lobby).
namespace coop::cerberus {

int Model();   // 297 once he is in memory (and is a character), -1 without him
void Ensure(); // asks for him again if his slot lost him (a cutscene took it)

}  // namespace coop::cerberus
