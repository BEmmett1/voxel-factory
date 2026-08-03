#pragma once

#include <string>
#include <vector>

// Is the content set COHERENT? -- asked of the live tables, at any time, by
// anyone.
//
// These checks were born inside `--selftest`, where they replaced the
// append-only rule the recipe tables used to live under: once a row may be
// reordered or deleted, the guardrail has to be about MEANING (can the tech
// tree still be walked from nothing?) rather than about ordering. They are
// still exactly the right questions to ask, but a build-time exit code is the
// wrong shape for them -- content no longer only comes from a compiler.
//
// So they answer with DIAGNOSTICS instead. `--selftest` prints them and fails;
// the pack loader (ContentPack.h) prints them and refuses the pack; and a
// generator that writes content can run the same loop to find out what it got
// wrong, in English, rather than being told only that something is.
namespace content {

    // Empty = coherent. Each entry is one player-readable problem, complete
    // enough to act on without reading this file (it names the recipe key or
    // the item it is talking about).
    std::vector<std::string> validate();

} // namespace content
