// Cutscene clips for the partner (see SceneMocap.h). The cutscene animations are ANPK files in anim\cuts.img, one
// animation per person with the bones tagged as on any ped (a face rig of its own on top), keyed in seconds of the
// cutscene. A clip is a stretch of one where the person stands and listens; it starts at 0, keeps the body bones only,
// and its root stays on the spot, turned to face the ped's forward as the game's own ped animations do. The clips go
// into one ANPK in memory (made on a thread of ours: the files are tens of MB), and CAnimManager::LoadAnimFile makes
// our block of it as it does a cutscene's.
#include "features/story/cutscene-clips/SceneMocap.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"

namespace coop::scene {

namespace {
using namespace game;

struct ClipDef {
    const char* cutscene;
    const char* actor;  // the person's animation in the cutscene's IFP
    float from, to;     // seconds of the cutscene
};
// CJ standing silent in them: the root within 20 cm and 30 degrees of where the stretch starts, the upper body moving
// about as much as a listener's does (picked over all 148 cutscenes by a scratch tool of the review), then looked at
// in the game (bot_mocap_preview): out went a phone at the ear, a hand at the mouth, a foot up on something, leaning
// on a counter, bending down, arms up, getting up from the ground
const ClipDef kClips[] = {
    {"BCRAS2", "csplay", 17.5f, 22.5f},   {"BCRAS2", "csplay", 25.5f, 30.5f},  {"BHILL1", "csplay", 111.5f, 116.5f},
    {"CAS6B_1", "csplay", 8.5f, 13.5f},   {"CAS_11A", "csplay", 28.5f, 33.5f}, {"CAS_4C", "csplay", 47.5f, 52.5f},
    {"CRASH3A", "csplay", 19.5f, 24.5f},  {"CRASH3A", "csplay", 52.5f, 57.5f}, {"CRASV2A", "CsPlay", 48.5f, 53.5f},
    {"DESERT4", "Csplay", 5.5f, 10.5f},   {"FARL_2A", "CsPlay", 34.5f, 39.5f}, {"FARL_3A", "CsPlay", 42.5f, 47.5f},
    {"GARAG1B", "csplay", 93.5f, 98.5f},  {"GROVE1A", "csplay", 38.5f, 43.5f}, {"GROVE1C", "csplay", 7.5f, 12.5f},
    {"GROVE1C", "csplay", 44.5f, 49.5f},  {"GROVE2", "csplay", 57.5f, 62.5f},  {"RIOT_2", "csplay", 35.5f, 40.5f},
    {"RIOT_2", "csplay", 56.5f, 61.5f},   {"RYDER3A", "csplay", 47.5f, 52.5f}, {"SCRASH2", "csplay", 65.5f, 70.5f},
    {"STRAP2A", "CSplay", 52.5f, 57.5f},  {"STRAP3A", "csplay", 68.5f, 73.5f}, {"STRAP3A", "csplay", 73.5f, 78.5f},
    {"SYND_7", "CsPlay", 26.5f, 31.5f},   {"TRUTH_1", "Csplay", 31.5f, 36.5f}, {"TRUTH_2", "csplay", 8.5f, 13.5f},
    {"ZERO_4", "CsPlay", 9.5f, 14.5f},
};
constexpr size_t kNumClips = std::size(kClips);
constexpr float kRootHeight = 0.023f;  // the root's height in the game's idles (PED/IDLE_stance)
// CJ walking in them: two steps from one pose back to the same (the bones within a degree and a half of it on
// average), the root going straight at an even speed (picked over all cutscenes by find_walks.py of the review)
const ClipDef kWalks[] = {
    {"D10_ALT", "CsPlay", 54.6f, 55.95f}, {"RIOT4E2", "csplay", 79.1f, 80.47f}, {"CAS_4B", "csplay", 84.0f, 85.31f},
    {"FARL_2A", "CsPlay", 78.7f, 80.09f}, {"DESERT4", "Csplay", 1.0f, 2.29f},   {"CAS_2", "csplay", 17.1f, 18.39f},
    {"DESERT6", "Csplay", 69.5f, 70.83f}, {"HEIST5A", "csplay", 34.3f, 35.49f},
};
constexpr size_t kNumWalks = std::size(kWalks);
constexpr float kWalkHeight = -0.01f;  // ... and in the game's walk (PED/WALK_player), on average

enum State { IDLE, MAKING, MADE, FAILED, LOADED };
std::atomic<int> g_state{IDLE};
std::vector<uint8_t> g_anpk;  // the thread's work, until the game loads it
uint32_t g_made = 0, g_madeWalks = 0;
char g_names[kNumClips][12], g_walkNames[kNumWalks][12];
float g_walkSpeed[kNumWalks];  // m/s: how fast the person went
size_t g_loaded = 0, g_walks = 0;

// the bones of a ped's skeleton (eBoneTag) but the jaw and brows (the facial task's): the cutscene faces' rig (5001+)
// and the thumbs are not on it
bool BodyBone(int tag) {
    return (tag >= 0 && tag <= 5) || (tag >= 21 && tag <= 26) || (tag >= 31 && tag <= 36) || (tag >= 41 && tag <= 44) ||
           (tag >= 51 && tag <= 54) || tag == 201 || tag == 301 || tag == 302;
}

struct Key {
    float q[4];  // as the file has them: the loader conjugates an ANPK's rotations
    float t[3];
    float time;  // seconds
};
struct Bone {
    char name[28];
    int32_t tag = -1;
    bool trans = false;
    std::vector<Key> keys;
};

uint32_t U32(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v;
    memcpy(&v, b.data() + at, 4);
    return v;
}
size_t Pad(size_t n) { return (n + 3) & ~size_t(3); }

bool ParseBones(const std::vector<uint8_t>& b, size_t pos, size_t end, std::vector<Bone>& out) {
    if (pos + 8 > end || memcmp(&b[pos], "INFO", 4)) return false;  // the bone count
    pos += 8 + Pad(U32(b, pos + 4));
    while (pos + 8 <= end) {
        size_t next = pos + 8 + Pad(U32(b, pos + 4)), a = pos + 8;
        if (memcmp(&b[pos], "CPAN", 4) || a + 8 + 44 > end || memcmp(&b[a], "ANIM", 4)) {
            pos = next;
            continue;
        }
        uint32_t animSize = U32(b, a + 4), frames = U32(b, a + 8 + 28);
        Bone bone;
        memcpy(bone.name, &b[a + 8], 28);
        bone.name[27] = 0;
        bone.tag = animSize == 44 ? (int32_t)U32(b, a + 8 + 40) : -1;  // as the loader reads it
        size_t k = a + 8 + Pad(animSize);
        if (frames && k + 8 <= end) {
            bool t = b[k + 2] == 'T', s = b[k + 3] == 'S';  // KR00, KRT0, KRTS
            size_t stride = 4 * (5 + (t ? 3 : 0) + (s ? 3 : 0));
            if (k + 8 + (size_t)frames * stride > end) return false;
            bone.trans = t;
            bone.keys.resize(frames);
            for (uint32_t i = 0; i < frames; ++i) {
                const auto* f = (const float*)&b[k + 8 + i * stride];
                Key& key = bone.keys[i];
                memcpy(key.q, f, 16);
                if (t) memcpy(key.t, f + 4, 12);
                else key.t[0] = key.t[1] = key.t[2] = 0;
                key.time = f[4 + (t ? 3 : 0) + (s ? 3 : 0)];
            }
        }
        if (BodyBone(bone.tag) && !bone.keys.empty()) out.push_back(std::move(bone));
        pos = next;
    }
    return !out.empty();
}

// the animation of one person of a cutscene IFP
bool ParseActor(const std::vector<uint8_t>& b, const char* actor, std::vector<Bone>& out) {
    if (b.size() < 16 || memcmp(b.data(), "ANPK", 4)) return false;
    size_t end = std::min(b.size(), (size_t)U32(b, 4) + 8), pos = 8;
    pos += 8 + Pad(U32(b, pos + 4));  // INFO: the block
    bool want = false;
    while (pos + 8 <= end) {
        uint32_t size = U32(b, pos + 4);
        size_t data = pos + 8;
        if (data + size > end) return false;
        if (!memcmp(&b[pos], "NAME", 4)) want = !_stricmp(std::string((const char*)&b[data], strnlen((const char*)&b[data], size)).c_str(), actor);
        else if (!memcmp(&b[pos], "DGAN", 4) && want) return ParseBones(b, data, data + size, out);
        pos = data + Pad(size);
    }
    return false;
}

float Dot(const float a[4], const float b[4]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; }

// the bone at time t; keyed at t whatever keys it has (a still bone has one): a key time out of order loops the
// game's CAnimBlendNode::NextKeyFrame for ever in a looped animation
Key Sample(const std::vector<Key>& ks, float t) {
    Key k = t <= ks.front().time ? ks.front() : ks.back();
    for (size_t i = 1; i < ks.size() && t > ks.front().time; ++i)
        if (ks[i].time >= t) {
            const Key &a = ks[i - 1], &b = ks[i];
            float u = b.time > a.time ? (t - a.time) / (b.time - a.time) : 0.0f, s = Dot(a.q, b.q) < 0 ? -1.0f : 1.0f, n = 0;
            for (int j = 0; j < 4; ++j) k.q[j] = a.q[j] + (b.q[j] * s - a.q[j]) * u, n += k.q[j] * k.q[j];
            for (int j = 0; j < 4; ++j) k.q[j] /= std::sqrt(n);
            for (int j = 0; j < 3; ++j) k.t[j] = a.t[j] + (b.t[j] - a.t[j]) * u;
            break;
        }
    k.time = t;
    return k;
}

// the stretch [from, to] of a bone's keys, from 0
std::vector<Key> Cut(const std::vector<Key>& ks, float from, float to) {
    std::vector<Key> out{Sample(ks, from)};
    for (const Key& k : ks)  // the files have a key a few ms back in time here and there: left out
        if (k.time > from && k.time < to && k.time > out.back().time) out.push_back(k);
    out.push_back(Sample(ks, to));
    for (Key& k : out) k.time -= from;
    for (size_t i = 1; i < out.size(); ++i)  // no sign flips between neighbours: the loader leaves compressed keys as they are
        if (Dot(out[i - 1].q, out[i].q) < 0)
            for (float& c : out[i].q) c = -c;
    return out;
}

void Mul(const float a[4], const float b[4], float o[4]) {  // (x, y, z, w)
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
float Yaw(const float g[4]) { return std::atan2(2 * (g[0] * g[1] + g[3] * g[2]), 1 - 2 * (g[1] * g[1] + g[2] * g[2])); }

// The game's ped animations hold the root a quarter turn about Z, the skeleton's X to the ped's forward (their pelvis
// is the same as the cutscenes'): the clip's root turns as the person did, facing that way on the whole. It does not
// travel (a walk goes on the spot: the ped is moved), and it is as high as in the game's own.
void Normalize(std::vector<Key>& root, float height) {
    float s = 0, c = 0, z = 0;
    for (const Key& k : root) {
        float g[4] = {-k.q[0], -k.q[1], -k.q[2], k.q[3]}, y = Yaw(g);
        s += std::sin(y), c += std::cos(y), z += k.t[2];
    }
    float d = 1.5707963f - std::atan2(s, c), lift = height - z / root.size();
    float turn[4] = {0, 0, std::sin(d / 2), std::cos(d / 2)};
    for (Key& k : root) {
        float g[4] = {-k.q[0], -k.q[1], -k.q[2], k.q[3]}, r[4];
        Mul(turn, g, r);
        k.q[0] = -r[0], k.q[1] = -r[1], k.q[2] = -r[2], k.q[3] = r[3];
        k.t[0] = k.t[1] = 0, k.t[2] += lift;
    }
}

struct Out {
    std::vector<uint8_t> b;
    void Raw(const void* p, size_t n) { b.insert(b.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
    void U32(uint32_t v) { Raw(&v, 4); }
    size_t Open(const char* tag) {
        size_t at = b.size();
        Raw(tag, 4);
        U32(0);
        return at;
    }
    void Close(size_t at) {  // the size without the padding, as the game's files have it
        uint32_t size = (uint32_t)(b.size() - at - 8);
        memcpy(&b[at + 4], &size, 4);
        while (b.size() % 4) b.push_back(0);
    }
};

void WriteClip(Out& o, const char* name, const std::vector<Bone>& bones) {
    size_t n = o.Open("NAME");
    o.Raw(name, strlen(name) + 1);
    o.Close(n);
    size_t dgan = o.Open("DGAN"), info = o.Open("INFO");
    o.U32((uint32_t)bones.size());
    o.U32(0);
    o.Close(info);
    for (const Bone& bone : bones) {
        size_t cpan = o.Open("CPAN"), anim = o.Open("ANIM");
        o.Raw(bone.name, 28);
        o.U32((uint32_t)bone.keys.size());
        o.U32(0);
        o.U32(0);
        o.U32((uint32_t)bone.tag);
        o.Close(anim);
        size_t kf = o.Open(bone.trans ? "KRT0" : "KR00");
        for (const Key& k : bone.keys) {
            o.Raw(k.q, 16);
            if (bone.trans) o.Raw(k.t, 12);
            o.Raw(&k.time, 4);
        }
        o.Close(kf);
        o.Close(cpan);
    }
    o.Close(dgan);
}

// the thread: anim\cuts.img (VER2: per entry its offset and size in 2048-byte sectors and its name) to our ANPK
void Make() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path(exe);
    path = path.substr(0, path.find_last_of(L"\\/") + 1) + L"anim\\cuts.img";
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) {
        LOGW("Cutscene clips: no %s", WideToUtf8(path).c_str());
        g_state = FAILED;
        return;
    }
    std::map<std::string, std::pair<uint32_t, uint32_t>> dir;
    char head[8];
    if (fread(head, 1, 8, f) == 8 && !memcmp(head, "VER2", 4)) {
        uint32_t count;
        memcpy(&count, head + 4, 4);
        std::vector<uint8_t> entries((size_t)std::min(count, 100000u) * 32);
        if (fread(entries.data(), 1, entries.size(), f) == entries.size())
            for (size_t i = 0; i < entries.size(); i += 32) {
                uint32_t off;
                uint16_t s1, s2;
                memcpy(&off, &entries[i], 4), memcpy(&s1, &entries[i + 4], 2), memcpy(&s2, &entries[i + 6], 2);
                char name[25] = {};
                memcpy(name, &entries[i + 8], 24);
                dir[ToLower(name)] = {off, s1 ? s1 : s2};
            }
    }
    Out o;
    size_t anpk = o.Open("ANPK"), info = o.Open("INFO");
    o.U32(0);  // the clip count, written below
    o.Raw("COOPCS\0\0", 8);
    o.Close(info);
    std::string have;
    std::vector<uint8_t> ifp;
    auto cut = [&](const ClipDef& c, std::vector<Bone>& bones) {  // the person's bones over [from, to], from 0
        std::string file = ToLower(c.cutscene) + ".ifp";
        auto it = dir.find(file);
        if (it == dir.end()) return false;
        if (have != file) {
            have.clear();
            ifp.resize((size_t)it->second.second * 2048);
            if (ifp.empty() || _fseeki64(f, (int64_t)it->second.first * 2048, SEEK_SET) ||
                fread(ifp.data(), 1, ifp.size(), f) != ifp.size())
                return false;
            have = file;
        }
        if (!ParseActor(ifp, c.actor, bones)) return false;
        for (Bone& bone : bones) bone.keys = Cut(bone.keys, c.from, c.to);
        for (const Bone& bone : bones)  // times from 0 in order, or the game's looping never ends (see Sample)
            for (size_t i = 0; i < bone.keys.size(); ++i)
                if (bone.keys[i].time < (i ? bone.keys[i - 1].time : 0.0f)) {
                    LOGW("Cutscene clips: %s %.1f-%.1f s, %s keyed out of order: left out", c.cutscene, c.from, c.to, bone.name);
                    return false;
                }
        return true;
    };
    uint32_t made = 0, walks = 0;
    for (const ClipDef& c : kClips) {
        std::vector<Bone> bones;
        if (!cut(c, bones)) continue;
        for (Bone& bone : bones)
            if (bone.tag == 0) Normalize(bone.keys, kRootHeight), bone.trans = true;
        snprintf(g_names[made], sizeof g_names[made], "coopcs%02u", made);
        WriteClip(o, g_names[made], bones);
        ++made;
    }
    for (const ClipDef& c : kWalks) {
        std::vector<Bone> bones;
        if (!cut(c, bones)) continue;
        float speed = 0;
        for (Bone& bone : bones) {
            if (bone.tag == 0) {
                const Key &a = bone.keys.front(), &b = bone.keys.back();
                speed = std::hypot(b.t[0] - a.t[0], b.t[1] - a.t[1]) / (c.to - c.from);
                Normalize(bone.keys, kWalkHeight), bone.trans = true;
            }
            // it ends in the pose it starts with (it was within a degree or two): it loops without a jump
            Key& last = bone.keys.back();
            float time = last.time;
            last = bone.keys.front();
            last.time = time;
            if (bone.keys.size() > 1 && Dot(bone.keys[bone.keys.size() - 2].q, last.q) < 0)
                for (float& q : last.q) q = -q;
        }
        if (speed < 0.5f) continue;  // the root does not carry the walk
        g_walkSpeed[walks] = speed;
        snprintf(g_walkNames[walks], sizeof g_walkNames[walks], "coopwk%02u", walks);
        WriteClip(o, g_walkNames[walks], bones);
        ++walks;
    }
    fclose(f);
    uint32_t count = made + walks;
    memcpy(&o.b[info + 8], &count, 4);
    o.Close(anpk);
    if (!made) {
        LOGW("Cutscene clips: none could be made from %s", WideToUtf8(path).c_str());
        g_state = FAILED;
        return;
    }
    g_anpk = std::move(o.b);
    g_made = made, g_madeWalks = walks;
    g_state = MADE;
}

// in the game's thread
void Load() {
    struct RwMemory {
        uint8_t* start;
        uint32_t length;
    } mem{g_anpk.data(), (uint32_t)g_anpk.size()};
    static const char kNoneUncompressed[1][32] = {""};
    void* stream = Call<0x7ECEF0, void*>(3, 1, (void*)&mem);  // RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD)
    if (stream) {
        Call<0x4D47F0>(stream, true, kNoneUncompressed);  // CAnimManager::LoadAnimFile, compressed like the cutscenes'
        Call<0x7ECE20, int>(stream, (void*)&mem);         // RwStreamClose
    }
    if (Call<0x4D3990, int>(kMocapBlock) < 0) {  // CAnimManager::GetAnimationBlockIndex
        LOGW("Cutscene clips: the game did not take them");
        g_state = FAILED;
    } else {
        g_loaded = g_made, g_walks = g_madeWalks;
        g_state = LOADED;
        LOGI("Cutscene clips: %u of %zu standing, %u of %zu walking loaded (%zu KB)", g_made, kNumClips, g_madeWalks, kNumWalks,
             g_anpk.size() / 1024);
    }
    std::vector<uint8_t>().swap(g_anpk);
}

class SceneMocap : public Module {
public:
    const char* Name() const override { return "SceneMocap"; }
    void OnPostProcess() override {
        int state = g_state;
        if (state == IDLE) {
            g_state = MAKING;
            std::thread(Make).detach();
            return;
        }
        if (state == LOADED && Call<0x4D3990, int>(kMocapBlock) < 0) {  // never seen: the game removes only its own blocks
            LOGW("Cutscene clips: the block is gone, making it again");
            g_loaded = g_walks = 0;
            g_state = IDLE;
            return;
        }
        // Before any cutscene loads its block: CCutsceneMgr::DeleteCutsceneData removes the last block
        // (CAnimManager::RemoveLastAnimFile), which must stay the cutscene's.
        if (state != MADE || !core::IsPlayerReady() || Ref<int32_t>(0xB5F84C) || Ref<uint8_t>(0xB5F850) || Ref<uint8_t>(0xB5F851))
            return;  // ms_cutsceneLoadStatus, ms_animLoaded, ms_running
        if (Ref<int32_t>(0xB4EA30) + 2 >= 180 || Ref<int32_t>(0xB4EA2C) + (int)(g_made + g_madeWalks) >= 2500) {  // CAnimManager's arrays
            LOGW("Cutscene clips: the game's animation arrays are full");
            g_state = FAILED;
            std::vector<uint8_t>().swap(g_anpk);
            return;
        }
        Load();
    }
};
SceneMocap g_mocap;
struct Reg {
    Reg() { core::RegisterModule(&g_mocap); }
} g_reg;
}  // namespace

size_t MocapCount() { return g_loaded; }
const char* MocapClip(size_t i) { return g_loaded ? g_names[i % g_loaded] : nullptr; }
size_t MocapWalkCount() { return g_walks; }
const char* MocapWalk(size_t i) { return g_walks ? g_walkNames[i % g_walks] : nullptr; }
float MocapWalkSpeed(size_t i) { return g_walks ? g_walkSpeed[i % g_walks] : 0.0f; }

}  // namespace coop::scene
