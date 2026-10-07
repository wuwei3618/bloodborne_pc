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
opening and the first area work, with a DualSense. Menus run at 60 to 120 FPS, the first area
at about 25 to 40 FPS, where the GPU command thread spends about two thirds of its time waiting
for the GPU. See [Known issues](#known-issues); reports (logs, see the end) are still welcome.

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
in-game menu opens with **Cmd+,** (or L3+R3 on a gamepad). It is in Chinese when Chinese is the
first language in System Settings (glyphs from the system font Hiragino Sans GB), else in
Russian; its Язык / 语言 entry, or `language=zh` or `language=ru` in `bbport.ini`, chooses one.

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
  compiles them again at the next start, before the title screen (about 15 s for 700). Each new
  render size (another output resolution or preset) needs its own pipelines, so the first run
  with new settings stutters more.
- Object motion vectors (the menu's character motion vectors) have no effect on a Mac yet: the
  motion vector debug view shows no object vectors (no blue) on moving characters, so with
  FSR 3.1 they can leave trails. They are off by default on macOS: turned on, they make each
  frame about 2.6 ms longer (see Performance). `object_motion=1` or the menu turns them on.
- Once, the GPU device was lost at the title screen, when the idle movie was about to start
  (`Device lost during submit`, and an `IOGPUMetalError` in the system log); in the next run
  the movie started normally.
- With a frame rate patch (`BB_FPS=60`, or `uncap`, the default) the character creator shows no
  character model; with `BB_FPS=30` (the game's own 30 FPS, no patch) it does. In the game
  itself models are shown with every preset. To see the model while creating a character,
  start with `BB_FPS=30`, and restart with your preset afterwards.
- Gameplay runs at about 25 to 40 FPS (M5 Pro). With `BB_FPS=60` every frame waits for the
  emulated 60 Hz vblank, so frames are shown for 16.7 or 33.3 ms in turn, which looks less
  smooth than `uncap` (the default) at the same frame rate: there vblank runs at 480 Hz and a
  frame is shown about 2 ms after it is finished. Use `uncap` on a Mac.
- FSR 3.1 works on KosmicKrisp in packages built from October 7, 2026 on; before, its Vulkan
  backend found no memory type for its images on unified memory and did not start. TAA is
  untested on a Mac.
- On a Retina display the default 1080p output looks soft. `output_res=3840x2160` and
  `preset=3` in `bbport.ini` (Performance: the scene renders at about 1080p and FSR 3.1 fills
  4K) look sharper at a slightly lower frame rate. Median frame rates in the first area with
  an M5 Pro, from light to heavy scenes: 1080p with FSR 3.1 native AA 37, 32, 30 and 28 FPS;
  4K output with Performance 31, 29.5, 28.5 and 27 FPS; with Quality (scene 2560x1440) 27, 25
  and 22.5 FPS in the three heavier groups, which is too slow (next item).
- Below 30 FPS the game runs slower than real time. The game advances at most 1/30 s per
  frame: the original game always advances 1/30 s, and the frame rate patches use the real
  frame time up to that limit. At 22 FPS the game runs at about 73% speed, at 27 FPS at about
  90%: at 19 to 25 FPS (4K output with Quality) movement was visibly slowed, at 26 to 33 FPS
  (4K output with Performance) it was not.

## Performance

Measured on an M5 Pro with macOS 27.2, `uncap`, standing still at one spot in the Hunter's
Dream.

- The GPU is the limit: it is busy about 90% of the time in gameplay, and the GPU command
  thread waits for it about 70% of the time. shadPS4 0.19.0 with the same KosmicKrisp is no
  faster: at 1080p without an upscaler, bbport ran at 34.5 to 36 FPS and shadPS4 at 32 to
  33 FPS (GPU busy 87 to 92% in both).
- KosmicKrisp prepares some draws in a compute pass before drawing them. Metal has no compute
  inside a render pass, so each of these ends the render pass and its attachments are stored
  and loaded again. With `BB_FRAME_STATS=1` the line `Compute-first draws` counts them per
  frame; at that spot: 43 indexed 16-bit strips with primitive restart off (KosmicKrisp
  rewrites their indices to 32 bits), 22 rect lists (bbport draws them with tessellation) and
  27 draws of the game's own tessellation.
- On macOS bbport turns primitive restart on for those strips, so KosmicKrisp draws them
  directly; only a strip that uses vertex 65535 draws differently, and none was seen.
  `BB_STRIP_RESTART=0` turns this off.
- Frame time at that spot with 4K output and FSR 3.1 Performance: 36.8 ms (27.2 FPS) before;
  2.8 ms less with restart on for the strips, 2.6 ms less again without object motion vectors
  (31.4 ms, 31.8 FPS, the macOS defaults now). FSR 3.1 itself took 4.5 ms of the frame.
- `BB_GPU_PROFILE=1` gives no usable numbers on KosmicKrisp: timestamps written outside a
  render pass read back as 0, and each one adds a compute dispatch and a resolve, which halves
  the frame rate.
- MoltenVK cannot replace KosmicKrisp: it lacks `robustBufferAccess2` and `nullDescriptor`,
  which the renderer requires, and geometry shaders and logic operations, among others. Apple's
  Game Porting Toolkit translates Direct3D, not Vulkan.

## Differences from Linux

- FSR 4 and FSR 4.1.1 need AMD-specific Vulkan features and fall back to FSR 3.1 (see above).
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
