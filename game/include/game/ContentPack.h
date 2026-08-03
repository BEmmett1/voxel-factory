#pragma once

#include <string>
#include <vector>

// The content pack format: one JSON document describing content by KEY.
//
// It has a writer (ContentDump.cpp, `--dump-content`) and a reader
// (ContentPack.cpp), and they are deliberately the same shape -- what the game
// writes is exactly what it will read back, so the format cannot drift from its
// own documentation. `--selftest` holds a dump -> load -> dump round-trip to
// keep that true.
//
// The writer therefore does double duty: it is the spec, the vocabulary of
// every key this build knows, and a complete worked example, all in one file.
// That is what you hand something that has to WRITE content, which is the point
// of the format existing at all.
//
// Nothing is referenced by ordinal here. An ordinal is this build's private
// encoding for a save or a wire (ContentRegistry.h); a key is the identity two
// content sets can actually agree on.
namespace content {

    // Bumped when the document's shape changes incompatibly. A pack declaring
    // anything else is refused rather than half-understood.
    inline constexpr int kPackFormat = 1;

    // The whole content set as a JSON document (trailing newline included).
    std::string dumpContent();

    // Every *.json in `dir`, sorted, so a folder of packs applies in a stable
    // order. An unreadable or absent folder is simply no packs.
    std::vector<std::string> findPacks(const std::string& dir);

    // Apply packs, in order, then check the result with content::validate().
    //
    // ALL OR NOTHING: if any pack fails to parse, or if the content set they
    // produce between them is incoherent, the tables are restored to exactly
    // what they were and the problems are returned. A pack may make the game
    // different; it may not make it broken, and it may never leave it half
    // converted.
    //
    // Startup only -- see the note on recipes::handTable() in Recipes.h.
    std::vector<std::string> applyPacks(const std::vector<std::string>& paths);

} // namespace content
