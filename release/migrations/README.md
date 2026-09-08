# MilTech Station migration contract

Persistent-data migrations are release-engineering operations. They may transform file/database layout and schemas, but they must not silently change product semantics.

## Registry

`index.json` is the ordered migration graph. Each migration entry has this shape:

```json
{
  "id": "001-example",
  "from_version": "1.0.0",
  "to_version": "1.1.0",
  "script": "001-example.ps1",
  "backup_paths": [
    "db",
    "config"
  ]
}
```

Rules:

- `id` must be unique and filesystem-safe;
- `from_version` and `to_version` are explicit; no wildcard migrations;
- `to_version` must be greater than `from_version`;
- `script` must remain inside this migration directory;
- every path modified by a migration must be declared in `backup_paths` unless it is safely recreatable and documented as such;
- a migration script must be idempotent or explicitly detect that it has already completed;
- a migration must validate its result and throw on failure;
- a migration must never reinterpret DUT/test data or product verdicts.

## Script interface

A migration script is invoked as:

```powershell
<migration.ps1> `
  -DataRoot <absolute-path> `
  -FromVersion <version> `
  -ToVersion <version>
```

The script must terminate successfully only after its own validation succeeds.

## Runner

Example:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/release/Invoke-StationMigrations.ps1 `
  -DataRoot "C:\ProgramData\MilTech\Station" `
  -CurrentVersion 1.0.0 `
  -TargetVersion 1.1.0
```

Before each migration, the runner snapshots every declared `backup_paths` entry to:

```text
<DataRoot>\backups\release\<UTC timestamp>-<migration-id>\...
```

If the migration throws/fails, those declared paths are restored before the update is reported failed.

After the complete chain succeeds, the runner atomically replaces `.miltech-release-state.json` with the target version and applied migration IDs.

## Safety invariant

If the registry has no unambiguous path from the installed version to the requested target version, the runner stops. It does not skip migrations, select a "nearest" migration or infer schema compatibility.

`index.json` is intentionally empty in the release-foundation commit because the common Station DataRoot has not yet been integrated into the runtime. The first real migration will be added together with that DataRoot contract, so that a migration moves data to a location the application actually uses.
