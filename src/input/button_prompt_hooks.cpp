// fable_2 - Pointing the game's GUI bank opens at the patched copies
//
// Wraps the kernel's file-open calls (see button_prompt_files.h). When keyboard
// button prompts are on, a path ending in data\art\gui\<bank> is rewritten to
// cache\button_prompts\<bank> on the same device before the runtime's own
// function runs. Everything else passes straight through.

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <rex/logging/macros.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "button_prompt_types.h"

namespace {

using GuestFunction = void (*)(PPCContext&, uint8_t*);

// The runtime's own export, which these definitions replace in the link.
GuestFunction Original(const char* name) {
#ifdef _WIN32
  for (const char* module : {"rexruntime.dll", "rexruntimed.dll"}) {
    if (HMODULE handle = ::GetModuleHandleA(module)) {
      if (auto function = reinterpret_cast<GuestFunction>(::GetProcAddress(handle, name))) return function;
    }
  }
  return nullptr;
#else
  return reinterpret_cast<GuestFunction>(::dlsym(RTLD_NEXT, name));
#endif
}

uint32_t Load32(const uint8_t* base, uint32_t at) {
  const uint8_t* p = base + at;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void Store32(uint8_t* base, uint32_t at, uint32_t value) {
  uint8_t* p = base + at;
  p[0] = uint8_t(value >> 24);
  p[1] = uint8_t(value >> 16);
  p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

bool EndsWithFolded(std::string_view text, std::string_view suffix) {
  if (text.size() < suffix.size()) return false;
  text = text.substr(text.size() - suffix.size());
  for (size_t i = 0; i < suffix.size(); i++) {
    char c = text[i] == '/' ? '\\' : char(std::tolower(static_cast<unsigned char>(text[i])));
    if (c != suffix[i]) return false;
  }
  return true;
}

// Copies the object attributes below the caller's stack with the new path and
// returns their address, or 0 to leave the call alone.
uint32_t Redirect(PPCContext& ctx, uint8_t* base, uint32_t attributes) {
  if (!fable2::prompts::Active() || attributes == 0) return 0;
  uint32_t root = Load32(base, attributes);
  uint32_t name = Load32(base, attributes + 4);
  if ((root != 0 && root != 0xFFFFFFFD) || name == 0) return 0;
  uint32_t length = (Load32(base, name) >> 16) & 0xFFFF;
  uint32_t chars = Load32(base, name + 4);
  if (chars == 0 || length == 0 || length > 512) return 0;
  std::string_view path(reinterpret_cast<const char*>(base + chars), length);

  for (std::string_view bank : {fable2::prompts::kTexturesBank, fable2::prompts::kHeadersBank}) {
    const std::string suffix = std::string("data\\art\\gui\\") + std::string(bank);
    if (!EndsWithFolded(path, suffix)) continue;
    const size_t prefix = path.size() - suffix.size();
    if (prefix > 0 && path[prefix - 1] != '\\' && path[prefix - 1] != '/' && path[prefix - 1] != ':') return 0;
    std::string target = std::string(path.substr(0, prefix)) + "cache\\button_prompts\\" + std::string(bank);

    uint32_t scratch = (ctx.r1.u32 - 0x800) & ~0xFu;
    uint32_t string = scratch + 16, text = scratch + 32;
    std::memcpy(base + text, target.data(), target.size());
    base[text + target.size()] = 0;
    Store32(base, string, (uint32_t(target.size()) << 16) | uint32_t(target.size() + 1));
    Store32(base, string + 4, text);
    Store32(base, scratch, root);
    Store32(base, scratch + 4, string);
    Store32(base, scratch + 8, Load32(base, attributes + 8));

    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) REXSYS_INFO("[button-prompts] serving {} from {}", path, target);
    return scratch;
  }
  return 0;
}

}  // namespace

extern "C" REX_FUNC(__imp__NtCreateFile) {
  static const GuestFunction original = Original("__imp__NtCreateFile");
  if (uint32_t attributes = Redirect(ctx, base, ctx.r5.u32)) ctx.r5.u64 = attributes;
  original(ctx, base);
}

extern "C" REX_FUNC(__imp__NtOpenFile) {
  static const GuestFunction original = Original("__imp__NtOpenFile");
  if (uint32_t attributes = Redirect(ctx, base, ctx.r5.u32)) ctx.r5.u64 = attributes;
  original(ctx, base);
}

extern "C" REX_FUNC(__imp__NtQueryFullAttributesFile) {
  static const GuestFunction original = Original("__imp__NtQueryFullAttributesFile");
  if (uint32_t attributes = Redirect(ctx, base, ctx.r3.u32)) ctx.r3.u64 = attributes;
  original(ctx, base);
}
