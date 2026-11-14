# Contributing to PS5 Backup Center

Thanks for helping. The project is young and moves fast; the most useful contributions right now are:

1. **Bug reports** with clear steps (use the issue form).
2. **Translations** — 19 languages are supported; native speakers who can review wording are very welcome.
3. **Hardware reports** — which NAS, which emulator versions, which storage setups work for you.
4. **Code** — small, focused pull requests. Please open an issue first for anything larger than a fix.

## Principles every change follows

- **Protect first.** Nothing on a console, disk or NAS is deleted or overwritten without an explicit confirmation. Restores are verified and make a safety copy. A running game is never touched.
- **TV and web together.** Every feature and option ships on both, with the same wording and order.
- **Detected, not hard-coded.** Sources (emulators, disks, devices) are described as data and detected; no title ID outside its source block.
- **Plain words.** User-facing text says what happens, in short sentences. No jargon on screen.
- **Credit everything.** Third-party code must be GPL-3.0-compatible and listed in `engine2/web/credits.json` (shown in the app).

## Building

Requirements: macOS or Linux, the PS5 Payload SDK and LLVM/Clang (see `toolchain/`), Python 3.

```sh
python3 build-system/build.py            # engine + app + package (incremental)
python3 build-system/build.py --clean    # from scratch (~2 min, reproducible)
python3 build-system/build.py engine|app|package
```

Preview the TV app on a Mac (reads a console's engine, read-only):

```sh
./app2/build-preview.sh "wait3000,shot,r1,wait3000,shot"   # PNGs in app2/out/preview/
```

The web UI is plain HTML/CSS/JS in `engine2/web/` — no build step.

## Repository layout

| Folder | What |
|---|---|
| `engine2/` | The engine: scanner, sources, destinations, tasks, web server, API `/api/v2/…` |
| `engine2/web/` | Web UI (served by the engine) and `credits.json` |
| `app2/` | TV app (native PS5 app, `PPSA99950`) |
| `build-system/` | Build scripts, `changelog.json` (single source of the version) |
| `docs/` | Plan, studies, references |

## Pull requests

- One topic per PR; describe the change from the user's side.
- Add your entry to `build-system/changelog.json` (`new`, `improved` or `fixed`).
- Attach TV and web screenshots for anything visible.
- By contributing you agree that your work is licensed under GPL-3.0-or-later.

## Code of conduct

Be kind and specific. See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
