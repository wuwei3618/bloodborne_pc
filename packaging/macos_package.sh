#!/usr/bin/env bash
# Builds dist/bbport-macos-x86_64.tar.gz on a Mac (docs/MACOS.md): the port's current build (run
# build.sh first) with the libraries it loads from the x86_64 Homebrew (packaging/macos_bundle.py),
# run.sh with its scripts, and the x86_64 KosmicKrisp driver of shadPS4's macOS release. The
# package runs on an Apple silicon Mac without Homebrew, Xcode or a build.
# --tests: dist/macos-x86_64-tests.tar.gz too, the test programs as out/, made self-contained the
# same way (CI runs them on an Apple silicon Mac under Rosetta 2).
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
[[ -f out/bb-probe && -f out/gpu/libbbgpu.dylib ]] || { echo 'Build first: bash build.sh' >&2; exit 1; }
# KosmicKrisp (Mesa, MIT) built for x86_64: LunarG's and Homebrew's are arm64-only.
kosmickrisp_url=https://github.com/shadps4-emu/shadPS4/releases/download/v.0.19.0/shadps4-macos-sdl-0.19.0.zip
kosmickrisp_sha256=47676b4875343b69f49689c7668d71de79ecaaa17ffeab3415d4137800b7ee02
name=bbport-macos-x86_64
stage=dist/$name
rm -rf "$stage" && mkdir -p "$stage/bin/gpu" "$stage/bin/vulkan/icd.d"
cp run.sh LICENSE "$stage/"
cp docs/MACOS.md "$stage/README.md"
cp -R scripts patches "$stage/"
rm -rf "$stage/scripts/__pycache__"
cp out/bb-probe out/bb-gpu-capabilities "$stage/bin/"
cp out/gpu/libbbgpu.dylib "$stage/bin/gpu/"
{
    echo 'Libraries in bin/lib and where the build took them from (Homebrew):'
    python3 packaging/macos_bundle.py "$stage/bin" "$stage/bin/bb-probe" "$stage/bin/bb-gpu-capabilities" \
        "$stage/bin/gpu/libbbgpu.dylib"
    echo
    echo "bin/vulkan/icd.d: libvulkan_kosmickrisp.dylib and its manifest from $kosmickrisp_url"
} > "$stage/THIRD_PARTY.txt"
curl -fsSL -o dist/shadps4-macos.zip "$kosmickrisp_url"
echo "$kosmickrisp_sha256  dist/shadps4-macos.zip" | shasum -a 256 -c
unzip -q -o -j dist/shadps4-macos.zip kosmickrisp_mesa_icd.json libvulkan_kosmickrisp.dylib -d "$stage/bin/vulkan/icd.d"
rm dist/shadps4-macos.zip
cat > "$stage/bbport.sh" <<'EOF'
#!/bin/bash
# Starts the game from this folder: BB_GAME_DIR=/path/to/CUSA03173 bash bbport.sh
cd -- "$(dirname -- "$0")" && BB_PREBUILT=1 exec bash run.sh "$@"
EOF
chmod +x "$stage/bbport.sh"
tar -C dist -czf "dist/$name.tar.gz" "$name"
ls -lh "dist/$name.tar.gz"
if [[ ${1:-} == --tests ]]; then
    tests=dist/tests/out
    rm -rf dist/tests && mkdir -p "$tests/gpu"
    cp out/bb-probe out/bb-gpu-capabilities out/*-test "$tests/"
    cp out/gpu/libbbgpu.dylib out/gpu/*-test "$tests/gpu/"
    python3 packaging/macos_bundle.py "$tests" "$tests"/*-test "$tests"/bb-* "$tests"/gpu/* > /dev/null
    tar -C dist/tests -czf dist/macos-x86_64-tests.tar.gz out
    ls -lh dist/macos-x86_64-tests.tar.gz
fi
