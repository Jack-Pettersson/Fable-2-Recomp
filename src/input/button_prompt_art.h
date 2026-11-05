// fable_2 - Drawing keyboard keys for the in-game button prompts
//
// Draws a key cap and writes the bound key on it with a system font, so any
// binding and any keyboard layout gets its own label.

#pragma once

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "button_prompt_textures.h"
#include "button_prompt_types.h"

namespace fable2::prompts {

// Every replaced texture by bank entry name, abyx included.
using PromptArt = std::map<std::string, Image>;

// Everything that shows a controller button, drawn with these labels. The
// original banks supply the sheets that mix buttons with other art. Returns
// nothing if no font could be loaded.
std::optional<PromptArt> RenderPromptArt(const PromptLabels& labels, std::span<const uint8_t> textures,
                                         std::span<const uint8_t> headers);

}  // namespace fable2::prompts
