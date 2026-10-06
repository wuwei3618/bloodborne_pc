# macOS 移植交接（给 Mac 本地的 Claude 会话）

到 2026-10-06 为止，macOS 移植一直在云端会话里做（那里没有 Mac，只能靠 GitHub Actions 验证）。
从 2026-10-06 起，真机测试和修改由 Mac 本地会话负责。用户的 Mac：M5 Pro，macOS 27.2。

现状（2026-10-07）：游戏已经在这台 Mac 上运行起来。用 CI 的预编译包可以启动游戏、播放影片、创建角色、
进入第一个区域，DualSense 手柄可用。菜单 60 到 120 FPS，复杂场景约 25 到 40 FPS，瓶颈是 Rosetta 2 下的
GPU 命令线程。真机测试中发现的问题和处理结果见第 5 节第 10 至 17 条和第 6.1 节，没做完的事见第 9 节。

## 0. 开工清单

1. `git clone --recursive https://github.com/wuwei3618/bloodborne_pc bbport && cd bbport && git checkout macos-port`
2. 先读 `docs/MACOS.md`（面向用户的说明，也是预编译包里的 README），再读本文。
3. 第一轮测试用 CI 的预编译包（第 3.1 节），不必先搭编译环境。
4. 需要改代码时再搭本地编译环境（第 3.2 节），或者走“推送 → CI 出包 → 下载测试”的循环。
5. 按第 6 节的预期日志逐项对照，出问题按第 7 节排查。
6. 本机上游戏目录和测试包的位置见第 2 节。

## 1. 当前状态

- 提交：`0c62f20` … `d9ae010`（`git log 5224a6d..macos-port`），全部在 `macos-port`，未合并到 main。
  真机测试期间的提交：`1c23636`（窗口放到主线程）、`8d2768f`（影片停止时的线程等待）、
  `7fb005a`（`BB_PM4_CHECK` 诊断）、`d9ae010`（提交节流的空闲信号）。
- CI：`.github/workflows/macos.yml`，每次推送跑两个任务（约 15 分钟）：
  - **Intel (x86_64)**（`macos-15-intel`）：x86_64 Homebrew 装依赖 → `build.sh --test` → GPU 库测试 →
    Python 测试 → `packaging/macos_package.sh --tests` → 上传产物 `bbport-macos-x86_64`（用户包，
    保留 90 天）和 `macos-x86_64-tests`（测试程序，7 天）。
  - **Apple silicon (x86_64 under Rosetta 2)**（`macos-26`，机器上**没有**任何 x86_64 库）：下载两个
    产物，在 Rosetta 下跑运行时 / GPU 库 / Python 测试，启动包里的 `bin/bb-probe`，再用包里的
    KosmicKrisp 跑 `--vulkan-only`（虚拟机没有能用的 GPU，预期返回 -3；真机应打印 GPU 名和 PASS）。
- 最新绿色构建：第 16 轮，https://github.com/wuwei3618/bloodborne_pc/actions/runs/37467304654
- Linux：到 `c9df91f` 为止，所有 macOS 代码都在 `__APPLE__` 分支或 `src/platform.h` 里，每轮改动都在 Linux
  （nix-shell）上运行过 `build.sh --test` 和 82 项 Python 测试。`8d2768f`、`7fb005a`、`d9ae010` 改的是
  Linux 和 macOS 共用的代码（`gpu/shim/core/libraries/kernel/threads.h`、`liverpool.cpp`、`gnmdriver.cpp`），
  还没在 Linux 上验证，合并前要补做。仓库没有 Linux CI。

## 2. 用户的环境与限制

- M5 Pro，macOS 27.2，CLT 27.2（Apple clang 21）。CI 用的是 macOS 26 + Xcode 26.6。预编译包在 macOS 27.2 上
  运行正常；本地还没用 CLT 27 完整编译过，只对改过的源文件做过 x86_64 的 `clang -fsyntax-only` 检查。
  构建用 `-Werror`，新版 clang 多出的警告可能让编译失败，遇到了就改代码。
- Rosetta 2 只完整支持到 macOS 27，从 macOS 28 起只保留面向老游戏的一部分。x86 方案大约还有一年的窗口期。
- Homebrew 7（2026 年 9 月）把 Intel macOS 降为 Tier 3：官方安装脚本拒绝装 x86_64 版，新版本的公式
  大多没有 Intel 预编译包（会从源码编），2027 年 9 月完全移除。
