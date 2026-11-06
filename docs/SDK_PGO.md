# SDK profile-guided optimization

Release builds of the ReXGlue runtime and GPU plugin (`tools/build_runtime_sdk.cmd`)
use ThinLTO, and profile-guided optimization when `tools/pgo/rexglue-sdk.profdata`
exists. The profile only holds execution counts for SDK code, no game data.

Measured on an RTX 3060 Ti with all of the render target and memexport switches on:

| Spot | Without | With |
|---|---|---|
| Bower Lake, 1x, uncapped (training spot) | 70.2 FPS | 80.5 FPS |
| Knothole Island interior, 2x, uncapped | 63.9 FPS | 71.2 FPS |

ThinLTO alone gave about 1.5%. `-march=x86-64-v3` would add about 3% more but
drops CPUs older than Haswell, so it is not used.

## Recording a new profile

Do this after an SDK pin bump with larger changes. An out-of-date profile still
works for unchanged functions; the compiler only warns, and those warnings are off.

1. `set FABLE2_SDK_PGO=generate`, then `build.cmd -release fable_2`.
2. `set LLVM_PROFILE_FILE=%CD%\out\pgo\sdk-%p-%m.profraw` and start the game from
   that shell. Load a save and play for a minute or two, outdoors and indoors.
3. Close the game normally (not from Task Manager), so the profile is written.
4. `llvm-profdata merge -o tools\pgo\rexglue-sdk.profdata out\pgo\*.profraw`
5. `set FABLE2_SDK_PGO=` and build again.
