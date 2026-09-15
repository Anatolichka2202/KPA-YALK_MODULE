# Legacy KTMA/UBSI procedure sources

This directory is intentionally outside every compiled target.

`ubsi_procedures_yvp_v7.cpp` is retained only as historical commissioning work. It contains an executable V7+ISD YVP sequence that is **not** the current production backend because its switching map and measurement sequence were not confirmed on the real stand. The active production alias is implemented in `../procedures/ubsi_procedures_v7.cpp` and remains fail-safe (`INCOMPLETE`) until that physical mapping is confirmed.

Do not add files from this directory to `ktma_ubsi_procedures` without a separate commissioning decision and tests. The obsolete alternate registration entrypoint was removed because the active `ubsi_procedures_v7.cpp` already owns the final `registerUbsiProcedures()` symbol.