- 游戏 dump 只在用户本地，不要上传、不要提交任何游戏文件。本机的位置：
  - 游戏目录：`~/Code/Bloodborne/CUSA03023`（亚洲版 1.09，见第 5 节第 14 条）。
  - 测试用的包：`~/Code/Bloodborne/ci-<run-id>/bbport-macos-x86_64`。存档和管线缓存在包目录的 `user/` 下，
    换新包时要把 `user/` 复制过去。
- 本机 zsh 里的 `grep` 是调用 ugrep 的函数，处理 `strings` 之类的输出时会把输入当成二进制文件，不打印结果。
  这种情况改用 `/usr/bin/grep`。

## 3. 怎么跑起来

### 3.1 预编译包（第一轮测试用这个）

产物 `bbport-macos-x86_64`（25 MB）：
- 网页下载：打开上面那个 run → Artifacts；
- 命令行下载：`gh run download <run-id> -n bbport-macos-x86_64 -D <目录>`。本机上这个命令有时一直不写出文件，
  可以改用 `gh api repos/wuwei3618/bloodborne_pc/actions/artifacts/<artifact-id>/zip > pkg.zip`
  （产物号用 `gh api repos/wuwei3618/bloodborne_pc/actions/runs/<run-id>/artifacts` 查，25 MB 约需 5 分钟）。

具体步骤见 `docs/MACOS.md` 的 “The prebuilt package” 一节：
```bash
softwareupdate --install-rosetta --agree-to-license
xcode-select --install                     # 只为 python3（3.9 就够）
tar -xzf bbport-macos-x86_64.tar.gz && cd bbport-macos-x86_64
xattr -dr com.apple.quarantine .           # 下载的程序未公证，不去掉隔离属性会被拦
bin/bb-probe --vulkan-only                 # 预期：Vulkan: <GPU 名>; ... readback PASS
BB_GAME_DIR=/path/to/CUSA03173 bash bbport.sh 2>&1 | tee bbport-macos.log
```
包内结构：
- `bin/bb-probe`、`bin/gpu/libbbgpu.dylib`；
- `bin/lib/*.dylib`：23 个依赖库，引用全是 `@rpath`；来源记在 `THIRD_PARTY.txt`；
- `bin/vulkan/icd.d/`：x86_64 KosmicKrisp；
- `run.sh`、`scripts/`、`patches/`；
- `bbport.sh`：设置 `BB_PREBUILT=1` 后运行 `run.sh`。

存档、`bbport.ini` 和生成的文件都放在包目录里。第一次启动慢，因为 Rosetta 要先翻译一遍。

### 3.2 本地从源码编（改代码时用）

```bash
bash tools/macos_homebrew_x86_64.sh        # 绕开官方安装脚本，在 /usr/local 装 x86_64 brew（需要密码）
arch -x86_64 /usr/local/bin/brew install cmake ninja pkgconf glslang vulkan-headers vulkan-loader \
    sdl3 ffmpeg boost fmt magic_enum robin-map xxhash zydis miniz xbyak
```
上面这一步会从源码编 cmake、ninja、pkgconf、glslang、vulkan-headers/loader、sdl3、ffmpeg（含依赖）和 xxhash，第一次会比较久。

```bash
BB_LTO=OFF bash build.sh --test            # 在 arm64 shell 里直接跑，它会自己切到 arch -x86_64 /bin/bash
PATH=/usr/local/bin:$PATH ninja -C out/gpu motion-history-test ui-composition-test motion-shader-test hle-thread-test submission-gate-test
for t in motion-history-test ui-composition-test motion-shader-test hle-thread-test submission-gate-test; do ./out/gpu/$t; done
python3 -m unittest discover -s tests
```

驱动：从 shadPS4 的 macOS 发布包取 x86_64 KosmicKrisp，放到 `out/vulkan/icd.d/`。

```bash
curl -LO https://github.com/shadps4-emu/shadPS4/releases/download/v.0.19.0/shadps4-macos-sdl-0.19.0.zip
mkdir -p out/vulkan/icd.d
unzip -j shadps4-macos-sdl-0.19.0.zip kosmickrisp_mesa_icd.json libvulkan_kosmickrisp.dylib -d out/vulkan/icd.d
BB_GAME_DIR=/path/to/CUSA03173 bash run.sh 2>&1 | tee bbport-macos.log
bash packaging/macos_package.sh            # 可选：打出和 CI 一样的 dist/bbport-macos-x86_64.tar.gz
```

