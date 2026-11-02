// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>
#include "../../ConstEnums.h"

// Asset-free validation shared by the portable save loader and regression tests.
namespace PvzpSave {
inline constexpr uint32_t SeedLayoutChunk = 0x4D4F4403U;
inline constexpr uint32_t CustomZombiesChunk = 0x4D4F4405U;
inline constexpr uint32_t CustomLevelsChunk = 0x4D4F4406U;
inline constexpr uint32_t StableSeedLayout = 200U;
inline constexpr size_t MaxBlobBytes = 1024U * 1024U;

static_assert(int(SEED_ELECTRIC_GATLINGPEA) == 53 && int(NUM_SEED_TYPES) == 54 && int(SEED_IMITATER) == 48 &&
    int(SEED_NONE) == -1 && int(SEED_CUSTOM_0) == 200 && int(SEED_CUSTOM_1) == 201 &&
    int(SEED_CUSTOM_2) == 202 && int(SEED_CUSTOM_3) == 203 &&
    int(SEED_CUSTOM_FIRST) == 200 && int(SEED_CUSTOM_LAST) == 215 &&
    int(NUM_CUSTOM_PLANT_SLOTS) == 16 && int(NUM_SEED_SLOTS) == 216, "Portable seed layout changed: add an explicit migration/version");
static_assert([] {
    constexpr SeedType pseudo[] = {SEED_BEGHOULED_BUTTON_SHUFFLE, SEED_BEGHOULED_BUTTON_CRATER,
        SEED_SLOT_MACHINE_SUN, SEED_SLOT_MACHINE_DIAMOND, SEED_ZOMBIQUARIUM_SNORKLE, SEED_ZOMBIQUARIUM_TROPHY,
        SEED_ZOMBIE_NORMAL, SEED_ZOMBIE_TRAFFIC_CONE, SEED_ZOMBIE_POLEVAULTER, SEED_ZOMBIE_PAIL,
        SEED_ZOMBIE_LADDER, SEED_ZOMBIE_DIGGER, SEED_ZOMBIE_BUNGEE, SEED_ZOMBIE_FOOTBALL,
        SEED_ZOMBIE_BALLOON, SEED_ZOMBIE_SCREEN_DOOR, SEED_ZOMBONI, SEED_ZOMBIE_POGO,
        SEED_ZOMBIE_DANCER, SEED_ZOMBIE_GARGANTUAR, SEED_ZOMBIE_IMP};
    for (size_t i = 0; i < sizeof(pseudo) / sizeof(pseudo[0]); ++i)
        if (int(pseudo[i]) != 55 + int(i)) return false;
    return true;
}(), "Portable pseudo seed ids 55..75 must not shift");

inline bool ValidPlantSeedId(int32_t value) {
    return (value >= 0 && value <= 53) || (value >= int(SEED_CUSTOM_FIRST) && value <= int(SEED_CUSTOM_LAST));
}
inline bool ValidImitaterSeedId(int32_t value) {
    return value == -1 || (value >= 0 && value <= 48);
}
inline bool ValidSeedId(int32_t value) {
    return value == -1 || (value >= 0 && value <= 53) ||
        (value >= 55 && value <= 75) || (value >= int(SEED_CUSTOM_FIRST) && value <= int(SEED_CUSTOM_LAST));
}
inline bool PseudoSeedId(int32_t value) { return value >= 55 && value <= 75; }
inline bool ValidPacketCount(int32_t value, uint32_t maximum) {
    return value >= 0 && static_cast<uint32_t>(value) <= maximum;
}
inline bool ValidBlobSize(size_t size, size_t remaining) {
    return size <= MaxBlobBytes && size <= remaining;
}

struct Span { const uint8_t* data = nullptr; size_t size = 0; };
class TLVReader {
public:
    const uint8_t* mData;
    size_t mSize, mPos = 0;
    bool mOk = true;
    TLVReader(const uint8_t* data, size_t size) : mData(data), mSize(size) {}
    bool ReadU32(uint32_t& value) {
        if (mPos > mSize || mSize - mPos < 4) { mOk = false; value = 0; return false; }
        value = uint32_t(mData[mPos]) | (uint32_t(mData[mPos + 1]) << 8) |
            (uint32_t(mData[mPos + 2]) << 16) | (uint32_t(mData[mPos + 3]) << 24);
        mPos += 4;
        return true;
    }
    bool ReadBytes(const uint8_t*& data, size_t size) {
        if (mPos > mSize || size > mSize - mPos) { mOk = false; data = nullptr; return false; }
        data = mData ? mData + mPos : nullptr;
        mPos += size;
        return true;
    }
    bool Next(uint32_t& id, Span& value) {
        uint32_t size = 0;
        return ReadU32(id) && ReadU32(size) && ReadBytes(value.data, value.size = size);
    }
};

inline bool ValidFields(Span fields, uint32_t knownMax, bool knownTail = false) {
    TLVReader reader(fields.data, fields.size);
    std::set<uint32_t> seen;
    while (reader.mPos < reader.mSize) {
        uint32_t id = 0;
        Span value;
        if (!reader.Next(id, value)) return false;
        if (((id >= 1 && id <= knownMax) || (knownTail && id == 100)) && !seen.insert(id).second)
            return false;
    }
    return reader.mOk;
}

inline bool KnownChunk(uint32_t type) {
    return (type >= 1 && type <= 20) ||
        type == 0x4D4F4401U || type == 0x4D4F4402U || type == SeedLayoutChunk || type == 0x4D4F4404U ||
        type == CustomZombiesChunk || type == CustomLevelsChunk;
}
struct ChunkView { uint32_t type; Span bytes; };

inline bool ChunkBody(Span chunk, Span& body) {
    TLVReader reader(chunk.data, chunk.size);
    uint32_t version = 0;
    if (!reader.ReadU32(version) || version != 1) return false;
    bool found = false;
    while (reader.mPos < reader.mSize) {
        uint32_t id = 0;
        Span field;
        if (!reader.Next(id, field)) return false;
        if (id == 1) {
            if (found) return false;
            found = true;
            body = field;
        }
    }
    return reader.mOk && found;
}

// Preflight the complete payload/chunk envelopes before touching Board state.
// Unknown chunks/fields remain forward-compatible. Nested entity contents are
// validated separately during import, so this is not a transactional load.
inline bool PreflightPayload(Span payload, std::vector<ChunkView>& chunks, bool& marked) {
    chunks.clear();
    marked = false;
    bool base = false;
    std::set<uint32_t> seen;
    TLVReader reader(payload.data, payload.size);
    while (reader.mPos < reader.mSize) {
        uint32_t type = 0;
        Span chunk;
        if (!reader.Next(type, chunk)) return false;
        if (KnownChunk(type)) {
            if (!seen.insert(type).second) return false;
            Span body;
            if (!ChunkBody(chunk, body)) return false;
            if (type == SeedLayoutChunk) {
                TLVReader layout(body.data, body.size);
                uint32_t value = 0;
                if (!layout.ReadU32(value) || layout.mPos != layout.mSize || value != StableSeedLayout)
                    return false;
                marked = true;
            }
        }
        if (type == 1) base = true;
        chunks.push_back({type, chunk});
    }
    return reader.mOk && base;
}
} // namespace PvzpSave
