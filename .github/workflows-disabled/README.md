# Disabled upstream workflows

`upstream-nightly-build.yml` is preserved here for upstream reference only. It is intentionally kept
outside `.github/workflows`, so GitHub Actions cannot schedule or run it.

This fork publishes the D18 research build deliberately through its guarded community installer. The
upstream nightly workflow instead packages a standard OptiScaler build and bypasses D18 Runtime
patching, validation, backup and installation documentation, which would make its artifacts ambiguous
on this research fork.
