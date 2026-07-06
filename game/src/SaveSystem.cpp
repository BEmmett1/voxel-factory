#include "game/SaveSystem.h"

#include "game/World.h"
#include "game/Chunk.h"
#include "game/Block.h"
#include "game/Item.h"

#include <fstream>

namespace {

    constexpr std::uint32_t kMagic = 0x53465856u; // "VXFS"
    constexpr std::uint32_t kVersion = 6;         // bump when enums/layout change

    template <typename T>
    void writePod(std::ofstream& out, const T& v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }

    template <typename T>
    bool readPod(std::ifstream& in, T& v) {
        in.read(reinterpret_cast<char*>(&v), sizeof(T));
        return in.good();
    }

    void writeInventory(std::ofstream& out, const Inventory& inv) {
        const std::uint32_t n = static_cast<std::uint32_t>(ItemId::Count);
        writePod(out, n);
        for (std::uint32_t i = 0; i < n; ++i) {
            writePod(out, static_cast<std::int32_t>(inv.count(static_cast<ItemId>(i))));
        }
    }

    bool readInventory(std::ifstream& in, Inventory& inv) {
        std::uint32_t n = 0;
        if (!readPod(in, n) || n != static_cast<std::uint32_t>(ItemId::Count)) return false;
        for (std::uint32_t i = 0; i < n; ++i) {
            std::int32_t c = 0;
            if (!readPod(in, c) || c < 0) return false;
            if (c > 0) inv.add(static_cast<ItemId>(i), c);
        }
        return true;
    }

    bool validBlock(std::uint8_t b) {
        return b < static_cast<std::uint8_t>(BlockId::Count);
    }

} // namespace

namespace SaveSystem {

bool save(const std::string& path, const SaveData& d) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    writePod(out, kMagic);
    writePod(out, kVersion);
    writePod(out, d.worldSeed);
    writePod(out, d.sourceRng);
    writePod(out, d.camPos.x);
    writePod(out, d.camPos.y);
    writePod(out, d.camPos.z);
    writePod(out, d.camYaw);
    writePod(out, d.camPitch);
    writePod(out, static_cast<std::int32_t>(d.selectedSlot));

    writeInventory(out, d.inventory);

    // Chunks: coord + raw block bytes.
    writePod(out, static_cast<std::uint32_t>(d.world.chunks().size()));
    for (const auto& [coord, chunk] : d.world.chunks()) {
        writePod(out, static_cast<std::int32_t>(coord.x));
        writePod(out, static_cast<std::int32_t>(coord.y));
        writePod(out, static_cast<std::int32_t>(coord.z));
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    writePod(out, static_cast<std::uint8_t>(chunk->get(x, y, z)));
                }
            }
        }
    }

    // Machines.
    writePod(out, static_cast<std::uint32_t>(d.machines.size()));
    for (const auto& [pos, m] : d.machines) {
        writePod(out, static_cast<std::int32_t>(pos.x));
        writePod(out, static_cast<std::int32_t>(pos.y));
        writePod(out, static_cast<std::int32_t>(pos.z));
        writePod(out, static_cast<std::uint8_t>(m.type));
        writePod(out, static_cast<std::int32_t>(m.selectedRecipe));
        writePod(out, m.progress);
        writeInventory(out, m.input);
        writeInventory(out, m.output);
    }

    // Belts.
    writePod(out, static_cast<std::uint32_t>(d.belts.size()));
    for (const auto& [pos, b] : d.belts) {
        writePod(out, static_cast<std::int32_t>(pos.x));
        writePod(out, static_cast<std::int32_t>(pos.y));
        writePod(out, static_cast<std::int32_t>(pos.z));
        writePod(out, static_cast<std::int8_t>(b.facing.x));
        writePod(out, static_cast<std::int8_t>(b.facing.y));
        writePod(out, static_cast<std::int8_t>(b.facing.z));
        writePod(out, static_cast<std::uint8_t>(b.item));
    }

    // Sources.
    writePod(out, static_cast<std::uint32_t>(d.sources.size()));
    for (const auto& [pos, timer] : d.sources) {
        writePod(out, static_cast<std::int32_t>(pos.x));
        writePod(out, static_cast<std::int32_t>(pos.y));
        writePod(out, static_cast<std::int32_t>(pos.z));
        writePod(out, timer);
    }

    // Saplings.
    writePod(out, static_cast<std::uint32_t>(d.saplings.size()));
    for (const auto& [pos, timer] : d.saplings) {
        writePod(out, static_cast<std::int32_t>(pos.x));
        writePod(out, static_cast<std::int32_t>(pos.y));
        writePod(out, static_cast<std::int32_t>(pos.z));
        writePod(out, timer);
    }

    // Weather.
    writePod(out, static_cast<std::uint8_t>(d.weatherRaining ? 1 : 0));
    writePod(out, d.weatherTimer);

    return out.good();
}

