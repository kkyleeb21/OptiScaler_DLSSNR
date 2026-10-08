# D18 follow-up work

This list separates D18-owned work from inherited OptiScaler DLSSNR cleanup. Completed work belongs in
Git history and release notes, not in this file.

## Inherited from upstream OptiScaler DLSSNR

- Disable the public multi-frame Capture controls until readback completion is guarded by a real GPU
  fence, or replace the inherited fixed-frame delay with fence-based completion and a bounded capture
  budget. Eight before/after RGBA16F frame pairs at 4K can approach 1 GiB before filesystem overhead.
- Decide whether to remove the unwired `AutoCapture` option or implement it only after Capture has a
  real GPU-fence/readback completion path. Its current fixed-frame waiting policy is not a completion
  guarantee.
- Remove unused `kAutoCaptureAfterFrames`, `g_autoCaptureDone`, `kSettleFrames`, and `settledAt` after
  confirming upstream has no pending integration for them.
