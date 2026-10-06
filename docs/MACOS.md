# macOS (x86_64, Rosetta 2) — experimental

bbport runs the game's original x86-64 code in its own process, so on a Mac the whole program
is an x86_64 build: native on Intel, translated by Rosetta 2 on Apple silicon. Rendering goes
through Vulkan on Metal with Mesa's **KosmicKrisp** driver, in the x86_64 build that shadPS4's
macOS release ships. The KosmicKrisp of LunarG's Vulkan SDK and of Homebrew is arm64-only, and
an x86_64 process cannot load it.

**Status:** GitHub Actions (`.github/workflows/macos.yml`) builds the port and runs its tests on
an Intel Mac and on an Apple silicon Mac, where everything runs as described below (x86_64
under Rosetta 2): the runtime tests (guest TLS, guest memory, locks, semaphores, files), the
GPU library tests that need no GPU, and the Python tests, which also start `bb-probe` on small
synthetic x86-64 images. The game itself has **not been run on a Mac yet** — reports (logs, see
the end) are what this stage needs.

## Requirements

- A Mac with Apple silicon (M1 or newer) and **macOS 26** or later: KosmicKrisp is built on
  Metal 4. Intel Macs can build the port natively, but KosmicKrisp does not support them.
  Rosetta 2 runs any x86_64 program through macOS 27; Apple keeps only a subset of it, for
  older games, from macOS 28 on.
- Rosetta 2, and Xcode 26 or later or its command line tools (as for shadPS4: the renderer
  needs `std::jthread` from the C++ library of Xcode 26). Their `python3` (3.9) runs the
  preparation scripts.
- The **x86_64 Homebrew** in `/usr/local` (next to an arm64 Homebrew in `/opt/homebrew`, if
  you have one). Libraries for an x86_64 program must be x86_64. Homebrew 7 made x86_64 macOS a
  Tier 3 platform (September 2026: no new Intel bottles, removal in September 2027), and its
  installer no longer sets it up; `tools/macos_homebrew_x86_64.sh` does.
- An **x86_64 KosmicKrisp**: the two driver files of shadPS4's macOS release (below), or a
  build of Mesa's KosmicKrisp for x86_64 (shadPS4 builds it with
  [shadexternals/mesa-kosmickrisp](https://github.com/shadexternals/mesa-kosmickrisp)).
- Your dump of Bloodborne CUSA03173, version 1.09.

## Setup

```bash
softwareupdate --install-rosetta --agree-to-license
xcode-select --install
git clone --recursive https://github.com/wuwei3618/bloodborne_pc bbport && cd bbport
git checkout macos-port

# x86_64 Homebrew in /usr/local (asks for your password), then the libraries
bash tools/macos_homebrew_x86_64.sh
arch -x86_64 /usr/local/bin/brew install cmake ninja pkgconf glslang vulkan-headers vulkan-loader \
    sdl3 ffmpeg boost fmt magic_enum robin-map xxhash zydis miniz xbyak
```

## Build and run

```bash
bash build.sh                          # switches to x86_64 itself on Apple silicon

# The x86_64 KosmicKrisp of shadPS4's macOS release, next to bb-probe
curl -LO https://github.com/shadps4-emu/shadPS4/releases/download/v.0.19.0/shadps4-macos-sdl-0.19.0.zip
mkdir -p out/vulkan/icd.d
unzip -j shadps4-macos-sdl-0.19.0.zip kosmickrisp_mesa_icd.json libvulkan_kosmickrisp.dylib -d out/vulkan/icd.d
out/bb-probe --vulkan-only             # Vulkan without the game: device name and PASS

BB_GAME_DIR=/path/to/CUSA03173 bash run.sh 2>&1 | tee bbport-macos.log
```

`bb-probe` uses the driver in `out/vulkan/icd.d` unless `VK_DRIVER_FILES` names a driver
manifest. A browser download is quarantined, a `curl` download is not; after a browser download
run `xattr -dr com.apple.quarantine out/vulkan`. Settings are in `bbport.ini` (the GTK launcher
is not ported). The in-game menu opens with **Cmd+,** (or L3+R3 on a gamepad).

## Differences from Linux

- FSR 4 and FSR 4.1.1 need AMD-specific Vulkan features and fall back to FSR 3.1; TAA and
  FSR 3.1 are the upscalers to try.
- Guest memory uses POSIX shared memory and a reservation of the guest address range at start;
  host objects the game can see come from an allocator below 1 TiB (`src/runtime_heap.c`).
- The game's thread pointer is read from a pthread TSD slot (macOS keeps GS for itself).
- Helper threads run at the utility QoS class instead of `SCHED_IDLE`; frame statistics do not
  include the GPU command thread's CPU time.

## Reporting

Send `bbport-macos.log` and the output of

```bash
out/bb-probe --vulkan-only
sysctl -n machdep.cpu.brand_string; sw_vers
```

If the game stops with a fault, the log ends with the guest offset and a thread dump; with
`BB_TIMEOUT=60` the watchdog dumps all threads after 60 seconds (a hang).
