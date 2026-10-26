#include "features/cerberus/Cerberus.h"

#include <windows.h>

#include <string>

#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/lib/memory/Memory.h"

namespace coop::cerberus {

namespace {
using namespace game;

constexpr int kSlot = 8, kModel = 290 + kSlot - 1;  // the special characters' models: 290-299
constexpr const char* kImg = "coop\\cerberus.img";   // relative to the game's folder (the streamer opens it there)
int g_img = -1;     // our archive's index among the streamer's
bool g_ok = false;  // he is in slot 8 and has a character's skeleton

// CStreaming::InitImageList: the game's archives (gta3.img, gta_int.img), then ours
using InitListFn = void(__cdecl*)();
InitListFn g_origInitList = nullptr;
void __cdecl HookInitImageList() {
    g_origInitList();
    std::wstring path = Cfg().coopDir + L"cerberus.img";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        LOGW("Cerberus: coop\\cerberus.img is missing");
        return;
    }
    g_img = (int)Call<0x407610, uint32_t>(kImg, true);  // CStreaming::AddImageToList (not the player's: its directory is read)
    LOGI("Cerberus: coop\\cerberus.img is archive %d", g_img);
}

// CStreaming::LoadCdDirectory(file, archive): a model with no model info (cerberus.dff) goes to the extra objects'
// directory, but without its archive's number (the game took them all for gta3.img's): ours is put on it here
using LoadDirFn = void(__cdecl*)(const char*, int);
LoadDirFn g_origLoadDir = nullptr;
void __cdecl HookLoadCdDirectory(const char* file, int img) {
    g_origLoadDir(file, img);
    if (img != g_img || img < 0) return;
    void* dir = Ref<void*>(0x8E48D0);  // CStreaming::ms_pExtraObjectsDir
    auto* entry = dir ? CallMethod<0x532450, uint32_t*>(dir, "cerberus") : nullptr;  // CDirectory::FindItem: DirectoryInfo*
    if (!entry) return;
    *entry = (*entry & 0xFFFFFFu) | ((uint32_t)img << 24);  // DirectoryInfo::Pos: the offset, the archive in its top byte
    LOGI("Cerberus: his model is in archive %d", img);
}

void Load() {
    g_ok = false;
    void* dir = Ref<void*>(0x8E48D0);
    if (!dir || !CallMethod<0x532450, void*>(dir, "cerberus")) return;  // no cerberus.dff anywhere
    Call<0x409D10>(kModel, "cerberus", 2);  // CStreaming::RequestSpecialModel (STREAMING_GAME_REQUIRED)
    LoadAllRequestedModels(false);
    if (!IsModelLoaded(kModel)) {
        LOGW("Cerberus did not load");
        return;
    }
    // a model that is no character (no skin's bones) crashed the game as a ped was made of it
    void* info = Ref<void*>(0xA9B0C8 + (uintptr_t)kModel * 4);  // CModelInfo::ms_modelInfoPtrs
    void* clump = info ? Field<void*>(info, 0x1C) : nullptr;     // CBaseModelInfo::m_pRwObject
    if (!clump || !Call<0x734A40, void*>(clump)) {                // GetAnimHierarchyFromSkinClump
        LOGW("Cerberus: cerberus.dff has no character's skeleton: not used");
        return;
    }
    g_ok = true;
    LOGI("Cerberus is in slot %d (model %d)", kSlot, kModel);
}

class CerberusModule : public Module {
public:
    const char* Name() const override { return "Cerberus"; }
    void OnInstallHooks() override {
        mem::HookFn(0x4083C0, &HookInitImageList, &g_origInitList, "CStreaming::InitImageList");
        mem::HookFn(0x5B6170, &HookLoadCdDirectory, &g_origLoadDir, "CStreaming::LoadCdDirectory");
    }
    void OnGameStarted() override { Load(); }
    void OnGameShutdown() override { g_ok = false; }
};

CerberusModule g_module;
struct Reg {
    Reg() { core::RegisterModule(&g_module); }
} g_reg;
}  // namespace

int Model() { return g_ok && IsModelLoaded(kModel) ? kModel : -1; }

void Ensure() {
    if (!core::IsPlayerReady()) return;
    if (!g_ok || !IsModelLoaded(kModel)) Load();
}

}  // namespace coop::cerberus
