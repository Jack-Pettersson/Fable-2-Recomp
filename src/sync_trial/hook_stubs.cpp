// Sync trial: no-op stand-ins for the manifest's mid-asm hooks.
// Each leaves its registers unchanged and never jumps or returns, so the game
// runs as if the patch were not there. The real bodies are in src/core/fable2_hooks.cpp.
#include "generated/default/fable_2_init.h"

void fable2_hook_website_g1(PPCRegister& /*r9*/) { }
void fable2_hook_website_g1b(PPCRegister& /*r8*/) { }
void fable2_hook_ce_g1(PPCRegister& /*r10*/) { }
void fable2_hook_ce_g1b(PPCRegister& /*r9*/) { }
void fable2_hook_website_grantnew(PPCRegister& /*r3*/) { }
void fable2_hook_disable_motion_blur(PPCRegister& /*f12*/) { }
void fable2_hook_direct_mouse_look(PPCRegister& /*r31*/) { }
void fable2_hook_ce_grantavail(PPCRegister& /*r3*/) { }
bool fable2_hook_high_tick_rate_skip_store() { return false; }
bool fable2_hook_high_hf_tick_rate_skip_store() { return false; }
bool fable2_hook_gui_every_tick() { return false; }
bool fable2_hook_address_scan_skip(PPCRegister& /*r26*/, PPCRegister& /*r27*/, PPCRegister& /*r28*/, PPCRegister& /*r31*/) { return false; }
void fable2_hook_skip_intro_videos(PPCRegister& /*r3*/) { }
void fable2_hook_realtime_texture_morphing(PPCRegister& /*r9*/) { }
