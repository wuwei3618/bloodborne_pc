# macOS (x86_64, Rosetta 2) — experimental

bbport runs the game's original x86-64 code in its own process, so on a Mac the whole program
is an x86_64 build: native on Intel, translated by Rosetta 2 on Apple silicon. Rendering goes
through Vulkan on Metal with Mesa's **KosmicKrisp** driver, in the x86_64 build that shadPS4's
macOS release ships. The KosmicKrisp of LunarG's Vulkan SDK and of Homebrew is arm64-only, and
an x86_64 process cannot load it.

**Status:** GitHub Actions (`.github/workflows/macos.yml`) builds the port on an Intel Mac, runs
its tests there and packages the build; an Apple silicon Mac without any x86_64 libraries then
runs the packaged programs and tests under Rosetta 2. The tests cover the runtime (guest TLS,
guest memory, locks, semaphores, files), the GPU library parts that need no GPU, and the loader
on small synthetic x86-64 images. The game itself has **not been run on a Mac yet** — reports
(logs, see the end) are what this stage needs.

## Requirements

- A Mac with Apple silicon (M1 or newer) and **macOS 26** or later: KosmicKrisp is built on
  Metal 4. Rosetta 2 runs any x86_64 program through macOS 27; Apple keeps only a subset of
  it, for older games, from macOS 28 on.
- Python 3.9 or newer for the preparation scripts: `xcode-select --install` provides one.
- Your dump of Bloodborne CUSA03173, version 1.09.

## The prebuilt package

Download the artifact **bbport-macos-x86_64** of the latest successful run of the macOS
workflow on the `macos-port` branch (GitHub: Actions → macOS → the run → Artifacts; you must be
signed in). It holds `bb-probe` and the GPU library with the libraries they use (`bin/lib`),
the x86_64 KosmicKrisp (`bin/vulkan/icd.d`), `run.sh` and its scripts.

```bash
softwareupdate --install-rosetta --agree-to-license
xcode-select --install                 # Python 3

cd ~/Downloads
unzip bbport-macos-x86_64.zip          # if Safari has not unpacked it already
tar -xzf bbport-macos-x86_64.tar.gz && cd bbport-macos-x86_64
xattr -dr com.apple.quarantine .       # downloaded programs are quarantined (not notarized)
bin/bb-probe --vulkan-only             # Vulkan without the game: device name and PASS

BB_GAME_DIR=/path/to/CUSA03173 bash bbport.sh 2>&1 | tee bbport-macos.log
```

The first start is slow: Rosetta 2 translates the programs once. Generated files, saves and
`bbport.ini` (settings; the GTK launcher is not ported) are kept in the package's folder. The
in-game menu opens with **Cmd+,** (or L3+R3 on a gamepad).

## Building from source

The build needs x86_64 libraries from an **x86_64 Homebrew** in `/usr/local`, next to an arm64
Homebrew in `/opt/homebrew` if you have one. Homebrew 7 made x86_64 macOS a Tier 3 platform
(September 2026; removal in September 2027): its installer no longer sets it up
(`tools/macos_homebrew_x86_64.sh` does), and the libraries updated since then have no Intel
bottles, so Homebrew compiles them, which takes a while. Xcode 26 or later, or its command line
tools, is needed (as for shadPS4: the renderer needs `std::jthread` from the C++ library of
Xcode 26).

```bash
softwareupdate --install-rosetta --agree-to-license
xcode-select --install
git clone --recursive https://github.com/wuwei3618/bloodborne_pc bbport && cd bbport
git checkout macos-port

bash tools/macos_homebrew_x86_64.sh    # asks for your password
arch -x86_64 /usr/local/bin/brew install cmake ninja pkgconf glslang vulkan-headers vulkan-loader \
    sdl3 ffmpeg boost fmt magic_enum robin-map xxhash zydis miniz xbyak
bash build.sh                          # switches to x86_64 itself on Apple silicon
bash packaging/macos_package.sh        # optional: dist/bbport-macos-x86_64.tar.gz, as above

# Or run from the source tree, with the x86_64 KosmicKrisp of shadPS4's macOS release
curl -LO https://github.com/shadps4-emu/shadPS4/releases/download/v.0.19.0/shadps4-macos-sdl-0.19.0.zip
mkdir -p out/vulkan/icd.d
unzip -j shadps4-macos-sdl-0.19.0.zip kosmickrisp_mesa_icd.json libvulkan_kosmickrisp.dylib -d out/vulkan/icd.d
BB_GAME_DIR=/path/to/CUSA03173 bash run.sh 2>&1 | tee bbport-macos.log
```

`bb-probe` uses the driver in `vulkan/icd.d` next to it unless `VK_DRIVER_FILES` names a driver
manifest. KosmicKrisp can also be built for x86_64 from Mesa, as shadPS4 does with
[shadexternals/mesa-kosmickrisp](https://github.com/shadexternals/mesa-kosmickrisp).

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
bin/bb-probe --vulkan-only             # out/bb-probe in a source tree
sysctl -n machdep.cpu.brand_string; sw_vers
```

If the game stops with a fault, the log ends with the guest offset and a thread dump; with
`BB_TIMEOUT=60` the watchdog dumps all threads after 60 seconds (a hang).
