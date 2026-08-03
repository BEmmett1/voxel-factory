#pragma once

#include "game/Item.h"

#include <algorithm>
#include <cstddef>
#include <vector>

// The player's item counts, indexed by ItemId.
//
// This was a `std::array<int, ItemId::Count>`, which was exactly right while
// every item was compiled in. It cannot be once a pack adds items: the array
// would be sized by the number of items the BINARY has, and an id past that is
// a buffer overrun in the quietest possible place -- a count going into the
// next item's slot.
//
// So it is a vector sized by itemCount(), and it GROWS on demand rather than
// only at construction: an Inventory can outlive the moment its size was
// decided (a Machine loaded from a save, a drag payload held across a frame),
// and a stale one that silently reported zero for a modded item would be a
// worse bug than the overrun. ~130 ints is one small allocation; a machine
// holds three of them.
class Inventory {
public:
    int count(ItemId id) const {
        const std::size_t i = idx(id);
        return i < m_counts.size() ? m_counts[i] : 0;
    }
    bool has(ItemId id, int n = 1) const { return count(id) >= n; }

    void add(ItemId id, int n = 1) {
        if (id != ItemId::None) slot(id) += n;
    }

    // Removes n if available; returns false (and changes nothing) otherwise.
    bool remove(ItemId id, int n = 1) {
        if (count(id) < n) return false;
        slot(id) -= n;
        return true;
    }

private:
    static std::size_t idx(ItemId id) { return static_cast<std::size_t>(id); }

    int& slot(ItemId id) {
        const std::size_t i = idx(id);
        if (i >= m_counts.size()) m_counts.resize(std::max(i + 1, itemCount()), 0);
        return m_counts[i];
    }

    std::vector<int> m_counts;
};
