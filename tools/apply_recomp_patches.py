#!/usr/bin/env python3
"""Apply recomp-level patches to codegen's output (generated/default/*.cpp).

Recomps run NATIVE code, so a Xenia code-region patch (a guest .text byte
change) has no runtime effect on its own. PREFERRED mechanism: the SDK's
mid-asm hooks ([[entrypoint.midasm_hook]] in fable_2_manifest.toml + a hook
function in src/fable2_hooks.cpp) - see docs/patches.md. This script is the
fallback for patches that cannot be expressed as hooks (e.g. constants
baked into memory operands). It runs as a build step between codegen and
compiling fable_2_recomp (wired in CMakeLists.txt), keyed on
codegen.build.stamp, so patches survive codegen re-runs.

Patches are applied as anchored text replacements with a unique marker
comment (idempotent: already-patched files are left alone). Missing anchors
are a hard error - a silently-skipped patch is worse than a failed build.

Set FABLE2_RECOMP_PATCHES=0 to skip all patches (used for A/B runs).
"""

import os
import sys

GENERATED_DIR = sys.argv[1] if len(sys.argv) > 1 else "generated/default"

# Each patch: (name, anchor, replacement). The anchor must occur exactly once
# per generated file that contains the target function. The replacement ends
# with the patch's marker comment, which is the idempotency check.
# Patches that cannot be expressed as a [[entrypoint.midasm_hook]] (see
# docs/patches.md).
# Sync trial: upstream 0.11.0 codegen emits a fall-through into a manifest
# function that starts with a bl as a call to the bl's target (FindCallTarget
# matches the bl's call site). The epilogue is skipped and r1/r31 come back wrong.
PATCHES = [
    ('upstream-fallthru-sub_82CD17D8',
     '\t// stw r11,16(r30)\n\tREX_STORE_U32(ctx.r30.u32 + 16, ctx.r11.u32);\n\tSetString_822F2020(ctx, base);\n\treturn;\n}\n',
     '\t// stw r11,16(r30)\n\tREX_STORE_U32(ctx.r30.u32 + 16, ctx.r11.u32);\n\t// [recomp-patch: upstream-fallthru-sub_82CD17D8] upstream 0.11.0 resolves this fall-through via the bl at 0x82CD182C; call the function there\n\tREX_CALL_INDIRECT_FUNC(0x82CD182C);\n\treturn;\n}\n'),
    ('upstream-fallthru-sub_82C5ABB0',
     '\t// li r4,40\n\tctx.r4.s64 = 40;\n\tsub_82CA8210(ctx, base);\n\treturn;\nloc_82C5AC54:\n',
     '\t// li r4,40\n\tctx.r4.s64 = 40;\n\t// [recomp-patch: upstream-fallthru-sub_82C5ABB0] upstream 0.11.0 resolves this fall-through via the bl at 0x82C5AC4C; call the function there\n\tREX_CALL_INDIRECT_FUNC(0x82C5AC4C);\n\treturn;\nloc_82C5AC54:\n'),
    ('upstream-fallthru-sub_82C59670',
     '\t// beq cr6,0x82c596a0\n\tif (ctx.cr6.eq) goto loc_82C596A0;\n\tsub_82C597F0(ctx, base);\n\treturn;\nloc_82C596A0:\n',
     '\t// beq cr6,0x82c596a0\n\tif (ctx.cr6.eq) goto loc_82C596A0;\n\t// [recomp-patch: upstream-fallthru-sub_82C59670] upstream 0.11.0 resolves this fall-through via the bl at 0x82C59698; call the function there\n\tREX_CALL_INDIRECT_FUNC(0x82C59698);\n\treturn;\nloc_82C596A0:\n'),
    ('upstream-fallthru-sub_82CA7C30',
     '\t// mr r8,r8\n\tctx.r8.u64 = ctx.r8.u64;\n\tProcessAndTlsAndProcess3_82CAC610(ctx, base);\n\treturn;\n}\n',
     '\t// mr r8,r8\n\tctx.r8.u64 = ctx.r8.u64;\n\t// [recomp-patch: upstream-fallthru-sub_82CA7C30] upstream 0.11.0 resolves this fall-through via the bl at 0x82CA7C70; call the function there\n\tREX_CALL_INDIRECT_FUNC(0x82CA7C70);\n\treturn;\n}\n'),
    ('upstream-fallthru-sub_830F0880',
     '\t// mr r8,r8\n\tctx.r8.u64 = ctx.r8.u64;\n\tGetGlobalPtr3_82240578(ctx, base);\n\treturn;\nloc_830F0990:\n',
     '\t// mr r8,r8\n\tctx.r8.u64 = ctx.r8.u64;\n\t// [recomp-patch: upstream-fallthru-sub_830F0880] upstream 0.11.0 resolves this fall-through via the bl at 0x830F0914; call the function there\n\tREX_CALL_INDIRECT_FUNC(0x830F0914);\n\treturn;\nloc_830F0990:\n'),
    ('upstream-fallthru-sub_82CBA740',
     '\t// mr r8,r8\n\tctx.r8.u64 = ctx.r8.u64;\n\tsub_82CBB0C0(ctx, base);\n\treturn;\n}\n',
     '\t// mr r8,r8\n\tctx.r8.u64 = ctx.r8.u64;\n\t// [recomp-patch: upstream-fallthru-sub_82CBA740] upstream 0.11.0 resolves this fall-through via the bl at 0x82CBA76C; call the function there\n\tREX_CALL_INDIRECT_FUNC(0x82CBA76C);\n\treturn;\n}\n'),
    ('upstream-fallthru-sub_82CD1880',
     '\t// stw r11,0(r30)\n\tREX_STORE_U32(ctx.r30.u32 + 0, ctx.r11.u32);\n\tsub_82CD1608(ctx, base);\n\treturn;\n}\n',
     '\t// stw r11,0(r30)\n\tREX_STORE_U32(ctx.r30.u32 + 0, ctx.r11.u32);\n\t// [recomp-patch: upstream-fallthru-sub_82CD1880] upstream 0.11.0 resolves this fall-through via the bl at 0x82CD18AC; call the function there\n\tREX_CALL_INDIRECT_FUNC(0x82CD18AC);\n\treturn;\n}\n'),
    ('upstream-fallthru-sub_82C5DA18',
     '\t// addi r3,r31,96\n\tctx.r3.s64 = ctx.r31.s64 + 96;\n\tsub_82C60E18(ctx, base);\n\treturn;\nloc_82C5DA9C:\n',
     '\t// addi r3,r31,96\n\tctx.r3.s64 = ctx.r31.s64 + 96;\n\t// [recomp-patch: upstream-fallthru-sub_82C5DA18] upstream 0.11.0 resolves this fall-through via the bl at 0x82C5DA68; call the function there\n\tREX_CALL_INDIRECT_FUNC(0x82C5DA68);\n\treturn;\nloc_82C5DA9C:\n'),
]

