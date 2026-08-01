#pragma once

#include "game/Block.h"
#include "game/Item.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Content identity that survives the ordinal.
//
// BlockId/ItemId ordinals are an ENCODING, not an identity: they are the raw
// bytes in a chunk and the slot positions in an inventory, so they only mean
// anything relative to the content set that produced them. That is fine while
// the only content set is the one compiled into this binary, and stops being
// fine the moment two content sets have to agree -- which happens twice:
//
//   * a SAVE written by a different build (content added or removed since), and
//   * a SERVER we joined, whose content set is authoritative for the session.
//
// Both are the same problem: "your ordinal N is my ordinal M". So both are
// served by the same object rather than by two mechanisms invented apart. Each
// row of kBlocks/kItems carries a stable `key`; a foreign content set is
// described by its list of keys in ITS ordinal order, and ContentMap turns that
// into a translation into ours.
namespace content {

    // ---- Our own content set --------------------------------------------
    // Key <-> ordinal over the compiled registries. Lookup is a linear scan:
    // it runs once per key at load/join, never per block.
    std::string_view blockKey(BlockId id);
    std::string_view itemKey(ItemId id);

    // BlockId::Count / ItemId::Count when the key is unknown to this build --
    // a distinguishable "no such content" rather than a wrong guess.
    BlockId blockFromKey(std::string_view key);
    ItemId  itemFromKey(std::string_view key);

    // Our full key tables, in our ordinal order. This is what gets written to
    // a save header, and what a server would send a joining client.
    std::vector<std::string> blockKeyTable();
    std::vector<std::string> itemKeyTable();

    // ---- Translating a foreign content set ------------------------------
    // Built from a foreign side's key tables; maps its ordinals onto ours.
    // Content we do not have maps to Air / None, and is reported so the caller
    // can name the missing mod instead of silently eating the player's world.
    class ContentMap {
    public:
        // Identity: the foreign ordinals ARE ours. Used for pre-v22 saves,
        // which predate the key tables and were written by this content set's
        // own ancestor, so their ordinals are ours by construction.
        static ContentMap identity();

        // index = foreign ordinal, value = that side's key.
        static ContentMap build(const std::vector<std::string>& blockKeys,
                                const std::vector<std::string>& itemKeys);

        BlockId block(std::uint32_t foreign) const;
        ItemId  item(std::uint32_t foreign) const;

        // Keys the foreign side had and we do not. Empty = a clean match.
        const std::vector<std::string>& missing() const { return m_missing; }
        bool isIdentity() const { return m_identity; }

        // How many ids the foreign side declared. 0 means it never told us --
        // an identity() map, i.e. a save older than the key tables -- which is
        // the difference between "this length is authoritative, reject anything
        // else" and "accept a shorter legacy run and default the tail".
        std::size_t foreignBlockCount() const { return m_blocks.size(); }
        std::size_t foreignItemCount() const { return m_items.size(); }

    private:
        std::vector<BlockId>     m_blocks;
        std::vector<ItemId>      m_items;
        std::vector<std::string> m_missing;
        bool                     m_identity = false;
    };

} // namespace content