注意：
- `build.sh` 会给子模块 `gpu/third_party/fsr-vulkan` 打补丁，提交前要还原：`git -C gpu/third_party/fsr-vulkan checkout -- .`。
- 默认开 LTO，编得慢，调试时用 `BB_LTO=OFF`。PGO 只在 GCC 下生效，Mac 上是关的。
- 省时间的思路（还没做）：cmake/ninja/pkgconf/glslang 只在构建时运行，用 arm64 Homebrew 的版本就行。
  真正必须是 x86_64 的只有链接进程序的库：sdl3、ffmpeg、vulkan-loader、xxhash、fmt、zydis、miniz。
  这样做的话，pkg-config 要改为只搜 `/usr/local` 下的 .pc 文件。

## 4. 移植改了什么（文件地图）

| 位置 | 内容 |
|---|---|
| `src/platform.h` | Linux/macOS 差异的统一入口：计时锁（macOS 上是 trylock + 小睡轮询）、单调时钟上的条件变量等待（相对时间）、`clock_nanosleep` 替代、`getrandom`→`getentropy`、stat 时间字段、信号上下文寄存器、线程 id/名字/CPU 时间、Mach-O 汇编符号下划线 |
| `src/runtime_memory.c` | 游戏地址空间：`guest_space()` 分段预留 `[0x800000000, 0xfc00000000)`，跳过宿主已占用的段（`host_ranges`）；内存池用 POSIX shm（`shm_open` 后立即 unlink，只 ftruncate 一次）；`discard()` 用 mincore 把有数据的页清零，代替 PUNCH_HOLE |
| `src/runtime_heap.c` | macOS 上游戏能看到的宿主对象（线程、TLS、锁、AvPlayer 句柄）必须在 1 TiB 以下：从 `runtime_low_map` 切出的小分配器 |
| `src/runtime_thread.c` + `src/probe.c` | 游戏 TLS：脚本已把 `mov rax, fs:[0]` 改成 gs 前缀；macOS 上 `probe.c` 的 `patch_thread_pointer_loads` 把位移改成 `pthread TSD key*8`，`runtime_thread.c` 用 `pthread_setspecific` 写入。读 `gs:[key*8]` 就等于 `pthread_getspecific`，Rosetta 下已验证可用 |
| `src/probe.c` | SIGBUS 也当作访问错误；`mach_vm_read` 读游戏内存；看门狗用 `task_threads` + `pthread_kill(SIGUSR2)` 抓所有线程；`use_packaged_vulkan_driver()`：没有设置 `VK_DRIVER_FILES` 时，自动使用 `<exe 目录>/vulkan/icd.d` |
| `gpu/CMakeLists.txt` | VMA 头文件内置（`third_party/vma`）；不用 X11；用 `-undefined dynamic_lookup`；PGO 仅 GCC |
| `gpu/shim/*` | SDL 的 Metal 视图作为 surface（`window.cpp`）；辅助线程用 utility QoS 代替 SCHED_IDLE；Mach-O 的字体嵌入汇编；菜单键 Cmd+, |
| `gpu/shadps4/...` | libc++ 兼容：`regs.h` 的 `BlockSet` 替代 libstdc++ 的 `bitset::_Find_first`；`texture_cache.h` 不用 `atomic_ref<const T>`；`pm4_cmds.h` 的地址转换；`liverpool.cpp` 用 `thread_info`；`vk_platform.cpp` 加 portability 枚举（MoltenVK）；AvPlayer 句柄从低地址堆分配；userfaultfd 只在 Linux |
| `build.sh` / `run.sh` | Apple 芯片上自动 `arch -x86_64 /bin/bash` 重启自己；只用 `/usr/local` 的 x86_64 Homebrew；rpath 和 `-headerpad_max_install_names`；run.sh 调大文件描述符上限，没有 x86_64 驱动时给出提示 |
| `packaging/macos_bundle.py` | 把 Homebrew 库收进 `lib/`，按程序引用时的名字存放（`libvulkan.1.dylib` 会被 GPU 库按名字 dlopen），引用全改成 `@rpath`，重新做 ad-hoc 签名，最后检查没有外部路径残留 |
| `packaging/macos_package.sh` | 组装用户包（含固定版本并校验 sha256 的 shadPS4 KosmicKrisp）；`--tests` 额外打测试程序包 |
| `tools/macos_homebrew_x86_64.sh` | 手动装 x86_64 Homebrew（官方安装脚本拒绝 Intel） |
| `src/probe.c`（真机） | macOS 上模块初始化和游戏入口（`start_game`）在新线程（8 MiB 栈）上运行，进程主线程进入 `bbgpu_window_loop` 处理窗口事件；`--cpu-only` 时主线程只等待 |
| `gpu/shim/bbgpu.cpp` | macOS 上 `bbgpu_init` 在调用线程（主线程）上直接创建窗口；`bbgpu_window_loop` 运行事件循环 |
| `gpu/shim/core/libraries/kernel/threads.h` | AvPlayer 用的线程封装：加锁，多个线程同时 join 时只由一个调用者真正 join，线程结束自己的对象时只在没人 join 时 detach |
| `gpu/shim/bbport_submission_gate.h` + `gnmdriver.cpp` | 提交锁（`BbPort::SubmissionGate`）：设置和清除都在互斥锁内读取 GPU 当时的状态，GPU 还有提交时收到的空闲信号不清除提交锁 |
| `liverpool.cpp` | `BB_PM4_CHECK=1`：提交时保存命令缓冲副本，处理开始时和解析出错时与内存比对 |
| `tests/` | `test_runtime.c` 补了 macOS 没有的 `pthread_barrier`；GPU 测试桩改用 `__thread`；两个 Python 测试把临时目录解析成真实路径；`test_probe.py` 检查 SDL 视频子系统在主线程上启动；`test_hle_thread.cpp`、`test_submission_gate.cpp` |