MARKER = "// [recomp-patch: "


def main() -> int:
    if os.environ.get("FABLE2_RECOMP_PATCHES") == "0":
        print("[recomp-patches] FABLE2_RECOMP_PATCHES=0 - skipping")
        return 0

    files = sorted(
        f for f in os.listdir(GENERATED_DIR) if f.startswith("fable_2_recomp.") and f.endswith(".cpp")
    )
    if not files:
        print(f"[recomp-patches] ERROR: no fable_2_recomp.*.cpp in {GENERATED_DIR}", file=sys.stderr)
        return 1

    applied_total = 0
    for name, anchor, replacement in PATCHES:
        marker = MARKER + name + "]"  # MARKER ends with a space
        found = False
        for fname in files:
            path = os.path.join(GENERATED_DIR, fname)
            with open(path, "rb") as f:
                data = f.read().decode("utf-8")
            if marker in data:
                found = True  # already applied (idempotent)
                continue
            if anchor not in data:
                continue
            data = data.replace(anchor, replacement, 1)
            with open(path, "wb") as f:
                f.write(data.encode("utf-8"))
            found = True
            print(f"[recomp-patches] applied '{name}' in {fname}")
            applied_total += 1
        if not found:
            print(f"[recomp-patches] ERROR: anchor for '{name}' not found in any "
                  f"{len(files)} file(s) under {GENERATED_DIR}", file=sys.stderr)
            return 1

    print(f"[recomp-patches] done ({applied_total} applied now, rest already present)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
