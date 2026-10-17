#!/bin/bash
set -euo pipefail

MINGW_PREFIX="$(brew --prefix mingw-w64)"
SYSROOT="$MINGW_PREFIX/toolchain-i686"
MINGW_INCLUDES=("-I$SYSROOT/i686-w64-mingw32/include")

# Prefer Homebrew's LLVM if it's installed; fall back to whatever's on PATH.
if command -v brew &>/dev/null && brew --prefix llvm &>/dev/null; then
    LLVM_PREFIX="$(brew --prefix llvm)"
    CLANG="$LLVM_PREFIX/bin/clang"
    WINDRES="$LLVM_PREFIX/bin/llvm-windres"
    STRIP="$LLVM_PREFIX/bin/llvm-strip"
    OBJCOPY="$LLVM_PREFIX/bin/llvm-objcopy"
    echo "==> Using Homebrew LLVM at $LLVM_PREFIX"
else
    CLANG="${CLANG:-clang}"
    WINDRES="${WINDRES:-llvm-windres}"
    STRIP="${STRIP:-llvm-strip}"
    OBJCOPY="${OBJCOPY:-llvm-objcopy}"
    echo "==> Using system clang"
fi

COMMON=(
    --target=i686-w64-mingw32
    --sysroot="$SYSROOT"
    -Weverything -Wall -Wextra -Wpedantic -Werror -Weffc++
    -Wno-c++98-compat-pedantic -Wno-reserved-macro-identifier -Wno-nonportable-system-include-path
    -fno-builtin -fno-threadsafe-statics -ftrivial-auto-var-init=pattern
    -fno-exceptions -fno-rtti -fsafe-buffer-usage-suggestions
    -flto -Oz -std=c++23
    -g0
    -ffunction-sections -fdata-sections
    -nostdlib -nodefaultlibs -nostartfiles
    -fuse-ld=lld
    -Wl,-e,start
    -Wl,-subsystem,windows
    -Wl,--disable-reloc-section
    -Wl,--gc-sections
)

LIBS=(-lkernel32 -luser32 -lshell32 -ladvapi32 -lole32)

echo "==> Building BatteryWarn.exe"
"$WINDRES" "${MINGW_INCLUDES[@]}" battery-warn.rc -o battery-warn.res
"$CLANG" "${COMMON[@]}" battery-warn.cc toast.cc battery-warn.res -o BatteryWarn.exe "${LIBS[@]}"
"$STRIP" --strip-all BatteryWarn.exe

echo "==> Done: BatteryWarn.exe"
