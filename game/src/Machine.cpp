#include "game/Machine.h"

#include <cstddef>
#include <iterator>
#include <vector>

// The machine and fuel registries at runtime. kMachineTraitSeed and kFuelSeed in
// Machine.h are the SEEDS; these are the tables, and a content pack appends to
// them. See the note on blockTable() in Block.cpp -- same shape, same reasons.

namespace {

    std::vector<MachineTraits>& traitTable() {
        static std::vector<MachineTraits> table(std::begin(kMachineTraitSeed),
                                                std::end(kMachineTraitSeed));
        return table;
    }

    // block ordinal -> row index, -1 for "not a machine". Rebuilt whenever the
    // table grows, which is startup only.
    //
    // This was a `constexpr std::array<std::int8_t, BlockId::Count>`, and the
    // int8_t was a silent ceiling at 127 machines that nobody would have found
    // until the 128th one started reading another machine's row. A vector of
    // int sized by blockCount() has neither the cap nor the fixed size.
    std::vector<int>& traitIndex() {
        static std::vector<int> idx;
        if (idx.size() != blockCount()) {
            idx.assign(blockCount(), -1);
            for (std::size_t i = 0; i < traitTable().size(); ++i) {
                const auto b = static_cast<std::size_t>(traitTable()[i].block);
                if (b < idx.size()) idx[b] = static_cast<int>(i);
            }
        }
        return idx;
    }

    std::vector<FuelInfo>& fuelTable() {
        static std::vector<FuelInfo> table(std::begin(kFuelSeed), std::end(kFuelSeed));
        return table;
    }

} // namespace

const MachineTraits& machineTraits(BlockId id) {
    return traitTable()[static_cast<std::size_t>(traitIndex()[static_cast<std::size_t>(id)])];
}

bool hasMachineTraits(BlockId id) {
    const std::size_t i = static_cast<std::size_t>(id);
    return i < traitIndex().size() && traitIndex()[i] >= 0;
}

const std::vector<MachineTraits>& machineTraitRows() {
    return traitTable();
}

void addMachineTraits(const MachineTraits& row) {
    traitTable().push_back(row);
    traitIndex().clear(); // force the rebuild; blockCount() has moved too
    (void)traitIndex();
}

float fuelSeconds(ItemId item) {
    for (const FuelInfo& f : fuelTable()) {
        if (f.item == item) return f.seconds;
    }
    return 0.0f;
}

const std::vector<FuelInfo>& fuelRows() {
    return fuelTable();
}

void addFuel(const FuelInfo& row) {
    fuelTable().push_back(row);
}
