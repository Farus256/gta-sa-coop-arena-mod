#pragma once
// IMA ADPCM, 4 bits per 16-bit sample. Every frame carries its own start state, so a lost packet
// never desynchronises the decoder: [i16 predictor][u8 step index][u8 0][n/2 bytes of nibbles].
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace coop::adpcm {

inline const int kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
inline const int kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

struct State {
    int predictor = 0;
    int index = 0;
};

inline int Step(State& s, int nibble) {
    int step = kStepTable[s.index];
    int delta = step >> 3;
    if (nibble & 4) delta += step;
    if (nibble & 2) delta += step >> 1;
    if (nibble & 1) delta += step >> 2;
    s.predictor = std::clamp(s.predictor + ((nibble & 8) ? -delta : delta), -32768, 32767);
    s.index = std::clamp(s.index + kIndexTable[nibble], 0, 88);
    return s.predictor;
}

// n must be even
inline std::vector<uint8_t> Encode(const int16_t* pcm, int n, State& s) {
    std::vector<uint8_t> out(4 + n / 2);
    int16_t pred = (int16_t)s.predictor;
    memcpy(out.data(), &pred, 2);
    out[2] = (uint8_t)s.index;
    for (int i = 0; i < n; ++i) {
        int diff = pcm[i] - s.predictor, nibble = 0;
        if (diff < 0) nibble = 8, diff = -diff;
        int step = kStepTable[s.index];
        if (diff >= step) nibble |= 4, diff -= step;
        if (diff >= step >> 1) nibble |= 2, diff -= step >> 1;
        if (diff >= step >> 2) nibble |= 1;
        Step(s, nibble);  // the encoder tracks exactly what the decoder will reconstruct
        out[4 + i / 2] |= (uint8_t)(i & 1 ? nibble << 4 : nibble);
    }
    return out;
}

inline bool Decode(const uint8_t* data, size_t size, std::vector<int16_t>& pcm) {
    if (size < 4 || data[2] > 88) return false;
    State s;
    int16_t pred;
    memcpy(&pred, data, 2);
    s.predictor = pred;
    s.index = data[2];
    pcm.resize((size - 4) * 2);
    for (size_t i = 0; i < pcm.size(); ++i) {
        uint8_t b = data[4 + i / 2];
        pcm[i] = (int16_t)Step(s, i & 1 ? b >> 4 : b & 15);
    }
    return true;
}

}  // namespace coop::adpcm