bool load(const std::string& path, SaveData& d) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    std::uint32_t magic = 0, version = 0;
    if (!readPod(in, magic) || magic != kMagic) return false;
    if (!readPod(in, version) || version != kVersion) return false;

    if (!readPod(in, d.worldSeed)) return false;
    if (!readPod(in, d.sourceRng)) return false;
    if (!readPod(in, d.camPos.x) || !readPod(in, d.camPos.y) || !readPod(in, d.camPos.z)) return false;
    if (!readPod(in, d.camYaw) || !readPod(in, d.camPitch)) return false;
    std::int32_t slot = 0;
    if (!readPod(in, slot) || slot < 0) return false;
    d.selectedSlot = slot;

    if (!readInventory(in, d.inventory)) return false;

    // Chunks.
    std::uint32_t chunkCount = 0;
    if (!readPod(in, chunkCount) || chunkCount > 4096u) return false;
    for (std::uint32_t c = 0; c < chunkCount; ++c) {
        std::int32_t cx = 0, cy = 0, cz = 0;
        if (!readPod(in, cx) || !readPod(in, cy) || !readPod(in, cz)) return false;
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    std::uint8_t b = 0;
                    if (!readPod(in, b) || !validBlock(b)) return false;
                    d.world.setBlock(cx * CHUNK_SIZE + x, cy * CHUNK_SIZE + y,
                                     cz * CHUNK_SIZE + z, static_cast<BlockId>(b));
                }
            }
        }
    }

    // Machines.
    std::uint32_t machineCount = 0;
    if (!readPod(in, machineCount) || machineCount > 100000u) return false;
    for (std::uint32_t i = 0; i < machineCount; ++i) {
        std::int32_t x = 0, y = 0, z = 0;
        std::uint8_t type = 0;
        Machine m;
        if (!readPod(in, x) || !readPod(in, y) || !readPod(in, z)) return false;
        if (!readPod(in, type) || !validBlock(type) || !isMachine(static_cast<BlockId>(type))) return false;
        m.type = static_cast<BlockId>(type);
        std::int32_t sel = -1;
        if (!readPod(in, sel)) return false;
        m.selectedRecipe = sel;
        if (!readPod(in, m.progress)) return false;
        if (!readInventory(in, m.input) || !readInventory(in, m.output)) return false;
        d.machines[{x, y, z}] = std::move(m);
    }

    // Belts.
    std::uint32_t beltCount = 0;
    if (!readPod(in, beltCount) || beltCount > 1000000u) return false;
    for (std::uint32_t i = 0; i < beltCount; ++i) {
        std::int32_t x = 0, y = 0, z = 0;
        std::int8_t fx = 0, fy = 0, fz = 0;
        std::uint8_t item = 0;
        if (!readPod(in, x) || !readPod(in, y) || !readPod(in, z)) return false;
        if (!readPod(in, fx) || !readPod(in, fy) || !readPod(in, fz)) return false;
        if (!readPod(in, item) || item >= static_cast<std::uint8_t>(ItemId::Count)) return false;
        Belt b;
        b.facing = {fx, fy, fz};
        b.item = static_cast<ItemId>(item);
        d.belts[{x, y, z}] = b;
    }

    // Sources.
    std::uint32_t sourceCount = 0;
    if (!readPod(in, sourceCount) || sourceCount > 100000u) return false;
    for (std::uint32_t i = 0; i < sourceCount; ++i) {
        std::int32_t x = 0, y = 0, z = 0;
        float timer = 0.0f;
        if (!readPod(in, x) || !readPod(in, y) || !readPod(in, z)) return false;
        if (!readPod(in, timer)) return false;
        d.sources[{x, y, z}] = timer;
    }

    // Saplings.
    std::uint32_t saplingCount = 0;
    if (!readPod(in, saplingCount) || saplingCount > 100000u) return false;
    for (std::uint32_t i = 0; i < saplingCount; ++i) {
        std::int32_t x = 0, y = 0, z = 0;
        float timer = 0.0f;
        if (!readPod(in, x) || !readPod(in, y) || !readPod(in, z)) return false;
        if (!readPod(in, timer)) return false;
        d.saplings[{x, y, z}] = timer;
    }

    // Weather.
    std::uint8_t raining = 0;
    if (!readPod(in, raining)) return false;
    d.weatherRaining = raining != 0;
    if (!readPod(in, d.weatherTimer)) return false;

    return true;
}

} // namespace SaveSystem
