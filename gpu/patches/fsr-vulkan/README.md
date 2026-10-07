# Local changes to gpu/third_party/fsr-vulkan

The submodule points at upstream FireBurn/FSR-Vulkan (`c64f093`). `build.sh` applies the
patches here to its working tree when they are not applied yet:

- `0001-...`: `BB_FSR4_PROFILE` (GPU time per FSR 4 pass) and `BB_FSR4_STATS` (driver
  statistics of each pass) in the FSR 4 v07 provider.
- `0002-...`: the FSR 3.1.4 Vulkan backend finds device-local memory on unified-memory devices:
  KosmicKrisp on Apple silicon has only a device-local type that is also host-visible, which
  the backend skipped, so FSR 3 could not start there. Test: `fsr3-memory-type-test`.
