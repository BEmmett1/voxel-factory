#pragma once

#include "game/Item.h"

#include <array>
#include <cstddef>

// The player's item counts, indexed by ItemId.
class Inventory {
public:
    int count(ItemId id) const { return m_counts[idx(id)]; }
    bool has(ItemId id, int n = 1) const { return m_counts[idx(id)] >= n; }

    void add(ItemId id, int n = 1) {
        if (id != ItemId::None) m_counts[idx(id)] += n;
    }

    // Removes n if available; returns false (and changes nothing) otherwise.
    bool remove(ItemId id, int n = 1) {
        if (m_counts[idx(id)] < n) return false;
        m_counts[idx(id)] -= n;
        return true;
    }

private:
    static std::size_t idx(ItemId id) { return static_cast<std::size_t>(id); }
    std::array<int, static_cast<std::size_t>(ItemId::Count)> m_counts{};
};
