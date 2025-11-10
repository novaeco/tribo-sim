# UVI Integration Build Attempts

## Context

- Commit: a90433a27f47342b7ba114146988df87c156ad04
- Branch: work
- Date: 2025-11-10T15:23:15Z

## Commands Executed

```bash
(cd firmware/dome && idf.py build)
(cd firmware/controller && idf.py build)
(cd firmware/panel && idf.py build)
```

## Outcome

All builds failed immediately because `idf.py` is not available in the current CI/runtime environment. ESP-IDF needs to be installed and the `idf.py` helper placed on the PATH.

## Next Steps

1. Install ESP-IDF v5.x toolchain and export the environment (`. $IDF_PATH/export.sh`).
2. Re-run the three `idf.py build` commands above.
3. Capture resulting firmware artifacts (`.bin`, `.elf`) for hardware smoke testing.
4. Proceed with sensor calibration using production hardware once builds succeed.
