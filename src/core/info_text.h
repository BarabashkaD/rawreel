#pragma once

// The Info pane text (design M3 v2 §8.4 layer 1; D4 "tone-mapped" line): wx-free, so it is unit
// tested without the GUI.

#include <span>
#include <string>

#include "core/session_types.h"

namespace rawreel {

// Lines joined with '\n'. No streams: "No stream. Open a file (Ctrl+O) or a URI (Ctrl+U), or drop a
// file here." Per stream: "Stream <slot+1>: <resolvedUri>", "  <format>, <W>x<H>, <fps> fps, <N>
// frames" (or "unbounded"), "  colour: matrix <m>, range <r>, transfer <t>, primaries <p>" (each
// with " (assumed)" when ResolvedColour assumed it), "  tone-mapped (BT.2390, 1000 → 100 nits)"
// for PQ and HLG, and "  warning: <text>" per warning.
[[nodiscard]] std::string formatInfo(std::span<const StreamInfo> streams);

}  // namespace rawreel
