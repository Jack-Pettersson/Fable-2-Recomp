@echo off
rem Matched Windows x64 runtime and D3D12 plugin. Optional second arg: Debug.
rem Run in a VS x64
rem developer shell with LLVM 20+, Ninja, CMake and Python on PATH.
setlocal
set "ROOT=%~dp0.."
set "SOURCE=%ROOT%\thirdparty\rexglue-sdk"
set "BUILD=%ROOT%\out\build\runtime-sdk"
set "STAGED=%ROOT%\out\tooling\runtime-sdk\win-amd64"
set "OFFICIAL_ARG=%~1"
set "OFFICIAL=%~1"
set "CONFIGURATION=%~2"
if not defined CONFIGURATION set "CONFIGURATION=Release"
if /i "%CONFIGURATION%"=="Debug" (
  set "CONFIGURATION=Debug"
  set "BUILD=%ROOT%\out\build\runtime-sdk-debug"
  set "STAGED=%ROOT%\out\tooling\runtime-sdk-debug\win-amd64"
) else if /i "%CONFIGURATION%"=="Release" (
  set "CONFIGURATION=Release"
) else (
  echo Error: runtime configuration must be Debug or Release. 1>&2
  exit /b 1
)
if not defined OFFICIAL set "OFFICIAL=%ROOT%\out\tooling\rexglue-sdk-0.10.0\win-amd64"
if not exist "%SOURCE%\CMakeLists.txt" (
  git -C "%ROOT%" submodule update --init thirdparty/rexglue-sdk || exit /b 1
)
rem Auto-fetch the official prebuilt SDK only when using the default location.
if not defined OFFICIAL_ARG if not exist "%OFFICIAL%\bin\rexglue.exe" (
  call "%~dp0setup_sdk.cmd" || exit /b 1
)
python "%~dp0prepare_runtime_sdk.py" "%SOURCE%" || exit /b 1
rem Release builds use ThinLTO, plus profile-guided optimization when a profile
rem is present (docs/SDK_PGO.md): +11-14% FPS where the GPU thread is the limit.
rem FABLE2_SDK_PGO=generate builds instrumented DLLs to record a new profile.
set "OPT_FLAGS="
set "LINK_FLAGS="
if /i "%CONFIGURATION%"=="Release" (
  set "OPT_FLAGS=-flto=thin"
  set "LINK_FLAGS=-flto=thin -fuse-ld=lld"
  if /i "%FABLE2_SDK_PGO%"=="generate" (
    set "OPT_FLAGS=-flto=thin -fprofile-instr-generate"
    set "LINK_FLAGS=-flto=thin -fuse-ld=lld -fprofile-instr-generate"
  ) else if exist "%~dp0pgo\rexglue-sdk.profdata" (
    for %%I in ("%~dp0pgo\rexglue-sdk.profdata") do set "OPT_FLAGS=-flto=thin -fprofile-instr-use=%%~sI -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"
  )
)
rem -g: always emit ReXGlue PDBs (Release + -g keeps production codegen but
rem produces full-symbol PDBs the fable_2 build stages next to the exe).
cmake -S "%SOURCE%" -B "%BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=%CONFIGURATION% -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ^
  -DCMAKE_C_FLAGS="-march=x86-64-v2 -g %OPT_FLAGS%" -DCMAKE_CXX_FLAGS="-march=x86-64-v2 -g %OPT_FLAGS%" ^
  -DCMAKE_SHARED_LINKER_FLAGS="%LINK_FLAGS%" -DCMAKE_EXE_LINKER_FLAGS="%LINK_FLAGS%" ^
  -DREXGLUE_USE_D3D12=ON -DREXGLUE_USE_VULKAN=OFF ^
  -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_ENABLE_FIDELITYFX=OFF ^
  -DREXGLUE_BUILD_TESTS=OFF || exit /b 1
cmake --build "%BUILD%" --target rexruntime rexgpu-xenos || exit /b 1
python "%~dp0stage_renderer_sdk.py" "%OFFICIAL%" "%SOURCE%" "%BUILD%" "%STAGED%" --configuration %CONFIGURATION% || exit /b 1
exit /b 0
