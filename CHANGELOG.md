<!-- Release notes of PS5 Backup Center. -->

# Changelog

All user-facing changes to **PS5 Backup Center**, newest first. This is the same history the app shows in **Settings › What's new**.

> Entries are written in French, the app's first language, and shown here in English (the app's own translations). A few lines without a translation yet — mostly the « Web: » parity notes — stay in French.
> *Les entrées sont rédigées en français, la langue d'origine de l'app ; quelques lignes pas encore traduites restent en français.*

## Highlights (English)

| Version | What it brought |
|---|---|
| **2.99** | **Same emulator progress** (ProsperoEden, Églantine) on both PS5s; several games at once on the other PS5's library; **Support the project** (♥, QR codes, crypto, gifts, projects to fund); report a problem; easier task clearing; beta update channel. |
| **2.98** | **The other PS5's library**: browse the games of the other connected PS5 and back them up from here (TV and web), with "up to date here / there" badges; the other PS5's card shows its installed version. Lighter app package, updates from GitHub built in. |
| **2.93–2.97** | **Églantine** (Switch 2) saves; **uninstall a game**, here or on the other PS5 (saves backed up first, game image kept by default); deletion in Files with several confirmations; compressed images on every PS5. |
| **2.87–2.92** | **Updates**: automatic check, Home bubble, automatic install, roll back to the previous version, list of available versions; console power (restart); **Files**: copy or move anything between disks, NAS and consoles; Switch over SMB; rest mode removed. |
| **2.83–2.86** | **The NAS in Sync**: saves, then the NAS as a **depot** for active progress and apps. **DLC and updates** inventory, package library, Switch DLC copied between PS5s. Sync screens near-instant. |
| **2.80–2.82** | Sync of **active progress** (most recent wins, old one backed up first, never in a loop) and of **apps** and their **settings** between two PS5s. |
| **2.75–2.79** | **Sync tab**: copies of saves kept on both PS5s, chosen games, Home sync bubble. Execute permissions of transferred apps kept. |
| **2.66–2.74** | Backups visible on every console of the same PSN account, copies stored on the other PS5, *Retrieve here* / *Receive here*, delete a console save (hold 5 s), Home split into *here* and *elsewhere*. |
| **2.49–2.65** | **Send a game to another PS5**, installed there (format chosen for its system), compatibility with evidence (system, SDK, backport), image tools menu, more robust engine. |
| **2.44–2.48** | **Link between two PS5s**: pairing with one code, paired consoles in Devices, send a save, the other PS5 as a destination. |
| **2.37–2.43** | New layout (one question per tab), *To do* notification centre, *All versions*, progress catalogue, account IDs and *Resume on my account*. |
| **2.28–2.36** | 8 themes, sounds, **19 languages**, new backup management with a 30-day trash, progress of every copy, ignore an item. |
| **2.27** | **Manage backups** (TV and web): every version of every game on every disk and the NAS, duplicates by content, keep last 5, protect, delete with confirmation, restore. NAS listed under *Saves › By disk*. |
| **2.26** | Compare PS5 save versions **by content**: game progress, play time when recorded, save time, fingerprint. Old per-game USB backups readable and restorable. |
| **2.24** | **Restore a PS5 save straight from the NAS**, including old script-made backups. New status *Newer in a backup*. |
| **2.23** | One **PBC Backup** folder on the NAS, per platform; *Move under PBC Backup* renames existing folders without copying or deleting. |
| **2.20–2.22** | Web UI at parity with the TV; NAS folder picker; *Test connection* before saving a device. |
| **2.16–2.19** | **Transfers** tab, **full PS5 backup**, game **image tools** (.ffpfsc / .exfat, Game Compressor core built in), copy/move games internal ↔ USB, transfers from the NAS. |
| **2.13–2.15** | **FTPS**; NAS layout per platform with existing backups read as versions; device browser; full plan editor on the TV. |
| **2.7–2.12** | Home carousel and shelves, search, official names and icons for save-only games, HTTPS in the engine, editable destinations and emulators. |
| **2.0–2.6** | New TV app and engine; in-app updates; PS5 saves per account; Switch ↔ Eden sync via JKSV; scheduled plans with rotation; acknowledgements screen. |

---

## 2.99.5 — 2026-11-13

**Several games at once on the other PS5**

### New

- Other PS5's library: tick several items (L2 on the TV, a box on the web), then one action for all: back up their saves there, full backup there, or take the newer saves here.

## 2.99.4 — 2026-11-13

**Tasks: easier clearing**

### Improved

- Tasks: a visible Clear the list button, Clear all (Finished, Failed and Cancelled) and Options to remove a single task; nothing is ever removed from the disks.

## 2.99.3 — 2026-11-13

**Projects to fund, gifts and reports**

### New

- Settings › Projects to fund: one card per product (PS5 Center, PS5 Connector) with its highlights and a gauge in US dollars.
- Support the project: a Send a gift card (gift cards, subscriptions, test hardware).
- Settings › Report a problem: a QR code to the GitHub form and the versions to copy.

## 2.99.2 — 2026-11-13

**A little heart to support**

### New

- A heart at the top right opens Support the project: on the TV, R1 from Tasks then Cross; on the web, one click.

### Fixed

- QR codes always black on white, whatever the theme; full network names; the What's new line is no longer cut off.
- Settings › Updates and startup scrolls: the last options are reachable; Available versions shows the official site's list even when updates come from a local folder or a USB key.

## 2.99.1 — 2026-11-12

**Support the project**

### New

- Settings › Support the project: a thank-you note, one QR code per way to support (Ko-fi and Patreon soon; BTC, ETH, BNB and SOL with their network), addresses to copy on the web, and a thank-you wall with the nickname each supporter chose. The app stays free: nothing is locked.

### Improved

- The list of ways to support is read from our site at most once a day, only when the screen is open; the app keeps its own list if the site does not answer.

## 2.99.0 — 2026-11-12

**Same emulator progress**

### New

- Sync › Emulators card: ProsperoEden and Églantine saves stay in step between two PS5s, game by game and profile by profile (profiles matched by name). The most recent wins; the one it replaces is copied first; never while the emulator is open; never back and forth in a loop; if both moved on, you choose.

### Improved

- Each console only writes to its own folders: it fetches the most recent save from the other one, with the files' original times.

## 2.98.5 — 2026-11-12

**Beta channel**

### New

- Settings › Updates › Update channel: Stable (recommended, default) or Beta. On Beta, test versions arrive before stable ones, marked “beta” in Available versions; “Go back to the previous version” is still available.

### Improved

- A beta is never published as a GitHub release: Prospero Store and other catalogs only offer stable versions.

## 2.98.4 — 2026-11-12

**Start-up windows in the right order**

### Fixed

- After an update, "What's new" no longer hides the "Update installed" window and its countdown; the other PS5's card starts from its installed version.

## 2.98.3 — 2026-11-12

**Update already installed on the other PS5**

### Fixed

- Devices: when the new version is already installed on the other PS5 but not running yet, its card says so ("installed, active at next start") instead of offering to send it again.

## 2.98.2 — 2026-11-12

**Other PS5's library: touch-ups**

### Fixed

- Other PS5's library: the newer-progress badge is no longer cut off ("Newer here / there"); its saves without the game are shelved under "Saves without the game".

## 2.98.1 — 2026-11-12

**Lighter package**

### Improved

- Lighter package (−5 MB, 29 fewer files): only what the app uses, plus the required licences. Nothing changes in use.

## 2.98.0 — 2026-11-12

**The other PS5's library**

### New

- Library → “Console” chip (web: “Console” menu): view a paired PS5's library, with the same filters. Each game shows whether it's also here, missing here, or which side has the newer progress; “Missing here / Also here” filter. A banner reminds you that you're looking at the other console.
- Details of a game on the other PS5 (also from Home, “… · installed games” row): Get here, take or send the newer progress, back up its progress or make a full backup there, uninstall there. The work is done on the console that has the game; tracked in Tasks.
- No needless calls: the list comes from the engine's cache; “Rescan” queries the other PS5 once; an offline PS5 is never queried (last known list, actions unavailable).

### Fixed

- “Reopen” is no longer offered when the engine is older than the app.

## 2.97.0 — 2026-11-12

**Uninstall, here or remotely**

### New

- Uninstall a game: game page (web) or the library’s Options menu (TV) → “Uninstall from this PS5…”; keep the image (default) or delete it; TV: hold Square 5 s; web: type DESINSTALLER. Saves backed up first, never while playing.
- Remotely: the other PS5’s card → “Uninstall a game on …” (on the local network or over the Internet); the work and checks happen on the console that has the game. Tracked in Tasks (“Uninstall”).

## 2.96.1 — 2026-11-12

**Safer uninstall**

### Fixed

- Uninstall: if ShadowMountPlus is busy, the engine waits and retries; if a step fails, the game is put back in its list; the image is found even outside the list.

## 2.96.0 — 2026-11-12

**Uninstall a game (engine)**

### New

- The engine can uninstall a game from its PS5, including when the other PS5 asks: game closed, its saves backed up first, removed from ShadowMountPlus's list then uninstalled by it; its image or folder deleted only if asked. TV and web screens follow.

## 2.95.3 — 2026-11-12

**Compressed images on every PS5**

### Fixed

- Compressed image (.ffpfsc) that would not mount on a PS5 with an old system: the app forced a block size (64 KiB) on ShadowMountPlus that this system rejects. It no longer does; compressed images also mount on 3.21. The automatic switch to uncompressed from 2.95.2 is removed: it was not needed.

## 2.95.2 — 2026-11-12

**Games sent to an older PS5**

### Fixed

- Game sent to another PS5 in automatic format: if it has never mounted a compressed image but mounts uncompressed ones, it arrives uncompressed straight away. Before, the first send to a PS5 on an old system failed to mount.
- When an image replaces another one of the same game, the old one is now removed from ShadowMountPlus's list, even when its name has a space after the game number.

## 2.95.1 — 2026-11-12

**Engine restart offered, covers of old backups**

### Improved

- An update that is installed but not running yet opens an « Update X installed » window with a 15 s timer: without an answer, the engine restarts (never over a game or during a copy; « Later » to wait).

### Fixed

- After an update, the engine copy started with the console is replaced at once: a console restarted or crashed before the restart now comes back on the new version, not the old one.
- Old backup stored under the game's name only (e.g. « Split Fiction »): its game is found even when it isn't installed on this PS5, and the cover is back.

## 2.95.0 — 2026-11-12

**Files: delete**

### New

- Files: « Delete » the ticked items, with several confirmations (TV: the list, then hold Square 5 s; web: the list, « permanent », then type SUPPRIMER). Followed in Tasks. Never the system, games and saves managed by the console, installed apps, the engine folder or a disk root; not yet on the other PS5.

### Improved

- ProsperoEden's second folder (/data/eden) is now called « ProsperoEden (/data/eden) » instead of « Eden »: it is not a separate app.

## 2.94.0 — 2026-11-12

**Tasks: clear the list, details**

### New

- Tasks: « Clear the list » for finished, failed and cancelled tasks (one confirmation; only the list, nothing is removed from the disks). A Files copy whose copied files can still be removed stays in the list.
- Tasks: « Details » on a finished task: start, end, duration, average speed, and for Files what was copied, already there, renamed or failed.

## 2.93.1 — 2026-11-12

**No more overload when the other PS5 is off**

### Fixed

- When the other PS5 is off, sync now answers “offline” at once instead of waiting up to 3 minutes per request: the TV app's requests piled up until the console became unreachable (“server busy”).

## 2.93.0 — 2026-11-12

**Églantine (Switch 2)**

### New

- Églantine, the Switch 2 emulator: recognized (icon, game folder read from its settings, even on a USB drive), its .nup / .nsp / .xci games in the library under “Switch 2”, with their covers and saves; its settings in Settings › Sources and emulators.

### Fixed

- Églantine settings shared with the other PS5: only its settings file, never the connection to its save server.

## 2.92.4 — 2026-11-12

**Visible verification**

### Improved

- Files: while a copy is re-read to verify it, the TV and web now show “Verifying x %” with its own bar, instead of a bar stuck at 100 % for several minutes. Same when a file of the same size is already there and both are compared (“Comparing with the existing copy x %”).

## 2.92.3 — 2026-11-12

**Copy diagnostics**

### Improved

- The engine's status answer tells where a « Files » copy stands (step, file, since when): to understand a copy that does not finish.

## 2.92.2 — 2026-11-12

