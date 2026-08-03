#include "game/ContentRegistry.h"

#include <cstddef>

namespace content {

    std::string_view blockKey(BlockId id) { return blockInfo(id).key; }
    std::string_view itemKey(ItemId id)   { return itemInfo(id).key; }

    BlockId blockFromKey(std::string_view key) {
        const auto n = static_cast<std::uint32_t>(blockCount());
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto id = static_cast<BlockId>(i);
            if (blockKey(id) == key) return id;
        }
        return kNoBlock;
    }

    ItemId itemFromKey(std::string_view key) {
        const auto n = static_cast<std::uint32_t>(itemCount());
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto id = static_cast<ItemId>(i);
            if (itemKey(id) == key) return id;
        }
        return kNoItem;
    }

    std::vector<std::string> blockKeyTable() {
        std::vector<std::string> keys;
        const auto n = static_cast<std::uint32_t>(blockCount());
        keys.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            keys.emplace_back(blockKey(static_cast<BlockId>(i)));
        }
        return keys;
    }

    std::vector<std::string> itemKeyTable() {
        std::vector<std::string> keys;
        const auto n = static_cast<std::uint32_t>(itemCount());
        keys.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            keys.emplace_back(itemKey(static_cast<ItemId>(i)));
        }
        return keys;
    }

    ContentMap ContentMap::identity() {
        ContentMap m;
        m.m_identity = true;
        return m;
    }

    ContentMap ContentMap::build(const std::vector<std::string>& blockKeys,
                                 const std::vector<std::string>& itemKeys) {
        ContentMap m;
        m.m_blocks.reserve(blockKeys.size());
        for (const std::string& k : blockKeys) {
            const BlockId id = blockFromKey(k);
            // Unknown content becomes Air: the cell empties, which is the only
            // honest answer when we have no geometry, no drop and no behaviour
            // for it. missing() is what turns that into a message.
            if (id == kNoBlock) {
                m.m_missing.push_back(k);
                m.m_blocks.push_back(BlockId::Air);
            } else {
                m.m_blocks.push_back(id);
            }
        }
        m.m_items.reserve(itemKeys.size());
        for (const std::string& k : itemKeys) {
            const ItemId id = itemFromKey(k);
            if (id == kNoItem) {
                m.m_missing.push_back(k);
                m.m_items.push_back(ItemId::None);
            } else {
                m.m_items.push_back(id);
            }
        }
        // A table that already matches ours position-for-position costs nothing
        // to translate, which is the common case (same build, no mods).
        m.m_identity = m.m_missing.empty() &&
            blockKeys.size() == blockCount() && itemKeys.size() == itemCount();
        if (m.m_identity) {
            for (std::size_t i = 0; i < m.m_blocks.size(); ++i) {
                if (m.m_blocks[i] != static_cast<BlockId>(i)) { m.m_identity = false; break; }
            }
        }
        if (m.m_identity) {
            for (std::size_t i = 0; i < m.m_items.size(); ++i) {
                if (m.m_items[i] != static_cast<ItemId>(i)) { m.m_identity = false; break; }
            }
        }
        return m;
    }

    BlockId ContentMap::block(std::uint32_t foreign) const {
        if (m_identity) {
            return foreign < static_cast<std::uint32_t>(blockCount())
                       ? static_cast<BlockId>(foreign) : BlockId::Air;
        }
        return foreign < m_blocks.size() ? m_blocks[foreign] : BlockId::Air;
    }

    ItemId ContentMap::item(std::uint32_t foreign) const {
        if (m_identity) {
            return foreign < static_cast<std::uint32_t>(itemCount())
                       ? static_cast<ItemId>(foreign) : ItemId::None;
        }
        return foreign < m_items.size() ? m_items[foreign] : ItemId::None;
    }

} // namespace content
