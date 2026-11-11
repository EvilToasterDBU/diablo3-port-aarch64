#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
DM_ROOT=${DM_ROOT:-"$HOME/dm-work"}
BUILD_DIR=${DM_BUILD_DIR:-"$DM_ROOT/build/linux/arm64-cmake"}
APPDIR=${DM_APPDIR:-"$DM_ROOT/build/linux/AppDir"}
TOOLS_DIR=${DM_APPIMAGE_TOOLS:-"$DM_ROOT/build/linux/tools"}
OUTPUT=${1:-"$DM_ROOT/build/linux/dist/diablo3-port-aarch64.AppImage"}
TOOL="$TOOLS_DIR/appimagetool-aarch64.AppImage"
RUNTIME="$TOOLS_DIR/runtime-aarch64"

if [ "$(uname -m)" != aarch64 ]; then
    echo "Build the ARM64 AppImage inside an aarch64 environment (for example: distrobox enter ubuntu-arm64)." >&2
    exit 1
fi
for tool in cmake ninja pkg-config curl; do
    command -v "$tool" >/dev/null 2>&1 || { echo "Missing build tool: $tool" >&2; exit 1; }
done

mkdir -p "$TOOLS_DIR" "$(dirname -- "$OUTPUT")"
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD_DIR" --target dmhost -j"${DM_JOBS:-4}"

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"
cp "$SCRIPT_DIR/AppRun" "$APPDIR/AppRun"
cp "$SCRIPT_DIR/diablo3-port.desktop" "$APPDIR/diablo3-port.desktop"
cp "$SCRIPT_DIR/diablo3-port.svg" "$APPDIR/diablo3-port.svg"
cp "$SCRIPT_DIR/diablo3-port.png" "$APPDIR/diablo3-port.png"
FONTDIR="$APPDIR/usr/share/diablo3-port/fonts"
mkdir -p "$FONTDIR"
cp "$SCRIPT_DIR/assets/DejaVuSans.ttf" "$FONTDIR/DejaVuSans.ttf"
cp "$SCRIPT_DIR/assets/DejaVuSans-LICENSE.txt" "$FONTDIR/DejaVuSans-LICENSE.txt"
chmod +x "$APPDIR/AppRun"

# A personal build can carry the user-generated Linux runtime. It deliberately
# excludes RomFS/game data and keys; those stay in the user's data directory.
if [ "${DM_BUNDLE_RUNTIME:-0}" = 1 ]; then
    RUNTIME_PACK=${DM_RUNTIME_PACK:-"$DM_ROOT/Diablo3Mobile/dm"}
    for part in out-linux shaders; do
        if [ ! -d "$RUNTIME_PACK/$part" ]; then
            printf 'Cannot bundle runtime: missing %s\n' "$RUNTIME_PACK/$part" >&2
            exit 1
        fi
    done
    BUNDLED_RUNTIME="$APPDIR/usr/share/diablo3-port/runtime"
    mkdir -p "$BUNDLED_RUNTIME"
    cp -a "$RUNTIME_PACK/out-linux" "$RUNTIME_PACK/shaders" "$BUNDLED_RUNTIME/"
    printf 'Bundled private Linux modules and shaders from %s (RomFS and keys remain external)\n' "$RUNTIME_PACK"
fi

# Bundle the application and SDL runtime libraries while leaving glibc,
# libstdc++, the Vulkan loader, and GPU/display driver libraries to Armada.
LIBDIR="$APPDIR/usr/lib/aarch64-linux-gnu"
mkdir -p "$LIBDIR"
ldd "$APPDIR/usr/bin/dmhost" | awk '$2 == "=>" && $3 ~ /^\// {print $3} $1 ~ /^\// {print $1}' |
while IFS= read -r lib; do
    [ -f "$lib" ] || continue
    name=$(basename -- "$lib")
    case "$name" in
        ld-linux*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libresolv.so*|libgcc_s.so*|libstdc++.so*|libvulkan.so*|libdrm.so*|libgbm.so*|libwayland-*.so*|libxkbcommon.so*|libdecor-*.so*|libX*.so*|libxcb*.so*|libEGL.so*|libGL.so*) continue ;;
    esac
    [ -e "$LIBDIR/$name" ] || cp -L "$lib" "$LIBDIR/$name"
done

# Runtime manifests generated from a user's game files may have private umask
# permissions. AppImage extraction runs as the handheld user, so every bundled
# runtime file must be readable by that user.
chmod -R a+rX "$APPDIR"

if [ ! -x "$TOOL" ]; then
    curl -fL "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-aarch64.AppImage" -o "$TOOL"
    chmod +x "$TOOL"
fi
if [ ! -x "$RUNTIME" ]; then
    curl -fL "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-aarch64" -o "$RUNTIME"
    chmod +x "$RUNTIME"
fi

if [ "${DM_APPIMAGE_STAGE_ONLY:-0}" = 1 ]; then
    printf 'Staged ARM64 AppDir and packaging tools under %s\n' "$DM_ROOT/build/linux"
    exit 0
fi

if command -v distrobox-host-exec >/dev/null 2>&1; then
    distrobox-host-exec env APPIMAGE_EXTRACT_AND_RUN=1 ARCH=aarch64 qemu-aarch64-static \
        "$TOOL" --runtime-file "$RUNTIME" "$APPDIR" "$OUTPUT"
else
    APPIMAGE_EXTRACT_AND_RUN=1 ARCH=aarch64 "$TOOL" --runtime-file "$RUNTIME" "$APPDIR" "$OUTPUT"
fi
chmod +x "$OUTPUT"
printf 'Built ARM64 AppImage: %s\n' "$OUTPUT"