**Saturation diagnostics**

### Improved

- The engine's status answer lists the requests in progress (what, for how long): to find what fills the server when it answers « busy ».

## 2.92.1 — 2026-11-12

**Files: faster, resume, removal**

### New

- Files: each copy keeps a journal; after an interruption or an update it resumes without re-reading what is already done. A cancelled or failed copy offers « Remove the copied files… » (two confirmations): only what it created, never a file that was already there.

### Improved

- Files: reading and writing in parallel in 4 MB blocks (a copy from a NAS no longer waits for each network round trip: much faster on a gigabit network).
- Files (TV): a « Next: choose the destination » button under the list as soon as something is ticked.

### Fixed

- Updates: a file replaced by another of the same size within the same second could be taken as unchanged; the kept fingerprint is now forgotten on every replacement.

## 2.92.0 — 2026-11-12

**Files: copy or move anything**

### New

- Transfers › « Files » (TV and web): copy or move any files or folders from one place to another — disks of this PS5, of the other PS5, NAS, Switch. Nothing is ever overwritten (an identical file is skipped, a different one is kept alongside as « (2) »), every file is checked, and « Move » removes the originals only after a verified copy of everything. The engine's own folder stays out of reach.

## 2.91.1 — 2026-11-12

**Rest mode removed**

### Fixed

- Console power: « Put in rest mode » is removed (TV and web) and refused by the engine, because rest mode can crash a jailbroken console. Restart and turn off remain available.

## 2.91.0 — 2026-11-12

**Available versions**

### New

- Settings › Updates › « Available versions » (TV and web): the list of the latest versions from the source (date, title; installed, recommended or test). Pick one and it installs through the same checked path, newer or older; when going down, the version you left is no longer offered automatically.

## 2.90.9 — 2026-11-11

**Clean public package**

### Improved

- The published package no longer contains any path from the build machine nor the old 0.x engine, and the release notes only use example names.

### Fixed

- Automatic check: an unreadable update source no longer shows as an error (like an unreachable source).

## 2.90.8 — 2026-11-11

**Updates from an https site**

### New

- The update source can be an https address (GitHub, website) and a version repository: the address of latest.json leads by itself to the manifest of the latest version. Only changed files are downloaded, each one checked.

### Fixed

- Web: the version history showed the day before the real date in the Americas.

## 2.90.7 — 2026-11-11

**Go back to the previous version**

### New

- Settings › Updates › « Go back to version 2.x » (TV and web): if a version causes trouble, the previous one is reinstalled (files checked, reversible) and the engine restarts. The version you left is no longer offered or installed automatically; a manual check still shows it.

## 2.90.6 — 2026-11-11

**Engine always available**

### Fixed

- The engine's « I'm here » answer no longer copies the whole task list (about 750 KB) on each call, the game list no longer blocks the engine while it is built, and the answer catches the engine between two operations instead of replying « busy ». The app therefore sees an updated engine right away and offers to reopen.
- No more background sync computation with a device (other PS5, NAS) as long as no sync is set up with it: only when you open its page, result kept 5 min and redone as soon as a choice changes.

## 2.90.5 — 2026-11-11

**Slowness diagnostics**

### Improved

- The engine records any operation that blocks it for more than a quarter of a second (where and for how long), to find the cause of slowdowns.

## 2.90.4 — 2026-11-11

**Update download progress**

### New

- While an update downloads, the Home bubble and Settings show the percentage, the MB received, the current file (5 of 23), the speed and the time left (TV and web).

### Fixed

- Sync: only one computation at a time per device. With a well-filled NAS, preparing the saves sync took 2 minutes and was restarted in parallel over and over, until the engine was full (« server busy », beeps, reconnections). During a computation the app gets the latest result, and a long computation is no longer redone every 15 s.

## 2.90.3 — 2026-11-11

**No more disconnect loops**

### Fixed

- The TV app kept disconnecting and reconnecting, with the « The engine is not responding » window every 30 s: each time it came back it reloaded the whole catalogue, which slowed the engine and caused the next disconnection. It now reloads only what changed, considers itself disconnected only after 3 missed tries, and during an update or an engine restart it shows « Reconnecting… » for 30 s before offering a choice.

## 2.90.2 — 2026-11-11

**A busy engine is no longer « unreachable »**

### Fixed

- While the engine was preparing a sync, the TV app could think it was unreachable: it now answers « busy » in under a second, and the app keeps its display while waiting.

## 2.90.1 — 2026-11-11

**Lighter update check**

### Fixed

- During an update check the engine answered slowly and the TV app could show « The engine is not responding » when opening: the fingerprints of installed files are now kept from one check to the next.

## 2.90.0 — 2026-11-11

**Console power**

### New

- Settings › Updates › « Console power » (TV and web): restart, put in rest mode or turn off the PS5, after confirming; refused during a game or a copy. The engine does it itself (system functions), with no other app; a paired PS5 can also ask the other one.

## 2.89.9 — 2026-11-11

**Install automatically**

### New

- New « Install automatically » setting (Settings › Updates, TV and web; Off by default): an update that is found installs itself, never during a game, then the engine restarts when no copy is running.

## 2.89.8 — 2026-11-11

**Update announced on Home**

### New

- An update that is found now shows in an « UPDATE · 2.x available » bubble on Home (TV: Cross to install it; web: one click), and the PS5 shows a notification, once per version, even if the app is closed or a game is running.

## 2.89.7 — 2026-11-11

**Updates checked automatically**

### New

- Backup Center checks for an update when the engine starts, when the app opens (TV or web), when you go to Settings, and every hour. If the source doesn't answer, no error is shown.

## 2.89.6 — 2026-11-11

**Switch over SMB**

### New

- Add a Switch: choose FTP or SMB (TV and web). Over SMB, the engine reads the Switch's JKSV folder just like over FTP; the port changes from 5000 to 445.

## 2.89.5 — 2026-11-11

**Saves unmounted at startup**

### Fixed

- If the engine stopped abruptly while reading a save (crash, console switched off), the image left mounted is unmounted as soon as it starts, and its temporary copies are deleted.

## 2.89.4 — 2026-11-11

**The engine has its real name**

### Fixed

- When started by Payload Manager or the jailbreak's loader, the engine showed up under the generic name “payload.elf” in the process list: it now names itself “pbc-engine2.elf”.

## 2.89.3 — 2026-11-11

**Stable engine whoever launches it**

### Fixed

- The engine now picks its own thread stack size (512 KB): launched by Payload Manager or by its own restart, it got threads that were too small and crashed at startup.

## 2.89.2 — 2026-11-11

**Self-sufficient engine**

### Improved

- No more calls to Arsenal: the engine restarts and updates itself with its own launcher, and the TV app, only when no engine is running at opening, starts it through the jailbreak's loader (elfldr, etaHEN / kstuff).

### Fixed

- Restart through our built-in launcher: the new engine keeps its elevated privileges (it crashed at startup on some consoles).
- The engine regains its elevated privileges by itself as soon as it starts, whoever launches it (Payload Manager at power-on, the app, its own restart): it crashed when Payload Manager launched it.

## 2.89.1 — 2026-11-11

**A single engine during a restart**

### Fixed

