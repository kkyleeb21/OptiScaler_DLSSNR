# D18 follow-up work

This list separates D18-owned work from inherited OptiScaler DLSSNR cleanup. Completed work belongs in
Git history and release notes, not in this file.

## D18-owned

- **P0 — GPU-fenced feature/resource retirement.** Replace the inherited fixed 32-evaluate retirement
  heuristic for D18-triggered feature and ratio rebuilds with queue fence values. Preserve in-game A/B
  switching; use restart-required only as a failure fallback.
- **P1 — Reproducible binary provenance.** Generate a build manifest at binary compile time containing
  source HEAD and dirty state, MSVC/MSBuild and DXC versions, exact solution/shader commands, submodule
  revisions, and output hashes. Keep the release packager's binary hash guards as an independent check.
- **P1 — Game-directory scope guard.** Reject drive roots and require at least one top-level game EXE
  before installation. Keep this generic enough for all supported games.

## Inherited from upstream OptiScaler DLSSNR

- Disable the public multi-frame Capture controls until readback completion is guarded by a real GPU
  fence, or replace the inherited fixed-frame delay with fence-based completion and a bounded capture
  budget. Eight before/after RGBA16F frame pairs at 4K can approach 1 GiB before filesystem overhead.
- Decide whether to remove the unwired `AutoCapture` option or implement it only after Capture has a
  real GPU-fence/readback completion path. Its current fixed-frame waiting policy is not a completion
  guarantee.
- Update stale INI comments for exposure-derived White Point, two-sided `MaxRatio`, and live feature
  rebuild behavior.
- Remove unused `kAutoCaptureAfterFrames`, `g_autoCaptureDone`, `kSettleFrames`, and `settledAt` after
  confirming upstream has no pending integration for them.
