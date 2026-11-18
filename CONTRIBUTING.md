# Contributing

Thanks for your interest! Bug reports, ideas, new watch faces, translations and fixes are all welcome.

## Before you start
- Read [docs/dev-workflow.md](docs/dev-workflow.md): build, OTA, the HTTP debug API and the verified hardware quirks
  (light-sleep wake lines, BLE in power-manager sleep, build-cache traps). It saves hours.
- For bigger changes open an issue first, so we can agree on the approach.
- Decisions live in [docs/adr/](docs/adr/). If your change overturns one, add a new ADR.

## Development setup
```sh
scripts/setup.sh                                          # secrets.h + ota_secrets.ini (gitignored)
uvx --python 3.13 --from platformio pio run -d firmware   # build
uvx --python 3.13 --from platformio pio test -d firmware -e native   # host unit tests
scripts/flash_usb.sh                                       # flash over USB
firmware/tools/ota_upload.sh                               # or over Wi-Fi (watch on a charger)
```

## Guidelines
- **Keep it small and readable.** One module per concern (`src/hw/*` board services, `src/ui/*` screens,
  `lib/core/*` pure logic). No new abstraction layers without a reason.
- **Pure logic goes to `firmware/lib/core/` with host tests** (`firmware/test/`, Unity). Hardware code is
  verified on the device — say in the PR what you checked on a real watch and what is untested.
- **Power matters.** Anything that runs while the screen is off must not keep the CPU awake. Check with
  `GET /debug?pm=1` and `GET /stats` (see [docs/power/README.md](docs/power/README.md)).
- **Never commit secrets.** `firmware/include/secrets.h` and `firmware/ota_secrets.ini` are gitignored; keep it so.
- Changing `firmware/platformio.ini` triggers a full rebuild (~7–10 min) — batch such changes.
- Code style: C++17, 4-space indent, `camelCase` functions, `kConstants`, `s_` for file statics,
  comments explain *why*. Commit messages: short imperative subject (`ui: swipe to dismiss`).

## Adding a watch face
1. Copy a face in `firmware/src/ui/faces/` (e.g. `face_minimal.cpp`): it exposes `build()` and `update(FaceData)`.
2. Register it in `faces.cpp`.
3. If it needs new glyphs or sizes, add a line to `firmware/tools/fonts/gen.sh` and regenerate.
4. Add a screenshot (`GET /debug?face=N`, then `GET /screenshot`) to the PR.

## Translations
UI strings are Russian and live in `firmware/src/ui/ui.cpp`, the faces and `firmware/lib/core/ru_text.*`.
A clean i18n layer does not exist yet — proposals are welcome (open an issue first).

## Pull requests
- One topic per PR, describe *what* and *why*, list what you verified on hardware.
- Make sure `pio run` and `pio test -e native` pass.
- By contributing you agree that your contribution is licensed under the [MIT License](LICENSE).

## Code of conduct
Be kind and constructive. We follow the [Contributor Covenant](CODE_OF_CONDUCT.md).