## 5. 已经踩过的坑（关键事实）

1. **KosmicKrisp**：LunarG Vulkan SDK（实测 1.4.363.0）和 Homebrew 的版本都只有 arm64，x86_64 进程加载不了。
   shadPS4 发布包里自带 x86_64 版（v0.19.0，Mesa 26.3）。bbport 渲染器硬性要求
   `VK_EXT_robustness2`（robustBufferAccess2 / robustImageAccess2 / nullDescriptor）、`VK_KHR_push_descriptor`
   和 `VK_EXT_vertex_attribute_divisor`，MoltenVK 很可能满足不了，所以不考虑。
2. **Rosetta 下的地址布局**：宿主占着 `0xfc0000000-0x1000000000`（arm64 commpage，每个进程都有）和
   `0x1000000000-0x7000000000`（64–448 GiB）。所以：
   - 游戏用户区从 `0x7000000000` 开始，和 shadPS4 Mac 版一样；
   - 低地址区实际可用的是 `[0x800000000, 0xfc0000000)`；
   - 游戏如果要求**固定**映射到这些被占的段，会得到 NO_MEMORY（`0x8002000c`）。
3. **`-undefined dynamic_lookup`**：`libbbgpu` 里的 `runtime_*` 符号在加载时从主程序解析。chained fixups 会在启动时一次绑定全部符号，所以测试桩必须把这些符号都定义齐。
   Darwin 上 C++ 的 `thread_local` 存储是内部的，跨镜像共享的符号要用 `__thread`。
4. **libc++ 和 libstdc++ 的差异**：没有 `_Find_first`、没有 `atomic_ref<const T>`，`std::jthread` 要 Xcode 26 以上。
5. **bash 3.2**（macOS 的 `/bin/bash`）：`set -u` 下空数组会报 unbound，要写成 `${a[@]+"${a[@]}"}`。
   run.sh 在 Linux AppImage 里只能用 bash 内建命令，在 macOS 上又会被 bash 3.2 执行，改它要同时兼顾两边。
6. **run.sh 调用的 Python 脚本**要兼容 3.9（CLT 自带的版本）。launcher/ 里用了 3.10 语法，但 macOS 上不用 launcher。
7. 改过的 Mach-O 必须重新签名（ad-hoc），否则在 Apple 芯片上会被杀。下载的包要去掉隔离属性。
8. macOS 的临时目录在 `/var -> /private/var` 链接后面，比较路径前要先解析。
9. CI 的 Intel 机器之所以还能装到 Intel 预编译包，是因为它的 Homebrew 用的是镜像里缓存的旧版本信息。
   镜像一更新，Dependencies 步骤就会开始从源码编，会变慢，到时考虑缓存 `/usr/local/Cellar`。
   `macos-15-intel` 镜像 2027 年 8 月下线。
