# Meshtastic core (vendored)

Copy of `src/` from [meshtastic/firmware](https://github.com/meshtastic/firmware) at commit `647831b`
(2.8.3, 2026-11-17), GPL-3.0 (see `LICENSE`). `main.cpp`, the screen, input and Bluetooth layers are not compiled:
`library.json` selects the mesh core, and `firmware/src/lora/` provides the glue (globals, init, BLE transport).
`variant/variant.h` is Meshtastic's T-Watch S3 variant.

Local changes are marked with `// twatch:` comments. Update by re-copying a newer tag and re-applying them.
"Meshtastic" is a trademark of Meshtastic LLC; this firmware is compatible with, not part of, Meshtastic.
