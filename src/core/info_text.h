#pragma once

// The Info pane text (design M3 v2 §8.4 layer 1; D4 "tone-mapped" line): wx-free, so it is unit
// tested without the GUI.

#include <span>
#include <string>

#include "core/session_types.h"

namespace rawreel {

// Lines joined with '\n'. No streams: "No stream. Open a file (Ctrl+O) or a URI (Ctrl+U), or drop a
// file here." Per stream: "Stream <slot+1>: <resolvedUri>", "  <format>, <W>x<H>, <fps> fps, <N>
// frames" (or "unbounded"), D5R §4.5 "  colour: <standard>[ <curve>]<origin>" (no origin for the
// URI's colour=; "no standard (from file)" for a file colour that matches no standard), "  matrix
// <m>, range <r>[<origin>], curve <t>, primaries <p> (not converted)" (the range's origin only when
// it differs from line 1's, never another field's; "matrix not used" on RGB and gray), "  hint:
// <dvv::media::sizeHint>" for a default or URI standard that does not fit the height,
// "  tone-mapped (BT.2390, 1000 → 100 nits)" for PQ and HLG, "  clip description: <file>",
// "  reads: <SourceInfo::reader>" for file sources (e.g. "unbuffered", "buffered (fallback:
// <reason>)"), and "  warning: <text>" per warning.
[[nodiscard]] std::string formatInfo(std::span<const StreamInfo> streams);

}  // namespace rawreel