10. SDL3 的 Cocoa 驱动只在进程主线程上初始化视频子系统（`SDL_cocoavideo.m` 检查 `[NSThread isMainThread]`），
    在其他线程上会报 `No available video device`。AppKit 的窗口和事件也只能在主线程上处理。处理方法见第 4 节
    `src/probe.c`（真机）和 `gpu/shim/bbgpu.cpp` 两行。测试：`test_probe.py` 的 `test_sdl_video_starts_on_the_main_thread`。
11. AvPlayer 的线程对象会被几个线程同时 join：游戏的 `sceAvPlayerStop`、解复用线程在文件结尾的收尾流程，
    以及解码线程结束自己的对象。没有同步时，第二次 join 在 macOS 上要么返回 EINVAL（异常导致进程退出），
    要么等待一个已经被系统分配给新线程的句柄，永远不返回（影片播完时游戏卡住）。测试：`hle-thread-test`。
12. 提交节流：`sceGnmSubmitDone` 在 GPU 还有工作时设置提交锁，GPU 空闲中断清除它，下一次提交前等待它被清除。
    GPU 线程每一轮结束都会发空闲信号，包括排空绘制管线期间又有新提交的情况。旧代码收到这种信号也会解除节流，
    游戏于是提前复用 GPU 线程还没读的命令缓冲。`BB_PM4_CHECK=1` 实测：未修复时 GPU 线程落后 31 到 35 个提交，
    命令缓冲在开始处理时已被改写；修复后为 0 次。症状是菜单里的 `packet length exceeds remaining submission size`
    和角色创建界面的 `Unimplemented PM4 type 0`（游戏退出）。只在 GPU 命令线程几乎没有空闲时出现
    （不锁帧时空闲 0.4%，30 帧时 18% 到 68%）。测试：`submission-gate-test`。
13. 帧率补丁（`60 FPS++`、`Uncap FPS++`）打开时，角色创建界面不显示角色模型：游戏不发出那部分绘制，
    帧统计里每帧一直是 201 次；`BB_FPS=30`（不打补丁）时会升到约 270 次，模型正常。进入游戏后各种设置下模型都正常。
    这是补丁本身的行为，和 macOS 无关，Linux 的默认设置（`uncap`）应该也一样，还没在 Linux 上确认。
14. 游戏版本：CUSA03023（亚洲版 The Old Hunters Edition）的 1.09 eboot 和 CUSA03173 是同一个构建
    （镜像大小、导入和重定位数量与开发日志的记录完全一致，sha256 `d65f0b4f...29f9`），可以直接使用。
    如果手里是 PKG：`param.sfo` 在 PKG 的条目表里（条目 0x1000，未加密），要单独取出放到 `sce_sys/`；
    用 LibOrbisPkg 0.2 的 `PkgTool.Core` 解包时，要用自带 .NET Core 3 运行时的 `PkgTool.Core-osx-x64`。
    在 .NET 6 及以上的运行时里，它的 `PFSCReader` 对每个 64 KiB 压缩块只调用一次 `DeflateStream.Read`，
    只拿到一部分数据，其余为 0：实测 26,913 个 DCX 文件里有 17,226 个损坏。损坏时游戏启动后进入空转，
    日志里有 `Error: shaderBinarySize ... is not equal to Program size ...`。解包后应检查所有 DCX 都能完整解压。
15. 看门狗转储：macOS 上 SIGUSR2 会让正在 `pthread_cond_wait` 里等待的线程提前返回（Linux 上 futex 等待会自动重启），
    游戏的工作线程随后执行无效任务并在 `0x53b3260` 处故障，转储因此中断，退出码 138。
16. KosmicKrisp 没有启用 Mesa 的磁盘着色器缓存。新管线首次编译最长约 2 秒，新区域会卡顿。bbport 把用过的管线
    存在 `user/cache`，下次启动时在标题画面之前重新编译（约 700 个管线多用约 15 秒）。
17. FSR 3.1 在 KosmicKrisp 上创建上下文失败（`Upscaler: FSR 3 context creation failed`），之后本次运行不再使用
    超分辨率，原因还没查。TAA 路径不创建 FSR 3 上下文，还没在 Mac 上测过。

## 6. 第一次真机运行：预期日志和最可能出问题的地方

