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
on small synthetic x86-64 images; the package's KosmicKrisp loads there too (the runner's
virtual GPU cannot run it). The game has run on a Mac (M5 Pro, macOS 27.2, October 2026, the
CI package of `macos-port`): it starts, plays its movies, and the character creator, the
opening and the first area work, with a DualSense. Menus run at 60 to 120 FPS, heavy scenes at
about 25 to 40 FPS: under Rosetta 2 the GPU command thread is the limit there. See
[Known issues](#known-issues); reports (logs, see the end) are still welcome.

## Requirements

- A Mac with Apple silicon (M1 or newer) and **macOS 26** or later: KosmicKrisp is built on
  Metal 4. Rosetta 2 runs any x86_64 program through macOS 27; Apple keeps only a subset of
  it, for older games, from macOS 28 on.
- Python 3.9 or newer for the preparation scripts: `xcode-select --install` provides one.
- Your dump of Bloodborne, version 1.09: CUSA03173, or an edition with the same 1.09 eboot
  (the Asian Old Hunters Edition, CUSA03023, was used on the Mac).

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

## Known issues

- The first time an area, an effect or a menu appears, its shaders and pipelines are compiled;
  on a Mac one pipeline can take up to about 2 s, so new areas stutter at first. KosmicKrisp
  keeps no disk cache of its own: the port saves the pipelines it used (`user/cache`) and
  compiles them again at the next start, before the title screen (about 15 s for 700).
- With a frame rate patch (`BB_FPS=60`, or `uncap`, the default) the character creator shows no
  character model; with `BB_FPS=30` (the game's own 30 FPS, no patch) it does. In the game
  itself models are shown with every preset. To see the model while creating a character,
  start with `BB_FPS=30`, and restart with your preset afterwards.
- FSR 3.1 does not start on KosmicKrisp yet (`Upscaler: FSR 3 context creation failed` in the
  log); the game is then shown without the port's upscaler. TAA is untested on a Mac.

## Differences from Linux

- FSR 4 and FSR 4.1.1 need AMD-specific Vulkan features and fall back to FSR 3.1, which does
  not start on KosmicKrisp yet (see above).
- Guest memory uses POSIX shared memory and a reservation of the guest address range at start;
  host objects the game can see come from an allocator below 1 TiB (`src/runtime_heap.c`).
  Under Rosetta 2 the system already uses 63–64 GiB (the commpage) and 64–448 GiB, so the
  game's memory starts at 448 GiB (0x7000000000; shadPS4's macOS build uses the same start).
- The game's thread pointer is read from a pthread TSD slot (macOS keeps GS for itself).
- Helper threads run at the utility QoS class instead of `SCHED_IDLE`; the GPU command thread's
  CPU time in the frame statistics comes from `thread_info`.
- The game window and its events belong to the process's main thread (AppKit); the game's main
  thread runs on a thread of its own.

## Reporting

Send `bbport-macos.log` and the output of

```bash
bin/bb-probe --vulkan-only             # out/bb-probe in a source tree
sysctl -n machdep.cpu.brand_string; sw_vers
```

If the game stops with a fault, the log ends with the guest offset and a thread dump; with
`BB_TIMEOUT=60` the watchdog dumps all threads after 60 seconds (a hang). On a Mac that dump
can stop early: the signal that collects it wakes a waiting game thread, which then faults
(`Guest fault (signal 10) at guest offset 0x53b3260`); `sample <pid>` gives the threads'
stacks of a running game instead. `BB_FRAME_STATS=1` adds frame rate and stall figures every
5 seconds.
