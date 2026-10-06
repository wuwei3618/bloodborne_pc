# macOS (x86_64, Rosetta 2) — experimental

bbport runs the game's original x86-64 code in its own process, so on a Mac the whole program
is an x86_64 build: native on Intel, translated by Rosetta 2 on Apple silicon. Rendering goes
through Vulkan on Metal with LunarG's **KosmicKrisp** driver (the one shadPS4's macOS build
uses).

**Status:** a GitHub Actions job (`.github/workflows/macos.yml`, Intel runner) builds the port
and runs its runtime tests on macOS. The game itself has **not been run on a Mac yet** —
reports (logs, see the end) are what this stage needs.

## Requirements

- A Mac with Apple silicon (M1 or newer) and **macOS 26** or later: KosmicKrisp is built on
  Metal 4. Intel Macs can build the port natively, but KosmicKrisp does not support them.
- Rosetta 2, the Xcode command line tools, Python 3.
- The **x86_64 Homebrew** in `/usr/local` (next to an arm64 Homebrew in `/opt/homebrew`, if
  you have one). Libraries for an x86_64 program must be x86_64.
- The **Vulkan SDK for macOS** from LunarG (1.4.357.0 or newer), which includes KosmicKrisp
  as a universal (x86_64 + arm64) driver.
- Your dump of Bloodborne CUSA03173, version 1.09.

## Setup

```bash
softwareupdate --install-rosetta --agree-to-license
xcode-select --install

# x86_64 Homebrew (installs to /usr/local)
arch -x86_64 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
arch -x86_64 /usr/local/bin/brew install cmake ninja pkgconf glslang vulkan-headers vulkan-loader \
    sdl3 ffmpeg boost fmt magic_enum robin-map xxhash zydis miniz xbyak python@3
```

Install the Vulkan SDK with LunarG's installer into its default place (`~/VulkanSDK/<version>`);
a system-wide install would overwrite Homebrew's Vulkan files in `/usr/local`. Check that the
driver has an x86_64 slice:

```bash
lipo -info ~/VulkanSDK/*/macOS/lib/libvulkan_kosmickrisp.dylib
```

## Build and run

```bash
git clone --recursive https://github.com/wuwei3618/bloodborne_pc bbport && cd bbport
git checkout macos-port
bash build.sh                          # switches to x86_64 itself on Apple silicon
BB_GAME_DIR=/path/to/CUSA03173 bash run.sh 2>&1 | tee bbport-macos.log
```

`run.sh` uses the SDK's KosmicKrisp driver (`~/VulkanSDK/*/macOS/share/vulkan/icd.d`) unless
`VK_DRIVER_FILES` names a driver manifest. Settings are in `bbport.ini` (the GTK launcher is not
ported). The in-game menu opens with **Cmd+,** (or L3+R3 on a gamepad).

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
arch -x86_64 ~/VulkanSDK/*/macOS/bin/vulkaninfo --summary
sysctl -n machdep.cpu.brand_string; sw_vers
```

If the game stops with a fault, the log ends with the guest offset and a thread dump; with
`BB_TIMEOUT=60` the watchdog dumps all threads after 60 seconds (a hang).
