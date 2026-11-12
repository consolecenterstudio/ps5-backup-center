# Contributing to PS5 Backup Center

Thanks for helping. The project is young and moves fast; the most useful contributions right now are:

1. **Bug reports** with clear steps (use the [bug report form](https://github.com/consolecenterstudio/ps5-backup-center/issues/new?template=bug_report.yml)).
2. **Translations** — 19 languages are supported; native speakers who can review wording are very welcome.
3. **Hardware reports** — which NAS, which emulator versions, which storage setups work for you.
4. **Code** — small, focused pull requests. Please open an issue first for anything larger than a fix.

## Principles every change follows

- **Protect first.** Nothing on a console, disk or NAS is deleted or overwritten without an explicit confirmation. Restores are verified and make a safety copy. A running game is never touched.
- **TV and web together.** Every feature and option ships on both, with the same wording and order.
- **Detected, not hard-coded.** Sources (emulators, disks, devices) are described as data and detected; no title ID outside its source block.
- **Plain words.** User-facing text says what happens, in short sentences. No jargon on screen.
- **Credit everything.** Third-party code must be GPL-3.0-compatible and listed in `engine/web/credits.json` (shown in the app).

For setup and troubleshooting, browse the [user guides](docs/guides/README.md). For a feature idea, use the [feature request form](https://github.com/consolecenterstudio/ps5-backup-center/issues/new?template=feature_request.yml). For vulnerabilities, follow the [security policy](SECURITY.md).

## Building

Build with `./build.sh` (requirements are listed at the top of the file). The web interface lives in `engine/web/` and needs no build step.

## Repository layout

| Folder | What |
|---|---|
| `engine/` | The engine (`pbc-engine2.elf`): scanner, sources, destinations, tasks, web server, API `/api/v2/…` |
| `engine/web/` | Web UI (served by the engine) and `credits.json` |
| `app/` | TV app (`eboot.bin`) and `app/package/PPSA99950/`, the app folder as installed on the console |
| `build.sh` | Builds the engine and the TV app, then the app folder and its zip |
| `docs/` | User guides and feature documentation |

## Pull requests

- One topic per PR; describe the change from the user's side.
- Describe the user-facing change in plain words in the pull request.
- Attach TV and web screenshots for anything visible.
- By contributing you agree that your work is licensed under GPL-3.0-or-later.

## Code of conduct

Be kind and specific. See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
