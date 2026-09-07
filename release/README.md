# MilTech Station — release foundation

This directory owns delivery mechanics only: packaging, version/build metadata, release manifests, migration contracts and later installer/updater assets. Product algorithms, equipment procedures and UI/UX do not belong here.

## Current release slice

The first slice establishes a reproducible staging boundary for the existing KTMA application:

1. the canonical product version remains `project(... VERSION ...)` in the root `CMakeLists.txt`;
2. CMake generates `build-info.json` with version, Git SHA, UTC build time and release channel;
3. `cmake --install` creates a controlled staging payload under `app/`;
4. `windeployqt` resolves Qt runtime dependencies;
5. `New-ReleaseManifest.ps1` records every staged file with size and SHA-256;
6. `Test-ReleaseManifest.ps1` rejects missing, modified or untracked payload files;
7. migration runner infrastructure provides declared backup + rollback for future persistent-data migrations.

SHA-256 in this slice provides integrity checking only. It does **not** authenticate the publisher. Network auto-update must remain disabled until release signing and updater trust policy are implemented.

## Configure and build

Example for a single-config Windows toolchain:

```powershell
cmake -S . -B build-release `
  -DCMAKE_BUILD_TYPE=Release `
  -DMILTECH_RELEASE_CHANNEL=internal

cmake --build build-release --parallel
```

For CI reproducibility, the pipeline may also provide:

```text
-DMILTECH_GIT_SHA=<full commit sha>
-DMILTECH_BUILD_UTC=<ISO-8601 UTC timestamp>
```

## Produce a staged release

```powershell
powershell -ExecutionPolicy Bypass -File scripts/release/Stage-WindowsRelease.ps1 `
  -BuildDir build-release `
  -QtBinDir <path-to-the-Qt-bin-directory>
```

Output:

```text
out/release/MilTechStation-KTMA-<version>-<channel>/
  app/
    OrbitaDesktop.exe
    build-info.json
    ...Qt/runtime/plugin/product payload...
  release-manifest.json
  release-manifest.sha256
```

The staging script creates a temporary directory, deploys and verifies the complete payload there, and only then moves it to the final artifact path. A failed stage is not published as a finished artifact.

## Release channels

CMake accepts the following channel values:

- `internal` — developer/integration builds;
- `pilot` — controlled bench/pilot rollout;
- `stable` — approved production rollout.

Channel promotion must create a new signed release record; a mutable "latest" folder is not a release identity.

## Transitional runtime-data layout

The current application still reads several KTMA runtime resources next to `OrbitaDesktop.exe`. Therefore this first slice intentionally stages the same compatible layout.

This is **not** the final data-placement contract. The target delivery topology is:

```text
Program Files\MilTech\Station\...     immutable installed application
ProgramData\MilTech\Station\...       persistent databases/config/history/backups
```

Moving files to ProgramData before the application consumes a common DataRoot would produce a broken installer. DataRoot integration plus migration of existing side-by-side data is the next release-engineering slice.

## Migration contract

See `release/migrations/README.md`. The runner refuses to invent a migration path: if no explicit version-to-version migration exists, update fails instead of guessing.

## Not implemented yet

The following are deliberately outside this first commit and remain blockers before production auto-update:

- common Station DataRoot and migration of existing installations;
- a single displayed application version (the desktop source still contains legacy hard-coded version text and must be connected to the canonical build version in a narrowly scoped change);
- Qt Installer Framework offline installer/bootstrapper;
- publisher/code signing and signed update metadata;
- update repository/channel publication;
- idle-state update orchestration and executable swap/rollback;
- CI release workflow and artifact retention policy.