- The TV app no longer starts a second engine while the engine restarts after an update (it only starts it by itself when opened, if it isn't running).

## 2.89.0 — 2026-11-11

**Remote PS5, over the Internet**

### New

- Devices › Remote PS5: pair a PS5 that isn't at home using an invitation code (single use, 24 h). It becomes a paired PS5 like any other: its disks, its games, send a game or a save, sync, update it or retrieve its version.
- Everything goes through an encrypted channel (ChaCha20-Poly1305, key derived from the pairing); only invited PS5s can get in, and the Internet link stays disabled until you enable it.
- Only one of the two PS5s needs to be reachable (port forwarding): the other one — LTE, Starlink, no public address — calls it and keeps a connection open, used in both directions.
- Test the connection: tells you exactly what is blocking (port closed on the other side, unknown address, pairing refused) and whether this PS5 is reachable from the Internet.
- Devices, on the web as on the TV: another PS5's card stays simple (Its games, Actions ⋯); its actions open in a menu.
- Another PS5's card shows its IP address (also when remote), its Backup Center version and its response time in ms.
- Remote PS5: choose which of the two opens the session (Automatic, this PS5 calls the other, or the other calls this PS5), and change it when you move; the other PS5 gets the reverse choice. A session already opened by the other is used first.
- A remote PS5's card also shows its local address.
- Settings › Updates › Engine startup: “Start with the console” (Payload Manager starts the engine at power-on; the option is removed the same way it is added) and the order of the loaders the app tries when the engine is not running (Arsenal, etaHEN's elfldr / kstuff), showing which ones are found on the console.

### Improved

- Engine restart after an update: Backup Center launches its new engine itself (elfldr loader built in), without any external app and without opening a port. If the restart doesn't take, you're told after 45 s.

## 2.88.11 — 2026-11-10

**Mirror updates trial**

### Improved

- Trial version: used to check sending, the « Reopen » window with its timer and fetching from the other PS5. No other change.

## 2.88.10 — 2026-11-10

**Reopen the app with a timer**

### Improved

- « Backup Center X is ready »: a 15 s bar; without « Later » or Circle, the app reopens by itself — only if it is in the foreground, never over a game.

### Fixed

- The Home bubble « UPDATE · X running » opens that same confirmation window; before, it started an invisible countdown that a Cross press cancelled (« Restart cancelled »).
- « Fetch X from… » answered « Unknown or unpaired PS5 »: the console introduced itself to the other one with the wrong identifier. The sending PS5 also recognises it by its address (consoles not yet updated can therefore fetch).

## 2.88.9 — 2026-11-10

**Update finish shown in green**

### Improved

- Devices: when an update finishes (sent or fetched), the badge turns green « Update: X running » for one minute, then disappears.

## 2.88.8 — 2026-11-10

**Mirror updates**

### New

- Devices: the older PS5 shows “Update available” on the other one’s card and can “Get” its version from it, just as the other can send it: it works both ways.
- After an update, if the open app is older than the engine, it offers to reopen (once per version, never automatically); a bubble on the Home screen lets you do it later.

## 2.88.7 — 2026-11-10

**Version badge only when useful**

### Improved

- Devices: another PS5's badge is centred above its name; it shows during an update, if it failed, if the new version isn't running yet or if the versions differ, and disappears when both PS5s are on the same version.

## 2.88.6 — 2026-11-10

**Readable update progress**

### Fixed

- Devices: another PS5's update progress is shown in a badge above its name; with three disks listed, it spilled out of the card.

## 2.88.5 — 2026-11-10

**Less memory**

### New

- On another PS5's card (Devices): « Restart the Backup Center engine » always offered, and « Close Backup Center » (only if it is the app open there, never a game).

### Fixed

- Computing app fingerprints (app sync) read each eboot.bin whole into memory: up to 780 MB used at once, which could make the system close a game (CE-15773-3). It now reads in small chunks.
- Installing a backport from a copy on another disk no longer loads each file whole into memory.

## 2.88.4 — 2026-11-10

**Memory diagnostics**

### Improved

- The engine logs the operation or request that makes its memory rise (more than 16 MB at once), to cut these peaks.

## 2.88.3 — 2026-11-10

**Restart after an update**

### Fixed

- A new engine now properly replaces an older one during an update (2.88.2 quit, thinking it was a duplicate).

## 2.88.2 — 2026-11-10

**No more duplicate engines (error CE-15773-3)**

### New

- Devices › other PS5: « Restart its engine » when a version is installed there but not yet running, and « Open Backup Center » on it (warning if a game is running).
- Home: an « Update » bubble during download and installation, then « 2.88.x installed · received from … » with the engine restart.

### Improved

- Updating one PS5 from the other also works with an older version (before 2.88): its source setting is put back as it was afterwards.

### Fixed

- Error CE-15773-3 (no game or app launches until a restart): duplicate engines stayed in memory. An engine restarted by the app no longer replaces the one that responds; only an update replaces it, and a stopped engine exits immediately.

## 2.88.1 — 2026-11-10

**Engine memory under watch**

### Fixed

- The engine decodes one cover at a time (less memory taken at once) and reports its memory in its status, to fix error CE-15773-3 (no game launches until a restart).
- Web › Devices: the “Update to …” button is readable, and progress shows on the other PS5's card.

## 2.88.0 — 2026-11-10

**Update a PS5 from the other one**

### New

- Devices › another, older PS5: « Update to … ». It downloads this PS5's version, verifies every file (size and checksum), keeps the old version if anything fails, then restarts its engine. Progress is shown on its card.

### Improved

- No more need for a computer to update a second console: one up-to-date PS5 is enough.

## 2.87.2 — 2026-11-10

**SMB form on the web**

### Fixed

- Web › Add a NAS: with SMB, the title says “Add an SMB share” and the FTPS encryption row goes away.

## 2.87.1 — 2026-11-10

**Readable device form**

### Fixed

- Devices › Add a NAS (TV): with the protocol choice, the buttons no longer cover the help at the bottom of the form.

## 2.87.0 — 2026-11-10

**NAS over SMB**

### New

- Devices › Add a NAS: choose FTP / FTPS or SMB (Windows, Synology, QNAP… shares). The first folder is the share (e.g. /Share/Backups). An SMB NAS does everything an FTP one does: saves, sync, depot, transfers.

### Improved

- Sync › NAS › Apps: “Not in the depot yet” instead of “Not installed on the NAS”.

## 2.86.0 — 2026-11-10

**The NAS as a depot: active progress and apps**

### New

- Sync › NAS › Active progress: the NAS keeps the most recent progress of each chosen game. Newer here: a copy goes to it; newer on the NAS: it is restored here, the one here backed up first. Your old “PS5 backup” folders count too.
- Sync › NAS › Apps: the NAS acts as an app depot. Newer here: it is put on the NAS; newer on the NAS: it is installed here, replacing the old one (permissions re-read, rolled back if the installation fails).

### Improved

- Same rule as between two PS5s: the most recent wins, never rolled back, and a log prevents endless back-and-forth.

**Web:** Mêmes cartes et mêmes écrans côté web.


## 2.85.7 — 2026-11-10

**Near-instant Sync**

### Improved

- Sync: the engine keeps each device's plans ready (saves, active progress, apps, DLC); cards and screens show up right away and refresh every 15 s. A choice or an upgrade triggers an immediate recalculation.

**Web:** Le web en profite aussi (même moteur).


## 2.85.6 — 2026-11-10

**Sync: the cards finally fill in on the TV**

### Fixed

- Sync: on the console, answers over 8 KB (Active progress, Apps, DLC, Settings) reached the TV app truncated, so it stayed on “reading…”. The engine now waits for the app to read instead of giving up.

**Web:** Le web en profite aussi (même moteur).


## 2.85.5 — 2026-11-10

**Sync diagnostics (answers)**

### Improved

- For each sync request from the TV app, the engine notes the answer it sent: code, size, duration.

**Web:** Sans objet : diagnostic du moteur seulement.


## 2.85.4 — 2026-11-10

**Sync diagnostics (continued)**

### Improved

- The TV app reports to the engine any sync answer it can't use, with the reason.

**Web:** Sans objet : diagnostic de l'appli télé seulement.


## 2.85.3 — 2026-11-10

**Sync diagnostics**

### Improved

- The engine keeps track of the TV app's latest requests, to understand a Sync screen stuck on “reading…”.

**Web:** Sans objet : diagnostic du moteur seulement.


## 2.85.2 — 2026-11-10

**Sync: no more endless “reading…”**

### Fixed

- Sync: if the other PS5 doesn't answer (restart, rest mode), the Active progress, Apps, DLC and Settings cards say “other PS5 unreachable” instead of “reading…”, and retry every 20 s.

**Web:** Même comportement côté web.


## 2.85.1 — 2026-11-10

**Switch covers everywhere**

### Fixed

- Sync: a Switch game missing from this PS5 shows its cover (Eden's, from the other PS5, cached) instead of a text tile. Sync › DLC: the “Packages stored here” line no longer touches the edge.

**Web:** Même jaquette côté web.


## 2.85.0 — 2026-11-10

**Switch DLC: complete from one PS5 to the other**

### New

- Sync › DLC: Triangle completes Switch games: only the missing DLC and update are copied into the game's folder, here or on the other PS5. Nothing is overwritten or deleted, never while Eden is open, and free space is checked first.
- Sync › DLC: packages (.pkg) stored on your USB drives and download folders appear under their game, with their location. They will be used by the installer.

### Improved

- Each PS5 only writes to its own folders: the receiving one fetches the files, and the other only serves the packages it has declared itself.

**Web:** Même bouton « Compléter… » côté web.


## 2.84.0 — 2026-11-10

**Sync: DLC and updates inventory**

### New

- Sync › DLC: for each game, the add-ons and update on each side, and what’s missing where (↓ here, ↑ there). A game missing on one side points to Transfers. Read-only: nothing is copied.

### Improved

- The package storage location (USB drive on the console or NAS) is set: it will be used at the install step.

**Web:** Même écran DLC côté web.


## 2.83.2 — 2026-11-10

**Home: the NAS picture**

### Fixed

- Home: a NAS Sync bubble now shows the NAS (no longer a PS5). Sync › NAS: the Saves card text is no longer cut off.

**Web:** Même texte côté web.


## 2.83.1 — 2026-11-10

**Sync: the right wording for the NAS**

### Fixed

- Sync › NAS: the Saves card now talks about the NAS (no longer “the other PS5”).

**Web:** Même texte côté web.


## 2.83.0 — 2026-11-10

**Sync: the NAS and settings**

### New

- Sync: the NAS appears alongside the PS5 and Switch consoles. Its Saves sync just like with another PS5: chosen games keep their saves on both sides, nothing is overwritten or deleted, and nothing is ever copied back and forth in a loop.
- Sync › Settings: the card opens on apps that have settings (“With settings” filter).

**Web:** Même NAS et même carte Réglages côté web.


## 2.82.1 — 2026-11-09

**Apps: more patient comparison**

### Fixed

- Sync › Apps: the first comparison no longer fails while the other PS5 computes its apps’ fingerprints (up to 3 minutes).

**Web:** Même correctif côté web (il est dans le moteur).


## 2.82.0 — 2026-11-09

**Apps: settings and fingerprint**

### New

- Sync › Apps: for each app, “App only”, “App + settings” or “Settings only” (L2 on TV). Settings only move between the same version; the receiving console files them under its own profile, after a backup copy.
- Settings: profiles for Shadow Mount+, Prospero Themes, ProsperoEden, Eden, Porpoise and Breeze; for others, only small config files — never games, saves, caches, logs or console-specific data. Same log as Progress: never looping, to decide if changed on both sides.

### Improved

- Apps: same version but different files (rebuilt homebrew) → to decide, never silently replaced again.
- Apps: the header counts chosen apps (not games).

**Web:** Mêmes modes, mêmes réglages et mêmes choix en cas de conflit côté web.


## 2.81.0 — 2026-11-09

**Sync: apps**

### New

- Sync › Apps: the same apps, at the same version, on both PS5s. Same version: nothing; newer on one side: it replaces the older one on the other side (same rule as app updates); missing: installed if you chose it.
- An app update never keeps two versions: the old one is removed only once the new one is installed, verified and its execute permissions re-read; otherwise it is put back.

**Web:** Même section Applis côté web.


## 2.80.2 — 2026-11-09

**Active progress: never in a loop**

### New

- Active progress: filters (chosen, to update, to decide, to the other PS5, to here).

### Improved

- Active progress: each sync is recorded on both sides. Progress that just arrived is no longer sent back in a loop; only the one you played again goes out. Played on both sides: nothing goes until you choose which one to keep.

**Web:** Mêmes filtres et même choix en cas de conflit côté web.


## 2.80.1 — 2026-11-09

**Active progress: the same rule as resuming**

### New

- Active progress: choose the account on each side (by default, the same PSN account); the progress is put back on the user ID of the console that receives it.

### Improved

- Active progress: same rule as resuming a game — more play time first, then the newest; if identical, nothing is touched.

**Web:** Même choix des comptes côté web.


## 2.80.0 — 2026-11-09

**Sync: active progress**

### New

- Sync › Active progress: choose the games whose progress should be the same on both PS5s. For each one, the time the game wrote its progress is compared: the most recent wins and goes to the other console, on the right account.
- Before progress is replaced, it is backed up (Saves tab): nothing is lost. Never while the game is running, and always after you confirm.

**Web:** Même section Parties actives côté web.


## 2.79.4 — 2026-11-09

**Devices: the IP address of each paired PS5 is shown.**

**Web:** Adresse IP des PS5 jumelées dans Appareils côté web aussi.


## 2.79.3 — 2026-11-09

**Transferred apps: execute permissions**

### Improved

- Devices: the IP address of each paired PS5 is shown.
- The tab bar stays balanced when a tab no longer fits (the space is shared out).

### Fixed

- Apps and folder games transferred from one PS5 to another: eboot.bin and modules keep their execute permission (they arrived as 0666 and the app refused to start). Original permissions are recorded in the copy, restored at install and re-read before saying “installed”; otherwise, a clear error.

**Web:** Adresse IP des PS5 jumelées dans Appareils côté web aussi.


## 2.79.2 — 2026-11-09

**Sync: direction filters**

### Improved

- Sync: the device column scrolls when there are many (Switch, NAS…).

### Fixed

- Choose games: the “↑ To the other PS5” and “↓ To here” filters are back.

**Web:** Mêmes filtres de direction côté web.


## 2.79.1 — 2026-11-09

**Sync: full names**

### Fixed

- Sync: section names (“Active progress”) are no longer cut off; “Soon” moves below.

**Web:** Le web affichait déjà les noms entiers.


## 2.79.0 — 2026-11-09

**Active progress and saves**

### New

- Sync: “Sync now” asks where to store saves when the other PS5 (or this one) has several drives.
- Sync: a “Whole games” card leads to Transfers (copy, compress, send a game).

### Improved

- One vocabulary everywhere: active progress is what's on the console; a save is a stored copy. The Backups tab is now called Saves, and the Sync section reminds you that saves aren't playable as is.

### Fixed

- A large save whose content can't be read (e.g. Gex) is no longer flagged “to check”.

**Web:** Mêmes noms, même choix des disques et même carte « Jeux entiers » côté web.


## 2.78.5 — 2026-11-09

**Sync: no more grey in the light theme**

### Fixed

- Sync: the big top panel and the Backups card follow the theme (no more grey background in the light theme).

**Web:** Le web suivait déjà le thème.


## 2.78.4 — 2026-11-09

**Sync: the section is called Backups**

### Improved

- Sync: the “Saves” section is now called “Backups” and says what it does: copy your backups to both PS5s, without touching the progress inside the games (that will be the Progress section).

### Fixed

- Home: the Sync bubble shows the PS5 picture without a grey background.

**Web:** Mêmes noms et même bulle côté web.


## 2.78.3 — 2026-11-09

**Sync: visible way back**

### Fixed

- Choose games: the help bar says Circle takes you back to the Sync page.

**Web:** Bouton « Retour à Synchro » côté web.


## 2.78.2 — 2026-11-09

**Distinct words**

### Fixed

- Translations: the Progress tab (saves on the console) and Saves (the copies) no longer share the same name.

**Web:** Mêmes noms côté web.


## 2.78.1 — 2026-11-09

**Sync: web touch-up**

### Fixed

- Web: the Saves card on the Sync tab keeps its shape.

**Web:** Correctif de la page Synchro côté web.


## 2.78.0 — 2026-11-09

**New Sync page**

### New

- Sync: a simpler page. Your devices on the left; at the top, where you stand with the other PS5 and a single “Sync now” button; below, separate sections: Saves, and soon Progress, Apps, DLC and Settings.
- Choose games: a switch per game, the date and play time on both sides, “Also add new games automatically”, choose all / remove all, and sending the whole game just once when it's missing on the other side.

### Improved

- The Sync bubble on Home speaks like the page (“No games chosen”, “N games to update”, “Everything is in sync”) and opens this PS5's page.

**Web:** Même page Synchro et même écran « Choisir les jeux » côté web.


## 2.77.5 — 2026-11-09

**Sync: the game is sent only once**

### Improved

- Copy sync: if you turn it on, a followed title's full game is sent only once, when the other side lacks it; after that, only its saves follow (never 150 GB re-sent because a save changed).

**Web:** Mêmes règles et mêmes mots côté web.


## 2.77.4 — 2026-11-09

**Sync: saves only by default**

### Improved

- Copy sync: followed titles send only their saves; full copies only if you choose, and then only the newest (never older versions of a game or app).
- Sync tab: one sentence says exactly what the sync touches (and doesn’t), and the size to send is shown.

**Web:** Même bouton « Sauvegardes seulement » et même explication côté web.


## 2.77.3 — 2026-11-09

**Sync: instant display**

### Fixed

- Sync tab: after “Follow everything” or “Follow nothing”, the card and summary change right away (before, you had to go back to Home).

**Web:** Côté web, la page se relisait déjà tout de suite.


## 2.77.2 — 2026-11-09

**Sync: follow everything or nothing**

### Improved

- Sync tab: the “Mode” button is replaced by “Follow everything” and “Follow nothing”, which start from scratch; the followed games list reminds “Options: all / none”.
- Home: the sync bubble shows the PS5 picture when no game is followed or everything is up to date.

**Web:** Mêmes boutons « Tout suivre » / « Ne rien suivre » côté web.


## 2.77.1 — 2026-11-09

**Sync: touch-ups**

### Fixed

- Sync tab: the other PS5's card counts like the bubble (followed games only); buttons wrap instead of overflowing.

**Web:** Même carte côté web.


## 2.77.0 — 2026-11-09

**Sync: followed games**

### New

- Copy sync: you choose the games followed per PS5 (none by default, or “all except excluded”); only those sync and count in the Home bubble — “No game followed”, “Followed games up to date” or “N to exchange”.
- Followed games list: filters (all, to here, to there, saves, full games), sort (name, date, size), newest of each game or all versions (to also send old saves), and for each copy its date, play time and progress.
- The controller's middle button (touchpad click) brings you back to Home from anywhere; shown at the bottom right of every screen.

### Improved

- Sync tab: “1 · Copy sync” and “2 · Save sync” clearly separated, with what each one touches.

### Fixed

- An app (e.g. Prospero Themes) stays labeled “App” everywhere — copies, other PS5, sync — instead of “PS5”.
- Sync tab: paired PS5s show up right away, even before the other console is first read.

**Web:** Mêmes jeux suivis, filtres, tri, bulle et étiquette « App » sur le web ; le raccourci Accueil est déjà là (logo et onglet).


## 2.76.3 — 2026-11-09

**Safer engine restart**

### Improved

- Update: the engine no longer goes silent if the old one is slow to free its place — it retries for 2 minutes and writes its status (engine-status.json); its port can no longer stay stuck because of an engine stopped abruptly.

**Web:** sans objet : moteur seulement


## 2.76.2 — 2026-11-09

**Clearer top bar**

### Improved

- Top bar redrawn: tabs keep their spacing and scroll with L1 / R1, small ‹ › arrows show the hidden ones; the status dot and version sit on one line next to the search button.

**Web:** sans objet : le web fait déjà défiler ses onglets


## 2.76.1 — 2026-11-09

**Sync: status right away**

### Fixed

- Sync: the status with the other PS5 shows as soon as you open it (it used to wait a minute).

**Web:** Correctif web et télé.


## 2.76.0 — 2026-11-09

**New Sync tab**

### New

- “Sync” tab (between Backups and Transfers): one card per device — each other paired PS5 and each Switch. For a PS5: “Copies · Backups” (sync, everything with full games, or tick the games), “Saves” (coming soon), history. For a Switch: “With Eden on this PS5” (the existing sync) and “With another Switch” (coming soon).

### Improved

- The tab bar tightens by itself when space runs short.
- Cards for another PS5 (Sync, Backups, Saves) show the PS5 picture.

**Web:** Même onglet Synchro sur le web.


## 2.75.0 — 2026-11-09

**Copy sync between PS5s**

### New

- Copy sync with the other PS5: Backups › the other PS5's card › Options (TV) or “Sync copies…” (web) — preview of what's missing on each side, then copying in the background; only additions, nothing is overwritten or deleted, the consoles' saves don't change. Full games apart (large), on request.
- Home › To do: “SYNC · <PS5>” bubble (copies up to date, or N copies to exchange → Cross: sync) and a progress bubble during the sync.
- Backups: moving copies to the trash now also requires holding Square for 5 seconds (TV) or the button for 5 seconds (web), NAS included; the screen reminds you these are copies, not the console's save.
- Home › To do: “NEWER COPY” bubble — a copy (here, on the NAS or on the other PS5) is newer than the save on the console; Cross opens Saves on those games to restore them.

### Improved

- Saves and Backups: a sentence under the title says what the tab shows (saves ON the console / COPIES elsewhere).
- Safety copies made before deleting a save are erased after 30 days, like the trash.
- Saves: a game played and backed up on the other PS5 now counts too (it shows “Newer backup” instead of “up to date”).

### Fixed

- Saves: a system app's save (e.g. LAPY20011 “Saved data”) is labeled “System data” and no longer counts as “to back up”.

**Web:** Même maintien 5 s et mêmes phrases d'aide.


## 2.74.0 — 2026-11-09

**Home: here, then elsewhere**

### New

- Home: “Console A · backups” row — every copy stored on the other PS5, even for games that are also here; Cross opens their versions.

### Improved

- Home in two parts: first what is on this console (recently played, PS5 games, PS4 games, saves without the game), then what is elsewhere, with a badge (available backups, the other PS5's installed games and backups), then retro and apps.

**Web:** Même ordre et mêmes pastilles sur l'accueil web.


## 2.73.0 — 2026-11-09

**Home: the other PS5's games**

### New

- Home: one “On Console A” row per paired PS5 — its installed games that aren't here; Cross: receive them here (drive, format, account), the other PS5 sends them and they get installed.

### Improved

- Home: “PS5 games” and “PS4 games” in two rows.

**Web:** Mêmes rangées « Sur … » et « Recevoir ici », PS5 et PS4 séparés.


## 2.72.0 — 2026-11-09

**Backups: one card per other PS5**

### New

- Backups: one card per paired PS5 at the top (“Console A · other PS5 · N copy(ies)”) to see at a glance everything stored there, even for games that are also here.

### Fixed

- Web: the other PS5's drives no longer show up as “missing drives”.

**Web:** Même carte par PS5 jumelée.


## 2.71.1 — 2026-11-09

**Home: saves without the game moved up**

### Improved

- Home: “Saves without the game” now comes right after games, before retro consoles and apps.

**Web:** Même ordre des rangées sur l'accueil.


## 2.71.0 — 2026-11-09

**Home: available backups**

### New

- Home: “Available backups” row — games with copies somewhere (here, on the NAS or on the other PS5) that are not on this console; Cross opens their versions to restore or retrieve.

### Improved

- Copy on a NAS this console doesn't have: the message suggests adding it in Devices to manage it from here.

### Fixed

- Saves and Backups: PS4 games (CUSA) missing from the library were labeled “PS5”; they now show “PS4” and their cover.

**Web:** Même rangée « Backups disponibles » sur l'accueil, même message pour le NAS.


## 2.70.1 — 2026-11-09

**Copies from the other PS5: covers and labels**

### Improved

- A game stored only on the other PS5 now says “N copy(ies) stored on …: can be retrieved here” and no longer offers “Delete…”.

### Fixed

- Covers: a console without an official icon (offline) uses the one from a paired PS5 that has the game.

**Web:** Mêmes libellés et même menu.


## 2.70.0 — 2026-11-09

**Backups: copies from the other PS5 too**

### New

- Backups also lists copies stored on paired PS5s (their drives, their NAS), labeled “Console B · USB 0”: wherever they are stored.
- On a save copy stored on the other PS5: “Retrieve here” — it is sent to this console and restored to the chosen account.

### Improved

- Copies from the other PS5 are never checked, suggested for cleanup or deleted from here: that's done from the console that stores them.

**Web:** Mêmes copies dans Backups, groupe « Rangées sur … », menu ⋯ « Récupérer ici ».


## 2.69.0 — 2026-11-09

**Delete: hold for 5 seconds**

### New

- Last step when deleting a console save: hold Square for 5 seconds (TV) or the button for 5 seconds (web), a bar fills up; releasing or Circle cancels.

### Improved

- Clear label everywhere: “Delete the console's save…”, and every confirmation reminds you it's the save on the console (your progress), not a copy in Backups.

**Web:** Même dernière étape : bouton à maintenir 5 s.


## 2.68.0 — 2026-11-09

**Clean up a save from anywhere**

### New

- “Delete save…” also in Saves and Backups (“Delete the console's save…”, copies stay).
- Without an up-to-date copy, deleting is still possible by forcing it: two confirmations, “Cancel” preselected on the last screen; a raw safety copy is always made.

### Fixed

- Covers: a game missing from the console (only its save or its copies) shows its official icon everywhere (Home “To do”, Saves, Backups).

**Web:** Mêmes actions et mêmes confirmations ; jaquettes des cartes « À faire » corrigées.


## 2.67.0 — 2026-11-09

**Delete a save from the console**

### New

- Details, Home and Library: “Delete save…” removes a game's save from the console, after confirmation — only if an up-to-date copy is in Backups (otherwise: back it up first), and a safety copy is made just before.

### Improved

- Saves and Backups: copies made on another console by the same PSN account are attached to this console's account (“up to date” status, direct restore).

### Fixed

- Backups: covers were missing for a game present only through its save.

**Web:** Même action dans la fiche et le menu « ⋯ » ; jaquettes corrigées dans Backups.


## 2.66.0 — 2026-11-09

**Backups visible on every console**

### New

- Home: Options button (TV) or “⋯” (web) on a cover: Open details, All versions, Ignore… — for example to set aside a save whose game is gone.

### Improved

- Backups: copies made on another console by the same PSN account now show up (accounts are recognized automatically, no ID to enter); a copy from another account stays visible, marked, and is never offered for cleanup.
- Backups: full backups stored on internal storage are listed too.
- Game details: Up and Down move between rows of buttons, to the nearest button, without skipping any.

**Web:** Menu « ⋯ » sur les jaquettes de l'Accueil ; Backups corrigé par le moteur (même vue).


## 2.65.1 — 2026-11-09

**A stuck disk no longer blocks everything**

### Improved

- Image status and compatibility: one read at a time; if a read hangs (stuck disk or mount), later requests get the last known result instead of piling up — the engine stays reachable.

**Web:** sans objet : moteur seulement


## 2.65.0 — 2026-11-09

**The engine no longer reads inside mounts**

### Improved

- To know whether a game is mounted, the engine reads the system mount table without waiting, instead of looking inside ShadowMountPlus mount folders: a broken mount can no longer block it.

**Web:** sans objet : moteur seulement


## 2.64.1 — 2026-11-09

**Move without touching the game’s registration**

### Fixed

- Image tools › Move: the ShadowMountPlus registration is updated only when moving the registered game itself; moving a working copy no longer replaces the real game’s registration.

**Web:** sans objet : moteur seulement


## 2.64.0 — 2026-11-09

**The backport travels with the game**

### New

- A game’s ShadowMountPlus backport (backports/<ID> folder) becomes part of the game: copied with it, sent with it to another PS5, and put back in place on install (never overwritten if one already exists).

**Web:** sans objet : moteur seulement ; la partie s'affiche déjà dans « Que sauvegarder ? » sur le web et la télé


## 2.63.2 — 2026-11-09

**Engine: always answers, even when stuck**

### Improved

- Engine: its status always answers, even if an operation gets stuck (it then says “busy”, since when and in which operation); a watchdog writes it to the log to diagnose a stuck disk.

**Web:** sans objet : moteur seulement


## 2.63.1 — 2026-11-09

**Browse the other PS5’s disks**

### New

- Devices › other PS5 › Disks: each disk opens in the file browser (read-only); on the web, click the disk name on the card.

**Web:** fait : nom du disque cliquable sur la carte, même explorateur


## 2.63.0 — 2026-11-09

**Other PS5 menu completed**

### New

- Devices › a paired PS5’s card: “Disks” (list with free space), “Saves” (its saves, read-only), “Send a game…”, “Send a save…”, “Close the open app…”, “Forget this console…”.

**Web:** fait : mêmes boutons sur la carte de l'autre PS5 dans Appareils


## 2.62.0 — 2026-11-09

**Close the open app on the other PS5**

### New

- After a send, Tasks offers “Close “…” there”; in Devices, the other PS5’s card offers “Close the open app…” at any time. Named confirmation; only the confirmed app is closed; a game is flagged (unsaved progress lost).

**Web:** fait : même bouton dans Tâches et sur la carte de l'autre PS5 dans Appareils


## 2.61.2 — 2026-11-09

**Instant install on the same disk**

### Improved

- Send a game to a PS5, as is: after its verified arrival, it is moved into the games folder with a simple rename (instant) instead of being copied again — a single copy, the network one.

**Web:** sans objet : moteur seulement


## 2.61.1 — 2026-11-09

**Folder games: installable from a copy**

### New

- Send a folder-format game to a PS5 (or install it from a copy): as is, the folder goes to the games folder; as .exfat, an image is created; as .ffpfsc, it is compressed — then registered in ShadowMountPlus.

**Web:** sans objet : moteur seulement, mêmes choix déjà présents sur le web


## 2.61.0 — 2026-11-09

**No duplicate after a send**

### New

- Send a game to a PS5: once it is installed in the other console’s games folder and its save restored, the transit copy is removed from “PBC Backup” — the game is there only once. Option to also keep a backup copy; if a step fails, the copy is kept.

**Web:** fait : même choix sur la fiche web


## 2.60.2 — 2026-11-09

**Reliable speed test**

### Fixed

- Read speed test: a single full read of the game (re-reading the same files measured memory, not the disk); the result shows in Tasks, with a warning if the game is too small for a reliable measure.

**Web:** fait : même résultat de vitesse dans Tâches sur le web


## 2.60.1 — 2026-11-09

**Usage bars on the other PS5’s card**

### Improved

- Devices: on a paired PS5’s card, each disk has its own small usage bar, with its free space.

**Web:** sans objet : le web affiche déjà une barre par disque sur la carte d'une PS5 jumelée


## 2.60.0 — 2026-11-09

**Accounts asked, one card per console**

### New

- Send a game to a PS5 or install it from a copy: you also choose the account that receives its save, as for a save on its own — both ways.

### Improved

- Devices: one card per paired PS5, its name as title and the list of its disks (Internal, USB 0, USB 1…) with their free space.

**Web:** fait : même choix du compte et même carte par console sur le web


## 2.59.2 — 2026-11-09

**Console-to-console sends: correct status**

### Fixed

- Send to a PS5: a step accepted by the other console (a restore or install starting) is no longer counted as an error; if refused, the other console’s message is shown.

**Web:** sans objet : correctif du moteur, même effet sur le web et la télé


## 2.59.1 — 2026-11-09

**The console as title**

### Fixed

- Devices: a paired PS5’s disk card shows the console name as title, with “Internal · system 3.21” below.

**Web:** fait : même carte sur le web


## 2.59.0 — 2026-11-09

**Send a save to the other PS5**

### New

- Saves › Options and game sheet › “Send to a PS5…”: send just the current save, choosing the console then the account that receives it; it is restored there, re-signed if the ID differs.

**Web:** fait : mêmes choix sur la fiche web et dans les actions de Parties


## 2.58.0 — 2026-11-09

**The other PS5 visible on both sides**

### Fixed

- Devices: when a paired PS5’s disks appear (it powers on, or the app was already open), the TV app and the web show them right away — before, an app opened too early never saw them.

**Web:** fait : le web relit aussi les endroits quand la révision change


## 2.57.1 — 2026-11-09

**Readable device names**

### Fixed

- Devices: a device name drops to a smaller size instead of being cut; a paired PS5 shows its disk as title with “console (system)” below.

**Web:** fait : même découpage disque / console (système) sur le web


## 2.57.0 — 2026-11-09

**What each tool is for**

### New

- Image tools: each menu choice says in one line what it does (verify, repair, uncompress, read-only, speed, AMPR index…), on the TV and on the web.

**Web:** fait : même explication sous chaque choix du menu web


## 2.56.0 — 2026-11-09

**Devices up to date at startup, theme right away**

### New

- Game sheet: games using APR-EMU (replacement libSceAmpr) are flagged, with their ampr_emu.index.

### Improved

- Devices: a paired PS5’s disks appear as soon as it is online (at startup or when switched back on), without waiting 30 s.
- TV: the chosen theme shows as soon as the app opens, even if the engine is slow to answer at console startup.

**Web:** fait : APR-EMU signalé sur la fiche web ; Appareils suit le moteur ; thème : sans objet (le web lit déjà le moteur)


## 2.55.0 — 2026-11-09

**Image tools in one button**

### New

- Game sheet: an “Image tools…” button opens a menu (verify, repair, uncompress, read-only, read speed test, copy or move, AMPR index, browse) — on the TV (as well as Triangle) and on the web.

**Web:** fait : même bouton « Outils de l'image… » en menu sur la fiche web


## 2.54.1 — 2026-11-09

**Menus at the right width**

### Fixed

- TV: a menu widens to fit its title; no title is cut anymore.

**Web:** sans objet : affichage de la télé (le web s'adapte déjà)


## 2.54.0 — 2026-11-09

**Each console’s system in plain sight**

### New

- Game sheet: its finding for this console and for each paired PS5, with their system (“Console A (3.21)”). If the game needs a newer system: “Backport needed: available” when a backport exists, otherwise “May not work”, with PS5 Center to help.

### Improved

- Send to a PS5: each disk of the other console shows its system; sheet texts shortened so nothing overflows.

**Web:** fait : mêmes constats par console sur la fiche web et même avertissement


## 2.53.0 — 2026-11-09

**The game’s backport, read and shown**

### New

- Game sheet and pre-send warning: the available backport is read and shown — the game’s fakelib libraries (with their SDK), ShadowMountPlus backports on the console, and the target console’s fakelib backport setting.

**Web:** fait : même ligne Backport sur la fiche et mêmes preuves dans l'avertissement


## 2.52.1 — 2026-11-09

**SDK also read from signed executables**

### Improved

- Compatibility: the SDK is also read from SELF-format executables when their segments are in clear (prepared games), without changing anything.

**Web:** sans objet : lecture côté moteur, même affichage sur le web et la télé


## 2.52.0 — 2026-11-09

**A game’s compatibility, with its evidence**

### New

- Game sheet: “Compatibility” line for this console — game version, SDK read from its executables (even inside an .exfat or .ffpfsc image, without mounting it), declared minimum, and a state: system recent enough, patch to verify or not enough information.
- Before sending or installing a game: if the console is below the declared system, the warning shows the findings and their evidence (executables’ SDK, game libraries, backport present) instead of concluding.

### Improved

- Automatic format: chosen from the mounts actually seen on the receiving console (ShadowMountPlus log), no longer from a system number.

**Web:** fait : même ligne Compatibilité sur la fiche et même avertissement sur le web


## 2.51.0 — 2026-11-09

**Warned if the system is too old**

### New

- Before sending a game to a PS5 or installing it from a copy: if the game needs a newer system than the console’s (for example 11.20 on a console on 3.21), a warning says so before going on.

### Improved

- Tasks: a finished install says when the game will appear on the home screen once the app is closed.

**Web:** fait : même avertissement et même mention dans Tâches sur le web


## 2.50.0 — 2026-11-09

**Choose the format of a sent game**

### New

- Send to a PS5 or install from a copy: choose the format — automatic (based on the console’s system), compressed (.ffpfsc), uncompressed image (.exfat) or as is. In automatic mode, an older console gets an uncompressed image it can mount.

**Web:** fait : même choix du format sur le web (fiche et Toutes les versions)


## 2.49.2 — 2026-11-09

**ShadowMountPlus notified directly**

### Improved

- Installing a game: ShadowMountPlus is notified through its API (as well as manual.lst); if it waits until no game or app is running, the task and the message say so.

**Web:** fait : même message sur le web (le jeu apparaît une fois l'application fermée)


## 2.49.1 — 2026-11-09

**The installed game shows up**

### Fixed

- Installing a received game or a copy: the copied image is now registered in ShadowMountPlus (the game appears on the home screen); if the same image is already on the disk, it is not copied again.

**Web:** sans objet : correctif du moteur, même effet sur le web et la télé


## 2.49.0 — 2026-11-09

**A game from one PS5 to another, installed**

### New

- Game sheet › “Send to a PS5…”: pick a disk on the other PS5; the game is copied and verified there, then installed (ShadowMountPlus) with its save re-signed. Everything is tracked in Tasks.
- All versions › a full copy › “Install on …”: puts the game back on the chosen disk of this console (internal or USB) and restores its re-signed save.

### Improved

- Game sheet: buttons wrap to a new line when space runs out.

**Web:** fait : mêmes boutons « Envoyer à une PS5… » sur la fiche et « Installer sur … » dans Toutes les versions


## 2.48.1 — 2026-11-09

**The other PS5's drives, like here**

### New

- Devices: each drive of a paired PS5 has its own card, like a drive of this console (name, space bar, free space out of total), on both sides; when it's off, a single “Offline” card.

**Web:** fait : mêmes cartes de disques de l'autre PS5 dans Appareils sur le web


## 2.48.0 — 2026-11-09

**The other PS5 as a destination**

### New

- A paired PS5's disks become destinations just like the NAS (“PS5-149 · USB 1”): from a game's page (“Where to back up?”), from Transfers (games, apps, saves, with verification) and for the PS5 full backup. Copies land in the other console's “PBC Backup”.

### Improved

- A paired PS5 is shown “online” as soon as it responds, even without an announcement received on the network.

**Web:** fait : mêmes destinations « autre PS5 » dans la fiche, Transferts et la sauvegarde complète sur le web


## 2.47.0 — 2026-11-09

**Two PS5 consoles working together**

### New

- One code is enough: when a PS5 pairs with another, both trust each other both ways.
- Devices: each paired PS5 has its card (real name, online or offline, its drives, forget), on the TV and on the web.
- Sends to another PS5 are tasks: “To do” bubble and Tasks tab, with progress.
- “How does it work?” in Devices › PlayStation 5: the three steps to link two consoles.

### Fixed

- Engine: two loops walked through a temporary list that was already destroyed (possible crash); fixed and covered by tests.

**Web:** fait : cartes des PS5 jumelées dans Appareils, mêmes étapes « Comment ça marche », envois visibles dans Tâches et dans les bulles


## 2.46.3 — 2026-11-09

**Another PS5: clearer menu**

### Fixed

- Another PS5: one line per paired console (“paired ✓, online”), and its actions in a submenu (See its drives, Forget this console… with confirmation). “Forget” no longer looks like an instruction.

**Web:** fait : mêmes libellés sur le web, et confirmation avant d'oublier


## 2.46.2 — 2026-11-09

**Link: lasting pairing, real console name**

### Improved

- Each console announces itself under its real name (the one in the PS5 Settings, as in Remote Play) instead of the first account's name.

### Fixed

- Link between consoles: pairing is kept under the other console's stable ID (it survives its restarts); before, it could be lost and the console appear twice.

**Web:** sans objet : moteur seulement ; les noms s'affichent dans « Une autre PS5 », télé et web


## 2.46.1 — 2026-11-09

**Updates without settings**

### Improved

- Updates: nothing to set. Without a chosen source, the app finds its updates by itself: an online source built into the package when there is one, otherwise a “PBC Update” folder on a USB drive, otherwise the folder dropped on the console.

**Web:** sans objet : moteur seulement ; Paramètres › Mises à jour affiche la source trouvée, télé et web


## 2.46.0 — 2026-11-08

**Send a save to another PS5**

### New

- All versions: “Send to <paired PS5>…” sends the copy to the other console (fingerprint checked), which restores it re-signed for its account, with a safety copy first. Works from a drive or the NAS.

**Web:** fait : même « Envoyer à … » dans le menu d'une version sur le web


## 2.45.1 — 2026-11-08

**Another PS5**

### New

- Devices › PlayStation 5: pair another PS5 running Backup Center — those seen on the network, or by IP address — with the 6-digit code it shows; this console's code is shown for the other one. A paired console shows its drives and free space, and can be forgotten.

### Fixed

- Engine: the link between consoles no longer re-reads the console's accounts every 5 seconds from its own thread (likely cause of an engine stop); the console name comes from the scan loop.

**Web:** fait : même fenêtre « Une autre PS5 » dans Appareils sur le web (le code de la console reste affiché sur la télé)


## 2.44.1 — 2026-11-08

**Link: more reliable announce**

### Fixed

- Link between consoles: the announce is also sent to the broadcast address of each of the console's networks, so another PS5 on the same network sees it.

**Web:** sans objet : moteur seulement


## 2.44.0 — 2026-11-08

**Link between consoles**

### New

- Engine: link between consoles — each Backup Center announces itself on the local network, pairs with another console using its 6-digit code and can read its status, drives, library and saves. Reusable module (also planned for PS5 Center).

**Web:** sans objet : moteur seulement ; l'écran (Appareils › autre PS5) arrive ensuite, télé et web ensemble


## 2.43.1 — 2026-11-08

**Reading the console's accounts**

### Improved

- Engine: read-only view of the account slots in the console registry (name, ID, type, activation), to prepare reactivating an ID. Nothing is changed.

**Web:** sans objet : moteur seulement (diagnostic) ; l'affichage arrive dans Paramètres › Utilisateurs, télé et web ensemble


## 2.43.0 — 2026-11-08

**Resume all on my account**

### New

- Settings › Users: “Resume all on …” for an ID that is not on the console. For each game, the console save is compared with the best copy from that ID; the most advanced one is checked (L2 to change), and each chosen game is restored and re-signed for the console account. The current save is copied first, nothing is deleted, and a running game is left out.

**Web:** fait : même « Tout reprendre sur … » dans Paramètres › Utilisateurs sur le web (tableau à cocher)


## 2.42.4 — 2026-11-08

**Settings › Users**

### Improved

- Settings: the “Accounts” section is now called “Users”, like on the PS5; you link an ID to the same user.

**Web:** fait : même nom de section et mêmes mots sur le web


## 2.42.3 — 2026-11-08

**Delete a copy, choose the account**

### Improved

- Restore: if there are several PS5 accounts on the console, the app asks which account to restore to.

### Fixed

- All versions: Square deletes the copy under the cursor (or the checked copies), with confirmation, without going through a menu; the button is shown at the bottom of the screen.
- IDs: a copy made under an ID that is not on this console is marked “Other ID”, with “not on this console”; Settings › Accounts says “On this console” or “Not on this console”. No more guessed names.

**Web:** fait : mêmes mots pour les identifiants sur le web ; le web avait déjà Supprimer sur chaque copie et le choix du compte à la restauration


## 2.42.2 — 2026-11-08

**Duplicates and Devices: fixes**

### Fixed

- Backups: R2 cleans safe duplicates again (checked games, otherwise all) with the preview before the trash; also in the Options menu, along with “Compare all”. In a game's detail, R2 cleans that game.
- Devices (TV): cards adapt to the number of devices; the “Add a device” row no longer goes under the hints at the bottom.

**Web:** fait : bouton « Nettoyer les doublons… » dans Backups sur le web ; Appareils : sans objet (mise en page télé)


## 2.42.1 — 2026-11-08

**Home and Library: fixes**

### Fixed

- Home: the category rows (games, one row per retro console, apps, saves without the game) are back under “Recently played”, where you open an item to back it up.
- Library (TV): each category is shown as a grid of 8 per row; Down goes further down the list (for example all apps), instead of a single row to scroll.

**Web:** fait : rangées par catégorie de retour sur l'Accueil web ; la Bibliothèque web était déjà en grille


## 2.42.0 — 2026-11-08

**Wider generic reader**

### New

- Wider generic progress reader: Unreal durations (Timespan), Blueprint struct fields (“CurrentLevel_49_…”), and a “detected automatically” note when progress comes from this reader.

### Improved

- Survey: on each comparison, useful field names (time, level, chapter, map…) from saves of unknown games are noted, to pick the next games for the catalogue.

**Web:** fait : mention « détecté automatiquement » dans Toutes les versions sur le web


## 2.41.0 — 2026-11-08

**Generic progress reader**

### New

- Generic progress reader: for every game not in the catalogue, play time, level, chapter and place are looked up under their common names in Unreal Engine and JSON saves. The catalogue keeps priority for the games it knows.

### Improved

- Copies already compared with the old reader go back to “Not yet compared”: “Compare all” reads them once more to get their progress. Safe duplicates are still offered meanwhile.

**Web:** sans objet : moteur seulement ; la télé et le web affichent déjà l'avancée fournie par le moteur


## 2.40.0 — 2026-11-08

**Progress: readable places, Little Nightmares, settings only**

### New

- Progress read from the save: Little Nightmares Enhanced Edition (place, including the expansion chapter); places are readable (“Spring Meadows” instead of “Level_SpringMeadows_Main_V2”), also for copies already compared.

### Improved

- A copy that only holds the game's settings, with no playthrough (e.g. Hades), is marked “settings only, no playthrough”.

### Fixed

- Engine: an old ID's name in All versions is now read safely (2.39.0 bug caught by the tests).

**Web:** fait : même mention « réglages seulement, pas de partie » dans Toutes les versions sur le web ; lieux lisibles fournis par le moteur


## 2.39.0 — 2026-11-08

**Settings › Accounts**

### New

- Settings › Accounts: every ID seen in your backups, with its name, copies, games and dates. You can rename it and link it to the same player (or unlink it); an ID with the same save as yours is flagged “Probably you”.

### Improved

- All versions: copies from an old ID show the name you gave it.

**Web:** fait : même section Comptes sur le web (renommer, c'est moi / ce n'est pas moi)


## 2.38.0 — 2026-11-08

**Resume on my account**

### New

- “Resume on my account”: a copy made under an old ID (before a format, on another console) is marked “Old ID”, with that ID's name when known; the action re-signs it for your current account (checked first, your current save copied beforehand, never while the game is running).

**Web:** fait : même étiquette, même texte et même action « Reprendre sur mon compte… » dans Toutes les versions sur le web


## 2.37.4 — 2026-11-08

**One color per task family**

### Improved

- Task bubbles: one color per family — backup and restore in purple, transfer and sync in teal, compression and images in pink.

**Web:** fait : mêmes couleurs de familles sur le web


## 2.37.3 — 2026-11-08

**Bubble colors**

### Fixed

- “To do” bubble colors made consistent: each kind has its color (to back up orange, duplicates green, not yet compared blue, to check red, tasks purple), applied to the edge, background, title and bar, on the TV and the web, in dark and light themes.

**Web:** fait : même règle de couleurs sur le web (thèmes sombre et clair vérifiés)


## 2.37.2 — 2026-11-08

**Time left in the bubbles**

### Improved

- Home: task bubbles (full game backup, compression, transfer…) show the percentage and time left; covers no longer flicker on the web.

**Web:** fait : pourcentage et temps restant aussi sur le web


## 2.37.1 — 2026-11-08

**Tasks in “To do”**

### Improved

- Home: one bubble per kind of running task (compression, transfer, decompression…), with the progress bar of the task shown.

**Web:** fait : mêmes bulles de tâches avec barre d'avancement sur le web


## 2.37.0 — 2026-11-08

**New layout**

### New

- New layout, one question per tab: Home (what's left to do), Library, Saves (where my games stand, player by player), Backups (my copies, disk by disk), Transfers, Devices and Tasks.
- One rule for selecting: L2 checks an item (the web has checkboxes), Options opens the actions for the selection. No more hidden shortcuts.
- “All versions” of a game, on all disks and the NAS, with Restore and the trash.
- Progress read from the save for ELDEN RING (character, level, play time), Clair Obscur: Expedition 33 (play time, location) and Cyberpunk 2077 (play time, level, lifepath). More games to come.
- Registry of account IDs, copied to each backup disk: after a format, an old ID's name is found again.

### Improved

- “To check” counts incomplete copies and empty copies, the same way on the TV and on the web.
- Home: “To do” becomes a notification center — one bubble per kind of task (to back up, duplicates, not yet compared, to check, tasks), with the count; the cover and name of the games involved rotate inside the bubble.

### Fixed

- The version number is no longer cut off at the top of the TV screen.

**Web:** fait : même découpage, cases à cocher, barre d'actions sur la sélection, Toutes les versions, Synchroniser et Appareils sur le web


## 2.36.1 — 2026-11-08

**Players and identifiers**

### Improved

- Engine: the account identifiers seen in your saves are listed (dates, games), and the ones that are clearly you (the same playthrough under two identifiers) are flagged; they can be given a name. The screens come with the new layout.

**Web:** sans objet : moteur seulement ; l'écran Paramètres › Comptes arrive avec le nouveau découpage, télé et web ensemble


## 2.36.0 — 2026-11-08

**Select all, delete, ignore**

### New

- Manage backups (TV and web): everything can be selected and sent to the trash — a whole item or copy by copy, even without a comparison — including full backups of games, apps, homebrew and ROMs. “By disk” can also move a copy to the trash.
- “Stop backing up…” on the page of any item (game, app, homebrew, ROM) and multiple selection in the Library (“Ignored” view); ignored items no longer appear on Home.

**Web:** fait : mêmes capacités sur le web (sélection, corbeille, ignorer depuis la fiche, mode Sélection dans la Bibliothèque, corbeille dans Par disque)


## 2.35.1 — 2026-11-08

**Old account identifiers**

### Improved

- Manage backups: an old identifier of your account (for example from before a console format) can be declared as yours; its copies are then treated as yours (duplicates offered for cleanup).

**Web:** sans objet : réglage du moteur, enregistré pour l'utilisateur ; l'écran pour le modifier arrive avec le nouveau découpage (Paramètres › Comptes), télé et web ensemble


## 2.35.0 — 2026-11-08

**Ignore a game, faster comparison**

### New

- “Ignore this game” (Backups, TV and web): the game is no longer backed up or shown, and its copies can go to the trash on all disks and the NAS in one go; the “Ignored games” list in Settings lets you undo it. “Check all / none” in a game's details.

### Improved

- Faster comparison: a copy that is byte-for-byte identical to one already read is no longer opened. An old save without a game ID (e.g. “Split Fiction” on the NAS) is linked to the right game instead of creating a second entry.
- Comparison: backups made by the app are no longer copied or downloaded when their content has already been read (each file's fingerprint is in their manifest); temporary folders of an interrupted comparison are emptied at startup.

**Web:** fait : « Ignorer ce jeu… », « Tout cocher / rien » et « Jeux ignorés » dans les Paramètres, comme sur la télé


## 2.34.0 — 2026-11-08

**New backup management**

### New

- New backup management (TV and web): a clear summary per game (copies, contents, duplicates, progress), a cleanup proposal — one copy per different content and the most recent one on each disk —, a preview before cleaning up, “Compare all”, and the 30-day trash with “Put back”.

### Improved

- Copies from another account and protected copies are never offered for cleanup.

**Web:** fait : mêmes écrans sur le web (vue par jeu, détail, aperçu, corbeille), boutons à la place des touches


## 2.33.0 — 2026-11-08

**Progress of every copy**

### New

- Progress of copies: each new backup records the game's progress at the time of the copy; “Compare all” reads existing copies once (disks and NAS), never opening the save of the game being played.

**Web:** sans objet : moteur seulement ; les écrans télé et web qui l'utilisent arrivent ensemble dans la version suivante


## 2.32.1 — 2026-11-08

**More accurate progress**

### Improved

- Game progress: the actual playthrough comes before profile or settings saves, and the game's name is no longer repeated before the text.

**Web:** sans objet : réglage du moteur (choix de la sauvegarde affichée), le web et la télé l'affichent pareil sans changement


## 2.32.0 — 2026-11-08

**Game progress and trash**

### New

- Saves: next to each game, the progress the game writes in its save (mission, percentage, level, time) and the play time counted by the app from now on. Comparing copies uses it to tell which one goes furthest.
- Manage backups: deleting moves copies to a trash (“PBC Backup/Corbeille”, same disk or NAS) for 30 days, from where they can be put back.

**Web:** fait : avancée par jeu dans Sauvegardes et messages de corbeille dans Gérer les backups, comme sur la télé


## 2.31.1 — 2026-11-08

**Web language protected**

### Fixed

- The web page language can no longer be changed by mistake by a test of the app run on a computer; the TV's release notes stay to be seen on the TV.

**Web:** sans objet : correctif interne de l'app télé (les aperçus sur ordinateur n'écrivent plus la langue système ni « nouveautés vues ») ; le web en profite sans changement


## 2.31.0 — 2026-11-08

**TV and web aligned**

### Improved

- Saves: the 6 views are on the TV as on the web (“Backup newer” was missing on the TV), same names and order. Web: the language follows the one chosen on the TV, spinner while the content is read, no more “Restore” on an incomplete version or for a non-PS5 account, USB disks silently re-read after a minute.

**Web:** fait : rattrapage complet après audit des 31 changements télé depuis la 2.20.1 (Par disque, Gérer les backups, langue en direct, restauration, 6 vues)


## 2.30.9 — 2026-11-08

**By disk on the web**

### Improved

- Web › Saves › By disk: like on the TV, covers for old backups, list kept for each disk, Refresh button and number of versions.


## 2.30.7 — 2026-11-08

**Square everywhere**

### Improved

- Saves: Square changes the view even when the cursor is on the left column (accounts, By disk, Sync), without moving right first. In the Library, Square also works from the top row.


## 2.30.6 — 2026-11-08

**Clearer hints**

### Improved

- Clearer on-screen hints: Square says what it shows next (“Show: On the console”, “Go to: USB 1”) instead of “or Up: filters”. In the Library, Square changes category (All, PS5, PS4, Applications, ROM).


## 2.30.5 — 2026-11-08

**Handier By disk**

### Improved

- Saves › By disk: each disk keeps its list when switching between them (Triangle to refresh), spinner while the NAS is read, scrolling through every version and covers for old backups.

### Fixed

- Transfers: the “Full PS5 backup” and “Transfers in progress” cards are no longer cut off.


## 2.30.4 — 2026-11-08

**More memory**

### Improved

- The TV app now has a much larger memory reserve (up to 96 MB instead of about 13 MB): no more stops when lists grow. A memory reading is written to the log every 30 seconds.
- Saves › By disk: the hint says “or Up: filters”, like everywhere else.


## 2.30.2 — 2026-11-08

**Magnifier and Square**

### Improved

- The magnifier can be selected with L1 / R1, between Tasks and Settings: Cross opens search.
- Saves › Sync: Square (filters) stays shown even when the Switch doesn't answer.


## 2.30.1 — 2026-11-08

**Airier Transfers**

### Improved

- Transfers: airier page, FROM, TO and WHAT TO DO? spaced out a little more.


## 2.30.0 — 2026-11-08

**Vietnamese**

### New

- New language: Vietnamese (Tiếng Việt), on the TV and the web, chosen automatically when the console is set to Vietnamese.

### Improved

- “Hold Circle 2 s: quit” is now shown at the bottom right of every view (before: Library only).
- The magnifier at the top right no longer has the small triangle under it.


## 2.29.5 — 2026-11-08

**17 languages**

### Fixed

- The TV app launches again: it now reads only the displayed language, and the task list it receives no longer includes the large content-comparison reports (the app's memory is limited to 13 MB).
- The console language is detected again when the TV app starts (on a French console, the app was showing English).


## 2.29.1 — 2026-11-08

**17 languages**

### Improved

- “Manage backups”, the release notes and the language list (translated name alongside: “Русский · Russian”) follow the chosen language.

### Fixed

- The “engine not responding” message no longer appears while measuring sizes on the NAS or comparing contents.


## 2.29.0 — 2026-11-08

**17 languages**

### New

- The app speaks 17 languages, on the TV and the web: French, French (Canada), English, Spanish (Spain and Latin America), Portuguese (Brazil and Portugal), German, Italian, Dutch, Polish, Turkish, Russian, Simplified and Traditional Chinese, Japanese, Korean and Arabic. Defaults to the console language; change it in Settings › General.

### Improved

- Only one engine at a time: a new engine waits until the old one has really stopped (otherwise it doesn't start), and stopping takes at most 20 seconds, even if an operation is dragging on.
- Save images mounted by the app are unmounted on shutdown, and the app uses its own mount folders (no more conflicts with Garlic SaveMgr if it's running too).
- /status shows the engine's PID, instance, role and current task.


## 2.28.0 — 2026-11-08

**Themes, sounds and world scripts**

### New

- 8 themes to choose from (Settings › General), on the TV and the web.
- The TV can display Chinese, Japanese, Korean, Russian, Greek and Arabic (joined letters, right to left) — ready for the translations.
- Sound is back: tile music, interface sounds (move, confirm, back, confirmation, error, task done) and background music (off by default). Settings: sounds, music and volume in Settings › General, on the TV and the web.

### Fixed

- The engine no longer gets stuck on “server busy”: only one NAS read at a time (other requests get the latest list), and a silent connection no longer holds its slot.


## 2.27.1 — 2026-11-08

**Manage backups**

### New

- Saves › Manage backups (TV and web): for each game, every version on the disks and the NAS — date and time, location, size, “old”, protected, and what their content says (same letter = identical content, progress). Actions: refresh, compare content, tick duplicates (one copy of each content is kept), keep the last 5, protect, delete (with confirmation, never a protected version), restore.
- The NAS appears in Saves › By disk, next to the USB drives.


## 2.26.0 — 2026-11-08

**What's inside a save**

### New

- Compare versions of a PS5 save by their content, not just their date: progress written by the game, play time when the game reports it, save time, and a fingerprint to spot identical copies. Console, disks and NAS, read-only on copies.

### Improved

- Old saves stored by game on a USB drive are read and can be restored, just like on the NAS.


## 2.25.0 — 2026-11-08

**Clearer saves**

### Improved

- Games that only exist in a backup show their real name and official icon (Indiana Jones, Split Fiction, games shown by their number).
- Versions: date and time; “old” for those made by script; “size?” instead of “0 B” when the size isn't known yet.

### Fixed

- Saves: Cross on a game opens a menu (Back up now / Restore a version) instead of starting a backup on every press.


## 2.24.1 — 2026-11-08

**Bring a save back from the NAS**

### Fixed

- “Newer in a backup” compares the time of the game save inside the backup (not the time of the copy): a recent copy of the same save is no longer seen as newer.


## 2.24.0 — 2026-11-08

**Bring a save back from the NAS**

### New

- Restore a PS5 save straight from the NAS (including old ones made by script: images only): the app downloads the chosen version and puts it back on the account, with the same safety (checked first, safety copy, game closed).
- TV: Saves › Cross on a game → Back up now, or Restore a version (date and location, NAS included), checked before it's put back.

### Improved

- Console ↔ NAS comparison down to the minute (NAS file times); new “Newer in a backup” status and filter.
- Same buttons for every device (Switch included): Save, Test, Tidy under PBC Backup, Forget.


## 2.23.1 — 2026-11-08

**Everything under PBC Backup**

### Improved

- NAS saves are recognized even when the folder name has extras (“(2025) (PPSA17599) (US) v1.400”) or no “[PS5]” (“Split Fiction”); “_…” folders are ignored.


## 2.23.0 — 2026-11-08

**Everything under PBC Backup**

### New

- A single “PBC Backup” folder on the NAS, sorted by platform underneath (PBC Backup/PS5 backup/[PS5] Name (ID)/saves/<date>, PBC Backup/Switch backup/…).
- “Tidy under PBC Backup” (NAS settings, TV and web): moves your “PS5 backup”, “Switch backup”… folders into “PBC Backup” by simply renaming them — nothing is copied or deleted; if a folder exists on both sides, it's merged date by date without overwriting anything.

### Improved

- The NAS shows all its saves, inside and outside “PBC Backup”, during and after tidying.


## 2.22.4 — 2026-11-08

**See everything on the NAS**

### Improved

- A NAS shows every save under its folder, whatever the layout: “by platform” ones (PS5 backup/[PS5] Name (ID)/saves/<date>, including those made outside the app) and those in the “PBC Backup” folder. The layout only decides where new ones go.


## 2.22.3 — 2026-11-08

**Choose the NAS folder**

### New

- “Test connection” also works before saving a device (NAS, FTP, Switch), using what you've entered: connection, then whether the chosen folder is writable. Nothing is kept.


## 2.22.2 — 2026-11-08

**Choose the NAS folder**

### Improved

- Web: the page reloads by itself after an app update (waiting until you finish typing).


## 2.22.1 — 2026-11-08

**Choose the NAS folder**

### New

- Add a NAS: connect first, browse its folders and pick the one for backups, then save — no typing, no paths to know (TV: Cross on “Folder”; web: Browse…).

### Improved

- If the account can't write to the chosen folder, the message says so clearly and offers to pick another folder.
- A saved NAS is shown with a NAS enclosure (Destinations and Transfers, TV and web).


## 2.21.1 — 2026-11-08

**Search and NAS**

### Improved

- QNAP tile with a real QNAP enclosure (TS-264), Synology with its own (DS224+), TV and web.
- Transfers (TV): all devices visible at once, across several rows; Switch consoles show up without scrolling. Up / Down move between rows.


## 2.21.0 — 2026-11-08

**Search and NAS**

### New

- Web: the magnifier in the header opens search (so does the “/” key).
- Transfers: an “Add a NAS” card in FROM and TO; the added NAS shows up there right away.

### Improved

- Search ignores case and accents, also works by ID (PPSA…), on the TV and the web.
- TV: the magnifier shows the button that opens it (Triangle) on Home and Library.
- Synology and QNAP tiles: same NAS image as the other devices, with the name below.
- Web: Transfers devices wrap onto new lines, everything is visible.


## 2.20.3 — 2026-11-08

**The web catches up with the TV**

### Improved

- Device settings (TV): Cross on “Folder” browses the device, Square picks the folder shown; it stays saved in the connection.
- Synology and QNAP tiles: logo centered like the other images, “NAS” below (TV and web).


## 2.20.2 — 2026-11-08

**The web catches up with the TV**

### Fixed

- Web: the Synology and QNAP logos no longer sit on top of the app logo, and device images are the right size.


## 2.20.1 — 2026-11-08

**The web catches up with the TV**

### New

- Web interface on par with the TV app: Home, Transfers (with NAS and full backup), PS4 filter, editable Destinations (Switch, FTP / FTPS, folder layout, test, browse), full plans with rotation, plans and image tools from the game page, emulator settings, file browser.

### Improved

- The version shown on the web is the app's version.
- The web uses the same device images as the TV (PS5, USB disk, Switch, consoles, Synology, QNAP, FTP / SMB, cloud).

### Fixed

- Pairing page: the cursor no longer jumps back into the code field while you type the device name.


## 2.19.0 — 2026-11-08

**From the NAS**

### New

- Transfers from a NAS: choose the NAS as source, tick its saves, and put them back on the PS5 (each file in its original place, registration included, without overwriting anything) or into a folder on a disk. Verified on arrival.

### Improved

- To a NAS: verified copy, organized like your other backups.


## 2.18.0 — 2026-11-08

**Image tools**

### New

- Game page, Triangle: tools for its image — verify, repair, compress, decompress (folder or .exfat), extract, read-only, speed test, AMPR index, or send it to another disk. Only tools that make sense for that game are offered (installed, image or folder).

### Improved

- A game's page shows the disk that actually holds its data and its form (.ffpfsc image, folder, installed).

### Fixed

- Copying or moving a game to a disk from the Transfers tab is no longer wrongly refused.


## 2.17.1 — 2026-11-08

**Game images**

### Fixed

- PS5 system apps are no longer listed among installed games.


## 2.17.0 — 2026-11-08

**Game images**

### New

- Compress a PS5 game into a .ffpfsc image, decompress it, create or extract a .exfat image, verify and repair an image: all done by the app's engine (PS5 Game Compressor code built in), with no other tool or port.
- Copy or move a game between internal storage and a USB disk: the copy is re-read (SHA-256), then re-registered with ShadowMount; the original is only deleted after verification.
- Each game shows how it's installed (native, .ffpfsc / .exfat / .ffpkg image, or folder) and the disk that actually holds its data.

### Improved

- A game being played is never touched: compressing, moving and deleting are refused while it's running.
- Transfer tracking shows the current step (compression, verification…).


## 2.16.1 — 2026-11-08

**Full backup at the right size**

### Fixed

- Full backup: each account's game saves are counted under “Saves” (no longer under “Accounts”), and the app's working folders are no longer counted as homebrews.


## 2.16.0 — 2026-11-08

**Transfers**

### New

- New Transfers tab: choose from where and to where (PS5, USB disks, Switch, NAS), then Copy, Move, Compress or Sync, with game selection, options and transfer tracking.
- Full PS5 backup: accounts, saves, game registrations, homebrews and their data, boot settings, captures — each category with its size, all in one dated backup on a disk or the NAS.

### Improved

- Restoring PS5 saves is built into the engine: no need to run another tool alongside.

### Fixed

- All six tabs fit in the header.


## 2.15.1 — 2026-11-08

**The real games**

### Fixed

- PS5 games mounted from an image (shadowmount, Game Compressor) finally count their real size: backing up the game copies the image (.ffpfsc…) and its registration, not just the shortcut of a few dozen MB.


## 2.15.0 — 2026-11-08

**Plans, editable with the controller**

### New

- Game page: “Scheduled plans…” to add it to one or more plans (or create one with this game), and “Browse location” to see its folders.
- Full plan editor on the TV: name, when (on demand, daily, weekly), days ticked one by one, time (in quarter hours), content (chosen games or rule), what to back up, destination, versions kept, active or paused.
- A loading spinner appears when the app is waiting for a response (folders, connecting to a device).

### Improved

- Tasks › Plans: Cross opens the plan to edit it; R2 runs it right away.


## 2.14.2 — 2026-11-08

**Small touch-ups**

### Fixed

- Browsing a Switch or a NAS no longer shows “the engine isn't responding” when the device takes a few seconds to answer (waking from sleep): the app waits up to a minute, without freezing.


## 2.14.1 — 2026-11-08

**Browse your devices**

### New

- Destinations: Triangle on a device opens its folders (internal storage, USB disks, Switch, NAS), with file sizes.


## 2.14.0 — 2026-11-08

**Your backups, organized your way**

### New

- A NAS or FTP server can store saves by platform, like your existing backups: “PS5 backup/[PS5] Name (PPSA…)/saves/<date>/”. “Folder layout” setting on the device page.
- Backups already on the NAS (made by your scripts) appear in Saves, one version per date.

### Improved

- Folders whose names contain brackets (“[PS5] …”) now list correctly on Synology NAS units.


## 2.13.0 — 2026-11-07

**Encrypted FTP**

### New

- NAS units and FTP servers that support it are reached over encrypted FTP (FTPS): passwords and files no longer travel in clear text on the network. Devices without encryption (ftpd on the Switch) stay on plain FTP.

### Improved

- NAS certificates (often self-signed) are accepted on the local network; their fingerprint is recorded.


## 2.12.1 — 2026-11-07

**Small touch-ups**

### Improved

- An emulator's settings window takes just the space it needs.


## 2.12.0 — 2026-11-07

**Configurable emulators**

### New

- Settings › Sources and emulators: Cross on an emulator opens its settings. Add, change or remove its game folders, choose its settings file and its saves folder.
- A console file browser to pick a folder or file without typing a path (Cross opens, Square picks the folder, Circle goes up).
- “Back to automatic” clears your choices: the emulator is detected on its own again.

### Improved

- Manually configured emulators are flagged, and the collection is rescanned right away.


## 2.11.0 — 2026-11-07

**Destinations, configurable from the PS5**

### New

- Destinations can be set up with the controller: Cross on a disk makes it the default destination; Cross on a Switch opens its page (name, address, port, user, password, folder) with Save, Test and Forget.
- Add a device: Switch, Synology, QNAP or FTP / SMB open a blank form to fill in with the on-screen keyboard; the connection is tested before saving.
- The on-screen keyboard has capitals and symbols (L1); passwords are shown as dots.


## 2.10.3 — 2026-11-07

**Filters with room to breathe**

### New

- PS4 filter in the Library, next to PS5.

### Improved

- More space between the counter, the game name and the filters.
- The types (Everything, PS5, PS4, Apps, ROM) form a group; Emulator, Location and Refresh are separate bubbles.


## 2.10.2 — 2026-11-07

**Small touch-ups**

### Improved

- More space between the header and the top of the Library.


## 2.10.1 — 2026-11-07

**The magnifier**

### New

- A magnifier at the top right: Triangle starts a search, from Home or the Library.

### Improved

- The top of the Library is less cramped.
- Add a device starts with PlayStation 5, then USB disk.


## 2.10.0 — 2026-11-07

**A roomier library**

### New

- The Library is browsed in rows, like Home: PS5 games, PS4 games, one row per console, Apps, Saves without the game. Filters and search stay.

### Improved

- Home: rows start right under the carousel, so you see the covers straight away.


## 2.9.2 — 2026-11-07

**Colored labels**

### Improved

- Cover labels take their family's color: PlayStation blue, Nintendo red, Sega turquoise, purple for apps, amber for the rest, with a subtle relief.


## 2.9.1 — 2026-11-07

**Small touch-ups**

### Improved

- Destinations › Add a device, in a more logical order: USB disk, PlayStation 5, Switch, Consoles, Synology, QNAP, FTP / SMB, Cloud, with a line of explanation under each.
- The “Quit” window is half the size.


## 2.9.0 — 2026-11-07

**A home screen to browse, and search**

### New

- Home scrolls down: “To back up”, “PS5 and PS4 games”, one row per retro console, “Apps”, “Saves without the game” rows.
- Search the Library with Triangle: an on-screen keyboard, results appear as you type.
- Settings are behind a gear at the top right.

### Improved

- The Home background follows the game selected in the rows.


## 2.8.2 — 2026-11-07

**Consistent covers**

### Improved

- All covers share the same full square format, like on PS5 and Switch; retro game boxes are framed on their title.
- A single small, discreet badge (N64, MD, PS5, App…) at the bottom of the cover, instead of the two green labels that hid the title.


## 2.8.1 — 2026-11-07

**Real images, even without the game**

### New

- Games with only their save left get the game's official icon (ELDEN RING, Cyberpunk 2077, Split Fiction…), just like in BlackBox.

### Improved

- An image that arrives later replaces the save's small image without restarting the app.


## 2.8.0 — 2026-11-07

**HTTPS in the engine**

### New

- Switch games that are only in Eden get their official icon in Sync.
- The engine can now connect over HTTPS, with certificate verification.

### Improved

- Destinations: “Other consoles” tile.


## 2.7.2 — 2026-11-07

**Sharper covers**

### Improved

- Large covers (Home, Library banner) are loaded in high definition.
- Modified ROMs get the original game's cover back (e.g. Majora's Mask Redux).
- The Square button is shown everywhere at the bottom of the screen: it switches filters, like Up then Left / Right.
- Holding Circle to quit shows the seconds remaining.

### Fixed

- The cross on Home's “Back up the game…” button is properly centered.


## 2.7.1 — 2026-11-07

**Small touch-ups**

### Improved

- More compact Destinations: Switch consoles get their own card, the Synology and QNAP logos are shown large.
- The Library banner shows the game's name without region tags.


## 2.7.0 — 2026-11-07

**A big library, and the real game names**

### New

- The Library gets a banner: the selected game's cover shown large behind the header, with its name.
- Destinations shows each device with its real image: PS5 for internal storage, PS5 and disk for USB, Switch for your Switch consoles. Plus an “Add a device” row (Synology, QNAP, PC, Switch, USB, cloud).
- Holding Circle to quit shows a card with a filling ring.

### Improved

- Games with only their save left show their real name (Ratchet & Clank: Rift Apart, ELDEN RING, Cyberpunk 2077…) instead of their code.
- Sharper covers, shown in full: you can see the title and the “2” of a sequel (Earthworm Jim 2).
- A game with no cover for its console borrows one from another console (Flashback on Dreamcast).
- Games without a cover get a blue-violet card.
- Active tab underlined with a glowing line, deeper background.

### Fixed

- Sources and emulators: “Eden” indicates it's only a folder, with no app installed.


## 2.6.1 — 2026-11-07

**Small touch-ups**

### Fixed

- A Switch game icon that can't be found online no longer holds up the display.


## 2.6.0 — 2026-11-07

**A home screen, and a nicer sync**

### New

- New Home tab: a carousel highlights games to back up, with a large cover, and your collection right below.
- Switch games that are only in Eden now have their name and icon in Sync.

### Improved

- Filters are picked with the arrows everywhere: Up to reach the filters, Left / Right to change (Saves, By disk, Sync, Tasks).
- Sync shows “Reading…” with a spinner during the comparison.
- A comparison already done is kept: switching filter, Switch or tab no longer loses it. Triangle to re-read.
- The logo is cut out, with no frame around it, and the header is lighter.


## 2.5.2 — 2026-11-07

**Small touch-ups**

### Fixed

- In Sources and emulators, text no longer runs under the emulator icon.


## 2.5.1 — 2026-11-07

**Images everywhere**

### New

- In Settings › Sources and emulators, each emulator shows its small icon.

### Improved

- Covers also appear in Saves › By disk and in Sync.


## 2.5.0 — 2026-11-07

**A new look**

### New

- The PS5 Backup Center logo is back, on the TV and on the web.
- Controller buttons are shown in their real colors: blue cross, red circle, pink square, green triangle.

### Improved

- The selected cover glows with a halo in the library.


## 2.4.0 — 2026-11-07

**Scheduled plans, on the TV too**

### New

- Tasks → Scheduled plans on the TV: create a plan with one press (PS5 saves every evening, ROMs every Sunday, apps every Monday, all games on demand).
- Cross runs a plan right away, Triangle pauses or resumes it. To fine-tune the details, use the web.

### Improved

- Each plan shows when it runs, what it backs up, how many versions it keeps and the result of the last run.


## 2.3.2 — 2026-11-07

**An image for every task**

### Improved

- In Tasks, each row has its image: the game or app cover, or a card in the console's color if no image exists.

### Fixed

- Restores appeared without an image in the task list.


## 2.3.1 — 2026-11-07

**Guided all the way**

### New

- After sending to the Switch, a step-by-step guide explains how to finish the restore in JKSV.

### Improved

- Library filters: reach them with Up, and Emulator / Location change directly with one press, no window.


## 2.3.0 — 2026-11-07

**Backups that run on their own**

### New

- Scheduled plans (Tasks → Plans): choose what to back up, when (daily, certain days, once) and where. New games are picked up automatically.
- Choose how many versions to keep: the oldest are removed, protected versions stay.
- “Run now”, pause or resume a plan at any time.
- Choose a backup's destination from all detected disks and network devices.

### Improved

- Library filters are reached with Up, and the lists (emulator, location) open with one press.
- Games and tasks show their image in Saves and Tasks.
- A ROM without a cover (modified version, homebrew) now gets a card in its console's color with its name.

### Fixed

- Some apps (BlackBox, Orbit, Game Compressor) were listed among PS5 games: fixed.
- An app's page talked about a “game”.


## 2.2.0 — 2026-11-07

**Your saves synced between consoles**

### New

- New Sync screen (Saves): your Switch and ProsperoEden side by side, game by game and profile by profile.
- Send a save from the Switch to Eden, or from Eden to the Switch: it lands in JKSV, ready to be restored.
- Backup Center recognizes identical saves by comparing their content, without downloading everything.

### Improved

- Before each transfer, everything is checked and you're shown what will change; nothing is ever deleted on the Switch.

### Fixed

- Comparing saves no longer blocks the rest of the app while it works.


## 2.1.1 — 2026-11-07

**What's new, always at hand**

### New

- Settings → What's new: the history of each version, what's new, improved and fixed.

### Improved

- After an update, “What's new” shows you what changed, just once.

### Fixed

- Long sentences were cut off with “…” on screen: they now wrap.
- The acknowledgments list overflowed the bottom of the screen.


## 2.1.0 — 2026-11-07

**Your Switch saves come to the PS5**

### New

- Connect your Switch over the network (ftpd): Backup Center reads saves made with JKSV and matches them to your Eden games on its own.
- Import a Switch save into Eden or ProsperoEden, on the profile of your choice. A copy of the current save is made first.
- New network destination: send your backups to a NAS, a PC or another PS5 over FTP.
- Restore a PS5 save from the web: it's checked first, and you're asked before it's moved to another account.
- Missing covers for your ROMs are downloaded automatically.

### Improved

- YouTube, VLC and other apps are no longer listed among games.
- A Switch save older than Eden's no longer overwrites anything without your consent.
- Putting a backup back in its original location now asks for explicit confirmation.

### Fixed

- The Switch import could refuse a perfectly valid save: fixed.
- Covers for a few games (special editions, names with “&”) were wrong or missing.


## 2.0.9 — 2026-11-07

**Your saves, account by account**

### New

- New Saves tab: your accounts on the left, and each game's status — up to date, console newer, never backed up.
- Back up a game's progress with one press, or your whole account at once.
- Uninstalled games keep their save in the list, with their image.


## 2.0.8 — 2026-11-07

**Thanks to those who helped us**

### New

- Acknowledgments screen, at the very bottom of Settings: every project used, its author, its license and its sources.


## 2.0.5 — 2026-11-07

**Updates, right in the app**

### New

- Settings → Updates: check, install, then restart the engine or the app with a countdown.
- Updates possible without a network, from a USB drive.

### Improved

- Only changed files are downloaded: an update installs in a few seconds.
- If something goes wrong during installation, the previous version is restored automatically.

### Fixed

- After an update, the app could refuse to start: fixed.


## 2.0.0 — 2026-11-07

**A new app, a new engine**

### New

- TV app completely rebuilt: faster, with filters that respond instantly.
- The backup engine keeps running when you close the app, and resumes an interrupted backup.
- The same app on your phone or computer, with a 6-digit code.

### Fixed

- The app crashed at startup on some consoles: fixed.


