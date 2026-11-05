// fable_2 - Patched GUI banks for keyboard button prompts
//
// The game re-reads GUI textures from disk during play, so the prompts are
// changed where it reads them. At startup the two GUI texture banks are
// patched for the keyboard map and written to cache\button_prompts; the
// file-open calls in button_prompt_hooks.cpp then point the game's opens of
// data\art\gui\<bank> there. The game data itself is never written.
//
// Prepare() must run before the runtime sets up the guest file system, which
// lists the folders on disk once at startup.

#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <system_error>

#include <rex/logging/macros.h>

#include "button_prompt_art.h"
#include "button_prompt_labels.h"

namespace fable2::prompts {

namespace files_detail {

inline std::optional<Bytes> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  return Bytes(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// Unchanged files are left alone, so most launches write nothing.
inline bool WriteIfChanged(const std::filesystem::path& path, const Bytes& bytes) {
  if (auto existing = ReadFile(path); existing && *existing == bytes) return true;
  std::filesystem::path temp = path;
  temp += ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()))) return false;
  }
  std::error_code ec;
  std::filesystem::rename(temp, path, ec);
  return !ec;
}

}  // namespace files_detail

inline std::filesystem::path PatchedFolder(const std::filesystem::path& game_root) {
  return game_root / "cache" / "button_prompts";
}

// Patch the GUI banks for this keyboard map, or remove old patched banks when
// the setting is off.
inline void Prepare(const std::filesystem::path& game_root, bool enabled, std::string_view keyboard_map,
                    bool mouse_look) {
  using namespace files_detail;
  const std::filesystem::path out = PatchedFolder(game_root);
  std::error_code ec;
  if (!enabled) {
    std::filesystem::remove_all(out, ec);
    return;
  }

  const std::filesystem::path gui = game_root / "data" / "art" / "gui";
  auto textures = ReadFile(gui / kTexturesBank);
  auto headers = ReadFile(gui / kHeadersBank);
  if (!textures || !headers) {
    REXSYS_WARN("[button-prompts] GUI texture banks not found in {}; keeping controller prompts", gui.string());
    return;
  }
  auto art = RenderPromptArt(LabelsFor(input_detail::ParseMap(keyboard_map), mouse_look), *textures, *headers);
  if (!art) {
    REXSYS_WARN("[button-prompts] no font found to draw keys; keeping controller prompts");
    return;
  }
  auto patched = PatchBanks(*textures, *headers, *art);
  if (!patched) {
    REXSYS_WARN("[button-prompts] unsupported GUI texture banks; keeping controller prompts");
    return;
  }
  std::filesystem::create_directories(out, ec);
  if (!WriteIfChanged(out / kTexturesBank, patched->textures) ||
      !WriteIfChanged(out / kHeadersBank, patched->headers)) {
    REXSYS_WARN("[button-prompts] could not write {}; keeping controller prompts", out.string());
    return;
  }
  Active() = true;
  REXSYS_INFO("[button-prompts] showing keyboard keys on {} prompt textures", art->size());
}

}  // namespace fable2::prompts