正常的话，日志里依次会有：
```
Runtime: guest range: 0xfc0000000-0x1000000000 is the host's, left out
Runtime: guest range: 0x1000000000-0x7000000000 is the host's, left out
Runtime: guest range 0x800000000-0xfc00000000 reserved
Thread pointer loads: N read gs:[0x...]        # 真实游戏 N 应 > 0
GPU: window and Vulkan presenter ready; SDK 0x..., N HLE symbols
Mapped ... bytes, ... segments; applied ... relocations
```
最可能出问题的地方，大致按可能性排序：
1. **渲染器初始化**：`vk_instance.cpp` 的 `ASSERT_MSG`（“Required Vulkan extension/feature unavailable”、
   API 版本）。bbport 在 shadPS4 之外加了需求，比如 FSR 3 accumulate 着色器要的特性（`vk_instance.cpp` 里
   标了 bbport 注释的地方）。可以对照 shadPS4 在 KosmicKrisp 上的特殊处理（`eMesaKosmickrisp`）。
2. **固定地址映射撞上宿主占用的段**（第 5 节第 2 条）：日志里映射失败、返回 `0x8002000c`。
3. **窗口 / Metal layer**：`gpu/shim/window.cpp`（SDL 驱动名 "cocoa"，`SDL_Metal_CreateView`）。
4. **GPU 页跟踪**：写保护页的故障在 macOS 上是 SIGBUS，由 `bbgpu_handle_fault` 处理。如果日志里出现
   `Guest fault (signal 10)` 或 `Host fault`，先看这一块。
5. **计时锁和等待**：`platform.h` 的 `bb_poll_lock`（轮询）和相对时间的条件变量，可能带来卡顿或延迟。
6. 音频（SDL3 → CoreAudio）、手柄（SDL3）、菜单（Cmd+,）。
7. 性能：游戏代码由 Rosetta 即时翻译（macOS 15 起 Rosetta 支持 AVX/AVX2）。FSR 4 不可用，会退回 FSR 3.1，也可以用 TAA。

### 6.1 实测结果（2026-10-06 至 10-07，M5 Pro，macOS 27.2）

- `bin/bb-probe --vulkan-only`：`Vulkan: Apple M5 Pro; command submission + 4096-byte readback PASS`。
  驱动报告 KosmicKrisp 26.2.99，Vulkan 1.4.363。
- 上面的预期日志全部出现，`Thread pointer loads: 17127 read gs:[0x850]`，`Mapped 93538364 bytes, 6 segments; applied 237298 relocations`。
- 第 1 至 5 条担心的问题都没有出现。出现过的问题见第 5 节第 10 至 17 条，都已处理或记录。
- 窗口按显示器大小建成 1728x971（Retina），vblank 跟随显示器 120 Hz（`VideoOut: vblank 480 Hz, frame limit 120 FPS`）。
- 音频走 CoreAudio，DualSense 由 SDL 识别，输入名字的对话框可以直接用键盘输入。
- 驱动缺少的可选扩展和格式（`D16UnormS8Uint` 不能做深度模板附件、`R5G5B5A1` 完全不支持等）只有警告，
  暂时没有看到对应的画面问题。
- 帧统计（`BB_FRAME_STATS=1`）：标题画面 120 FPS；角色创建界面不锁帧约 93 FPS（GPU 命令线程空闲 0.2%）、
  60 帧时空闲约 75%；第一个区域 30 帧时 GPU 命令线程空闲 18% 到 68%；过场动画（每帧约 1,400 次绘制）约 25 FPS。

## 7. 调试手段

- `BB_TIMEOUT=60`：60 秒后看门狗打印所有线程（判断是不是卡死）。macOS 上转储可能中断，见第 5 节第 15 条。
- `sample <pid> 3 -file out.txt`：采样正在运行的游戏 3 秒，得到所有线程的调用栈，包括游戏代码地址
  （游戏镜像从 `0x800000000` 开始），不需要开发者模式。卡住、空转时首选这个。
- `BB_FRAME_STATS=1`：每 5 秒打印帧率、最慢一帧、着色器编译次数和耗时、GPU 命令线程空闲比例。
- `BB_PM4_CHECK=1`：报告提交之后被改写的命令缓冲和无法解析的命令包（第 5 节第 12 条）。
- `BB_PAD_FILE=<文件>`：把按键名（`cross`、`down` 等）写进文件就按下，清空就松开，可以脚本化操作菜单。
  设置菜单打开时注入的按键会被忽略。
