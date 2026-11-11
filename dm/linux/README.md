# Native ARM64 Linux port

This target adapts the Diablo III Switch guest to native ARM64 Linux (glibc), SDL2, and Vulkan for the Retroid Pocket 6 on ArmadaOS. The Linux loader probes glibc's inline pthread TSD slots before starting the guest; `DM_TARGET=linux` patches are checked against both game modules.

## Build the personal runtime

The repository's setup script reads the owner's NSPs and `prod.keys`, then writes decrypted runtime files only under `work/Diablo3Mobile`:

```sh
./setup.sh --target linux "/path/to/Diablo 3 [NSP]"
```

The Linux executable and translated shaders are at `work/Diablo3Mobile/dm/out-linux` and `work/Diablo3Mobile/dm/shaders`; the merged RomFS and DLC packs are in `romfs` and `aoc`. The setup keeps keys in memory and does not copy them. When separate setup processes share one work directory, the shader stage now takes an exclusive lock and rejects incomplete job caches before translating.

Build a personal ARM64 AppImage in the provided AArch64 Distrobox:

```sh
DM_ROOT="$PWD/work" DM_BUNDLE_RUNTIME=1 \
  distrobox enter ubuntu-arm64 -- env DM_ROOT="$PWD/work" DM_BUNDLE_RUNTIME=1 \
  sh "$PWD/dm/linux/build_appimage.sh"
```

The AppImage bundles the Linux executable, its SDL and FreeType libraries, DejaVu Sans for the in-window text dialog, patched modules, and Vulkan shader bundles. It does not contain the game RomFS, DLC, or keys. Keep the generated game folder private.

## Install on ArmadaOS

Copy the prepared game data and AppImage over SSH:

```sh
rsync -a --info=progress2 work/Diablo3Mobile/ armada@192.168.1.48:dm-work/Diablo3Mobile/
ssh armada@192.168.1.48 'mkdir -p ~/Games'
scp work/build/linux/dist/diablo3-port-aarch64.AppImage armada@192.168.1.48:Games/
ssh armada@192.168.1.48 'chmod +x ~/Games/diablo3-port-aarch64.AppImage'
```

Launch `~/Games/diablo3-port-aarch64.AppImage` from ArmadaOS. The app still reads game data under `~/dm-work/Diablo3Mobile`. User settings persist in `~/.config/diablo3-port/settings.conf`; game options that need guest startup state apply on the next launch.

The gamepad stays native: the Linux front end adds no virtual gameplay buttons. One gear button is drawn directly into the game's Vulkan swapchain. Its settings panel is also drawn in the same game window. The gear can be hidden from that panel; open or close the panel with `Esc` or `L3+R3` at any time. While the panel is open, gameplay input is paused. Keyboard arrows/Enter, gamepad D-pad/A/B, and touch are supported for navigating it. When the game requests text (such as a new hero's name), a compact text dialog appears in the same window; open the console keyboard with `Steam+X`, then confirm with `Start`/Enter or cancel with `B`/`Esc`.

The panel exposes game language, handheld/docked mode, aspect handling, loading-screen stretch, sharpening, 30/60/120 FPS cap, left-stick dead zone, gear visibility, gear size, and the ArmadaOS Vulkan driver status. Linux-only items that have no host implementation (Android custom-driver packages, haptics, and virtual-stick layout/edit controls) are not exposed.

The executable has built for AArch64, and both guest modules passed the Linux TLS equivalence check. Rendering, audio, touch, controller mappings, and save behavior still need an on-device play session; SSH connectivity alone does not validate those paths.