- 游戏里的错误路径可以用外部补丁临时写入 `ud2`：在 `BB_PATCHES_DIR` 指向的目录里放一个 `isEnabled="true"` 的
  shadPS4 格式 XML，地址等于游戏偏移加 `0x400000`。执行到那里时故障处理会打印调用链。
- 出故障时日志最后会有 `Guest fault ... at guest offset 0x...` 或 `Host fault ... in <库>+0x...`，后面跟宿主调用栈。
- `VK_LOADER_DEBUG=error,warn,driver`：看驱动有没有找到、有没有加载成功。
- 不经过 run.sh 直接运行（先完整跑一次 run.sh，让它生成 `out/` 里的文件）：
  ```bash
  bin/bb-probe out/boot-linked.bin --content-profile out/content.bin --patches out/patches.bin \
      --app0 /path/to/CUSA03173 --user user --timeout 0    # 源码树里换成 out/bb-probe
  ```
  run.sh 还会设置 `BB_CONFIG`、`BB_VBLANK_HZ` 等环境变量，需要时照抄。
- lldb（Rosetta 进程也能调）：先执行 `process handle -p true -s false -n false SIGSEGV SIGBUS SIGUSR2 SIGALRM`，
  再 `run`。页跟踪本身就靠 SIGSEGV/SIGBUS 工作，不放行的话会一直停在这些信号上。本机没有开启开发者模式，
  附加进程时会弹出授权对话框，需要先由用户执行 `sudo DevToolsSecurity -enable`。
- 其他开关见 README（`BB_DMEM_MB`、`BB_TOGGLE_FILE`、`BB_AUDIO_TRACE` 等）。

## 8. 改代码的规矩

- 在 `macos-port` 上提交、推送，CI 两个任务都要保持绿色。每次推送都会产出新的预编译包，用户可以直接下载测试。
- **不要改变 Linux 行为**：新代码放进 `__APPLE__` 分支，或者加到 `platform.h` 的抽象里。
  仓库没有 Linux CI，合并前要在 Linux 机器上跑
  `nix-shell shell.nix --run 'bash build.sh --test && python3 -m unittest discover -s tests'`。
- shell 脚本要兼容 bash 3.2，run.sh 用到的 Python 脚本要兼容 3.9。
- 新增动态库依赖时打包脚本会自动收进来；Apple 芯片 CI 那一步会验证包能独立运行。
- 提交信息沿用现有风格（`git log` 里 `macOS: ...` 开头，正文写清原因和验证方式）。

## 9. 没做完的事

1. 在 Linux 上验证共用代码的改动（`8d2768f`、`7fb005a`、`d9ae010`），然后再考虑合并。
2. FSR 3.1 在 KosmicKrisp 上创建上下文失败（第 5 节第 17 条）；顺便测 TAA。
3. 帧率补丁下角色创建界面不显示模型（第 5 节第 13 条）：可以逐步去掉补丁行，找出是哪几行。
   `60 FPS++` 和 `Uncap FPS++` 有 37 行相同的改动，其中 30 行在 `30 FPS++` 里也有。
4. 复杂场景性能：GPU 命令线程在 Rosetta 下是瓶颈（第 6.1 节）。
5. macOS 的默认帧率设置（目前和 Linux 一样是 `uncap`），等第 3 项有结果后再定。
6. 看门狗转储在 macOS 上中断（第 5 节第 15 条）：可以改用 Mach 的 `thread_get_state` 读取各线程状态，不发信号。
7. 本地用 Xcode/CLT 27 完整编译还没试过。GTK 启动器没移植（Mac 上用 `bbport.ini` 和游戏内菜单）。FSR 4 不可用。
8. 可以考虑自己从 Mesa 编译 x86_64 KosmicKrisp（shadPS4 用的是 `shadexternals/mesa-kosmickrisp`），不再借 shadPS4 的二进制。
9. 长期：Rosetta 在 macOS 28 之后只保留部分功能，Homebrew 2027 年 9 月移除 Intel 支持，
   长远要么做 arm64 原生版（工作量很大），要么像 shadPS4 那样把依赖都并入项目自己编译。
10. 可以补一个 Linux CI 任务（在 nix-shell 里运行 `build.sh --test` 和 Python 测试），这样改 macOS 时 Linux 的回归也能自动发现。
