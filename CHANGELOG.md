<!-- Generated from build-system/changelog.json — do not edit by hand. -->

# Changelog

All user-facing changes to **PS5 Backup Center**, newest first. This is the same history the app shows in **Settings › What's new**.

> Entries are written in French, the app's first language, and shown here in English (the app's own translations). A few lines without a translation yet — mostly the « Web: » parity notes — stay in French.
> *Les entrées sont rédigées en français, la langue d'origine de l'app ; quelques lignes pas encore traduites restent en français.*

## Highlights (English)

| Version | What it brought |
|---|---|
| **3.0** | **PS5 Backup Center 3** — **cloud storage** (Amazon S3, Backblaze B2, Wasabi, Cloudflare R2, Scaleway, OVHcloud, Synology C2, pCloud, Koofr, Nextcloud; Google Drive / OneDrive / Dropbox sign-in from the phone), **NAS over WebDAV**, « Used for » per NAS and several syncs, a full on-screen keyboard, clearer Home / Library / Backups, Follow us. |
| **2.87–2.88** | **SMB** for the NAS (besides FTP). **Update another PS5 from this one**, restart / open / close Backup Center on it, Home « Update » bubble. No more duplicate engines and much lower memory peaks (fixes for CE-15773-3). |
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

## [3.2.0] — 2026-11-19

**Launch a game from its .pkg (first version)**

### New

- “Install… › Mount it where it is” for a Kraken .pkg: the game is declared to the console and runs from its package, nothing unpacked — by Kraken Mount if it's installed, otherwise by our built-in version (Kraken Mount's work inside our engine, with ampr-emu-ng, pgo_stub and dlc_emu built by us; needs kstuff). A game already taken by ShadowMount or installed by the console is left alone. First version, to be tried.
- Settings › Game mounting: ShadowMountPlus and Kraken Mount detected; by default the installed tools are used, and our built-in version takes over for .pkg files when Kraken Mount isn't there (or by choice).
- Our built-in ShadowMount (ShadowMountPlus 1.7, built by us): when the installed ShadowMountPlus isn't running, game images and folders are still mounted and registered — no hook in the system, no port; the installed one takes priority as soon as it runs (Settings › Game mounting). Its settings are never modified.
- “Install…” for a game that is elsewhere (NAS, cloud, other PS5), from Files: choose the target PS5 and its drive; verified copy, then mounting or installation through the console. The NAS and the cloud are sources, never targets.
- Web: “Install from this PC…” (Files) — a game file from the PC (.pkg or image) sent to the chosen PS5 and drive, in chunks, resuming after a dropout, then mounted or installed through the console.
- Settings › Console tools: Payload Manager's payloads and those running — start or stop them from here (kstuff and our engine protected, ShadowMountPlus stopped cleanly), and open Payload Manager; no more restarting the console to relaunch a tool.
- Game sheet: backup on the left (the checkboxes, then “Back up” right below), what you do with the game on the right, with “Install…” added; ← → switches column.
- Game libraries: a folder on the NAS, a cloud, a USB drive or the other PS5, declared from Files; its uninstalled games appear in Home and Library (“To install”), ready to install on the chosen PS5 and drive. Read in the background, nothing there is changed. Each .pkg is recognized from its header: game, update or DLC (never taken for the game: filed under it, installable through the console once the game is installed), PS4 or PS5, fake PKG or official package; Kraken is offered only for a PS5 game in fake PKG, otherwise the reason is given.
- “Install…” from elsewhere: choose the format (the image, the game's .pkg launched by Kraken or installed by the console, or the image converted to .pkg then installed), with the estimated time; then a single task with numbered steps (copy, verification, conversion, installation), each with its bar, speed and time left. It can be stopped at any step.
- Uninstalling a game launched from its .pkg (Kraken): the console removes it itself, then its package is deleted if requested; ShadowMountPlus no longer needs to know it. An installation in progress resumes where it left off after the engine restarts.


## [3.1.76] — 2026-11-19

**Preparing to launch from a .pkg**

### Improved

- Read-only diagnostic of the mount table, to prepare launching a game from its .pkg.


## [3.1.75] — 2026-11-19

**Send a .pkg and install it on the other PS5**

### New

- “Install…” for a .pkg package: “Send it to <PS5> and install it there” — verified copy, then the other console installs it itself; a notification says so.

### Improved

- Install with the console: tried for real (Breeze on PS5-01, installed on internal storage).


## [3.1.74] — 2026-11-19

**Install a .pkg package with the console**

### New

- “Install… › Install with the console” for a .pkg package: the console installs it itself, like a Store game, with no server or port; never during a game; the package stays where it is.


## [3.1.73] — 2026-11-19

**Install a game, where it is or elsewhere**

### New

- “Install…” in a game's tools, .pkg packages and Downloads › Orbit and BlackBox: mount it where it is (placed in its drive's games folder, no copy, mounted by ShadowMount), or install it on another drive or another PS5 (verified copy, original kept or removed).


## [3.1.72] — 2026-11-19

**Automatic save sync**

### New

- Sync › Active saves: an “Automatic” setting per PS5 or NAS — when a followed game closes, the save you just played goes over 1 min later; or also every night at 4 AM, both ways. The replaced save is always backed up first; never during a game; a conflict waits for your choice.
- Home: the Sync bubble says “Updated automatically: … · 5 min ago”.

### Improved

- Orbit and BlackBox: the Home bubble announces newly arrived games (backed up or not) with, on equal terms, Back up, Move, Compress to .pkg, See and “Seen”. No more nightly plan for these games.


## [3.1.71] — 2026-11-19

**Orbit and BlackBox: back up what they bring**

### New

- Library: a game that arrived via Orbit or BlackBox carries their badge; its sheet says “Arrived via Orbit” and whether it is not backed up yet.
- Home › To do: an “Arrived via Orbit” card while a game they brought isn't backed up — Back up now (a .pkg that isn't installed goes as is to the NAS, the cloud or the other PS5), back them up every night, compress to .pkg, see, or Not now.
- Plans: new rule “Games that arrived via Orbit and BlackBox” and a preset “New Orbit games, every night at 3 AM”.


## [3.1.70] — 2026-11-19

**Disc games: recognised and backed up**

### Improved

- Packages .pkg: “Send to a NAS, the cloud or another PS5” (verified copy; on the other PS5, into its games folder).

### Fixed

- A game that only has its saves on the console (PS4 disc game, uninstalled game) is no longer taken for an app, and “Back up” from Saves or Home no longer says “item not found”.
- Transfers (TV): going down from “What to do?” to the row below, the cursor keeps its column.


## [3.1.69] — 2026-11-18

**Packages .pkg everywhere**

### New

- Transfers › “.pkg packages”: every .pkg on the drives (made here, Orbit…), with Verify, Copy or move, Delete; never registered in ShadowMount.

### Improved

- Transfers › Compress and “Compress before sending”: choose the format, .ffpfsc image or .pkg package (and its compression); Orbit and BlackBox: “Compress to .pkg” or “to .ffpfsc” for an .exfat image.


## [3.1.68] — 2026-11-18

**A log that survives a restart**

### Improved

- The engine also keeps its log in /data/pbc-backup-center/logs/engine.log (4 MB, plus the previous one): after the console freezes, you can see what it was doing.


## [3.1.67] — 2026-11-18

**Package .pkg: choose the compression**

### Improved

- “Create a .pkg package (Kraken)” asks for the compression: Fast (recommended), Uncompressed (safest, read by every console) or Smaller (slower).


## [3.1.66] — 2026-11-18

**Create a .pkg package (Kraken)**

### New

- Game tools (folder, .exfat, .ffpkg or .ffpfsc): “Create a .pkg package (Kraken)” — a single compressed file, read back and verified before it is kept; the original stays. Dump Forge and PS5Upload code built into the engine (no extra app or port).


## [3.1.65] — 2026-11-18

**Orbit and BlackBox: tidy up their games**

### New

- Downloads › Orbit and BlackBox: “Move to…” a finished game (internal ↔ USB, verified copy, always shown in its new place) and “Free up space…” from a kept archive or leftover, with confirmation; never an open game, a mounted image, or while they are downloading.


## [3.1.64] — 2026-11-18

**Orbit and BlackBox: their downloads in Downloads**

### New

- Downloads › “ORBIT AND BLACKBOX”: what Orbit and BlackBox downloaded on the console (status, drive, size, file not found) and their temporary files (kept archive, download in progress, leftover), read from their side without changing anything.


## [3.1.63] — 2026-11-18

**PS5s warn each other before a plan**

### Improved

- Backup plans to the same NAS or cloud from two or more PS5s: each console tells the others when it starts and when it is done; the others wait, then see its new copies right away.


## [3.1.62] — 2026-11-18

**Every copy knows which PS5 made it**

### Improved

- Every new copy records the PS5 that made it; “Keep N” never removes another PS5's latest copy stored in the same place.


## [3.1.61] — 2026-11-18

**Web: the same card details as the TV**

### Improved

- Web: dates as on the TV (“17/11/2026 14:36”), “Everywhere” on two lines, the other PS5's card with its status and address, and in Sync the summary of a device family (“Switch · 2 targets”).


## [3.1.60] — 2026-11-18

**Dropbox links, and reconnecting a cloud account without losing anything**

### New

- Share: “Dropbox link · from the service”, like OneDrive (permission granted by Dropbox).

### Improved

- Devices › a Google Drive, OneDrive or Dropbox account › Settings: “Reconnect the account” (Cross on “Account” on the TV) keeps the device and its settings; only the access is renewed.

### Fixed

- Sync with the other PS5: no more wrong “nothing to sync” for a few minutes after it comes back (the plan is redone as soon as its list of copies changes).


## [3.1.59] — 2026-11-18

**Automatic plans without duplicates, even with several PS5s**

### New

- Plans: a save or a game already at the destination (same content, copied by any PS5) is no longer copied again; the summary says “already up to date”. Nothing is reread to know it.
- Plans: when another PS5 is already running a plan to the same destination, this one waits 10 minutes, then only does what is missing.

### Improved

- “Keep N copies” also applies to the NAS and the cloud; extra copies go to the trash (30 days), and a saves backup never touches the full-game copy anymore.


## [3.1.58] — 2026-11-18

**Web like the TV: the last 14 layout gaps**

### Improved

- Web: Saves grouped by family (This PS5, NAS, Cloud), “Where to store?” window as cards, game sheet in two columns, device actions in “≡ Actions”, Network › OPTIONS, closing Settings returns to the previous tab.
- Web: “Ask where to store: No” is respected everywhere; Saves counters and Settings sentences say the same as the TV.


## [3.1.57] — 2026-11-18

**“Newer copy”: no more false alert when the copy is the same save**

### Fixed

- “Newer copy”: a copy holding the same save as the console (same content read, wherever it is stored) is no longer announced as “newer”. The time used is the one written in the save, no longer the file's time, which changes when a full copy is remounted.


## [3.1.56] — 2026-11-18

**Internet link: the public address only**

### Fixed

- BitTorrent link: the Internet link now carries only the console's public address (no longer its home address, useless and revealing for someone far away); the QR code is easier to scan.


## [3.1.55] — 2026-11-18

**Public link: the BitTorrent link also works from the Internet**

### New

- BitTorrent link: like the .zip link, it has a home link and an Internet link (with the console's public address, found as for a remote PS5). A button switches between them; the QR code and the address follow.

### Improved

- Web: a link's window now has a single QR code, with the “Home link / Internet link” button, like the Square button on the TV.


## [3.1.54] — 2026-11-18

**My links says when a link no longer shares anything; web menus have their title**

### Improved

- My links: a BitTorrent link whose files were moved or removed is marked “files not found” instead of “shared”, with what to do.
- Web: the “Send to…” and “Format of…” menus have their title at the top, as on the TV; a PS5 that is off no longer offers “See its drives”.


## [3.1.53] — 2026-11-18

**My links: all your links in one place; the web catches up with the TV (full audit)**

### New

- Downloads › My links: every link given from the console (BitTorrent, console .zip, MEGA, OneDrive), with its QR code, its address and a way to stop it. MEGA and OneDrive links are kept there.

### Improved

- Creating a link for a game opens its window right away: you watch the preparation, then the QR code appears. The link carries the game's name.
- Stopping a BitTorrent share touches nothing: the game and its files stay where they are.
- Web: 52 gaps filled after a full audit of the TV (Tasks, Where to put, Home “In the cloud”, Devices and Sync by family, Compare, .torrent files by folder, clear error messages…).
- Web: the sentence under menus, like the TV; “Progress tab” to put saves back on an account, on both sides.

### Fixed

- Safeguard: a version is no longer frozen if a TV text is missing from the web without a written reason; the exception list was reviewed line by line.


## [3.1.52] — 2026-11-18

**Web like the TV; “Browse” and sharing a game image fixed**

### Improved

- Web › Settings › Where to put and Settings › Devices: like the TV; the Devices tab no longer holds these settings.
- Web: the tagline under the name, and on each page the title and its sentence on the same line, with the TV's sentences.
- Web: “Details” in menus, “Close without saving” in forms, “Actions” for the ≡ menu (TV and web).

### Fixed

- “Browse” on a game stored as an image (for example on USB): the location comes from the engine, on the TV and the web; it said “Unknown location”.
- Share my copy › The game: a game stored as an image can be shared (the image itself); it said “not stored as a single folder”.
- My share links: BitTorrent links created here appear too, with their QR code.
- BitTorrent: large files (a game image, tens of GB) can be shared and downloaded; the console refused the memory (“Cannot allocate memory”).


## [3.1.51] — 2026-11-18

**Web: Transfers, Tasks and Settings like the TV**

### Improved

- Web › Transfers: the TV's title and order (This PS5 first), the count of each family and the names of the cloud services.
- Web › Tasks: a message for each filter when the list is empty.
- Web › Settings: “Devices” and “Where to put” in the menu, like on the TV.


## [3.1.50] — 2026-11-18

**Web: the same cards and bubbles as the TV**

### Improved

- Web › Saves: the same cards as the TV (Everywhere with the number of copies, Compare · sort with what is left, Trash) and the three colored bubbles (safe duplicates, not compared yet, to check).
- Web › Devices: the TV's families (This PS5, each NAS, Cloud, other PS5s); a click opens the family's devices and their actions.
- Web › Sync: cloud services grouped in a “Cloud” card, with their logo.


## [3.1.49] — 2026-11-18

**Beta: share, download, resume after an interruption**

### New

- Share my copy: a link and a QR code for a game, a ROM, an app or a save, wherever it is stored (console, USB, NAS, cloud); home or Internet link, BitTorrent, MEGA or OneDrive.
- Downloads: magnet links and .torrent files; MEGA, Google Drive, Dropbox and OneDrive share links opened like a drive; what comes from a remote PS5.
- Full BitTorrent: torrent details, queue, limits, encryption, stop sharing, copy or move at the end to any place, watch folder.
- Resume after an interruption, without starting over: backups, restores, NAS (FTP, SMB, WebDAV) and cloud (MEGA included).
- MEGA arrives in Devices › Cloud.
- Settings › Network: the app's ports, “Test ports”, the public address.
- “Where to put”: a single place choice, offered at each action.
- TV: the same help at the bottom of every screen, windows and bubbles drawn the same way, L3 for all menus, R3 for Settings.


## [3.1.48] — 2026-11-18

**MEGA links: the key in the account's vault**

### Fixed

- MEGA folder link: the share key is written to the account's key vault (backed up on the console first), so the link shown by the MEGA site and app works too; an already shared folder keeps its key.


## [3.1.47] — 2026-11-18

**Share…: sort before choosing**

### New

- Downloads › Share…: first what (PS5 games, PS4 games, ROMs by console, apps, game saves, a file or folder), then where (each drive, with the count), then the A to Z list.
- “The game” can also be shared for a ROM or an app (BitTorrent link, no copy), wherever it is (internal, USB…).


## [3.1.46] — 2026-11-18

**Share my copy: “What to share?”**

### New

- Share my copy: a first choice “What to share?” — the game (with where it is: internal, USB…, and its size), a game save, or a copy of the game with its save.


## [3.1.45] — 2026-11-18

**Share my copy: clearer, and the game too**

### New

- Share my copy: the installed game is offered first (BitTorrent link, no copy).

### Improved

- Each copy says what it is: “Game save · account” or “Copy of the game”, then the place, date and size.
- Link kinds renamed: console link (.zip, home or Internet), BitTorrent link, service link (MEGA, OneDrive).
- Public address: Settings › Network shows it with “Refresh”; Triangle asks again in a link's window.


## [3.1.44] — 2026-11-17

**Share over the Internet and OneDrive; BitTorrent options**

### New

- BitTorrent options: watch folder (a .torrent dropped into “Torrents à ajouter” is added by itself).
- Torrent menu: first and last pieces first, super-seed, rename.

### Improved

- Share my copy: besides the home link, an Internet link with the console's public address (the one set for “Remote PS5”, otherwise the one learned by BitTorrent); on the TV, Square shows it.
- The console's port 52370 is opened on your router by UPnP if the option is on (Settings › Network); otherwise forward it on your router.
- The public address is asked again each time the app opens (also when you close and reopen it).
- OneDrive link for a copy on OneDrive (read-only, for anyone with the link; can be removed); a copy on Google Drive or Dropbox goes through the console's temporary link.
- If it is not known, the public address is asked from an online service (api.ipify.org), like remote play gateways do.

### Fixed

- Web: windows where you type (magnet link, .torrent file, peer, tracker, identity name) keep what you typed.


## [3.1.43] — 2026-11-17

**Share my copy: a link and a QR code**

### New

- Share my copy: from a game's sheet (“Share my copy…”) or a copy's menu (“Share this copy…”), a link to a copy wherever it is stored (console, USB, NAS, cloud), with its QR code.
- Console temporary link: the copy downloads as a .zip on the home network, read straight from where it is stored; it stops by itself (1 hour, 24 hours or 7 days) or by hand in “My share links”.
- BitTorrent link for a copy on a console drive, MEGA link for a copy on MEGA.


## [3.1.42] — 2026-11-17

**Where to put: the PS5 photo for other PS5s**

### Fixed

- “Where to put?” and Settings › Where to put: other PS5s show the PS5 photo, like this PS5 (no longer the group picture).
- “Where to put?”: “Up / down to see more” as soon as there are more choices below, and no family title left alone at the bottom.
- Home: a game on a USB drive shows “USB 0”, not a drive identifier.


## [3.1.41] — 2026-11-17

**Resume after an interruption: WebDAV and MEGA**

### Improved

- Copy to a NAS over WebDAV: a big file goes in 16 MB pieces; after a stop, the copy resumes where it was (if the server allows it, like Synology).
- Copy to MEGA: after a stop, the upload resumes at the first missing piece instead of starting over.


## [3.1.40] — 2026-11-17

**Small fixes: incoming connections, ratio, texts**

### Fixed

- Settings › Network › “Accept incoming connections” really applies (Yes by default; No: outgoing only, and links created here no longer carry the console's address).
- The ratio is written with the language's decimal separator (1.5 in English).
- Web: the help of “Where to put downloads” correctly says “a drive of this console”.


## [3.1.39] — 2026-11-17

**BitTorrent: encryption, peer by hand, blocked addresses**

### New

- BitTorrent options › Protocol encryption: preferred, required or disabled (encryption hides from internet providers that this is BitTorrent).
- Torrent details › Actions › Add a peer by hand (ip:port): connect directly to another PS5, a PC or a friend.
- BitTorrent options › Blocked addresses: addresses or ranges (1.2.3.0/24, 1.2.3.4-1.2.3.9) with which nothing is exchanged.


## [3.1.38] — 2026-11-17

**Fix: the copy at the end of a download**

### Fixed

- When done, copy to: the copy never started (destination “not found” every time).


## [3.1.37] — 2026-11-17

**BitTorrent: when done, copy or move; links reachable from the Internet**

### New

- BitTorrent options › When done, copy to: a verified copy of each finished download goes on its own to any device — drive, NAS, cloud, Switch or another PS5 (into its “Downloads” folder).
- BitTorrent options › When done, move to: the files go to another drive of this console (for example USB) and stay shared from there.
- Magnet links also carry the console's public address: someone far away connects to it directly, if the BitTorrent port is open on the router (or through UPnP).
- Torrent details › Actions › Download the .torrent on the phone: a QR code opens the console's web page, which offers to save the file.

### Fixed

- When done, copy to a NAS or the cloud: the “Downloads” folder is created in the folder set for the device (in 3.1.36, at the server's root: refused).


## [3.1.36] — 2026-11-17

**BitTorrent: when done, copy or move**

### New

- BitTorrent options › When done, copy to: a verified copy of each finished download goes on its own to the NAS, the cloud or a drive (into its “Downloads” folder).
- BitTorrent options › When done, move to: the files go to another drive of this console (for example USB) and stay shared from there.


## [3.1.35] — 2026-11-17

**BitTorrent options: stop sharing, space on the drive, full speed at night**

### New

- Settings › Network › BitTorrent options: stop sharing a finished torrent after a ratio (1.0 to 5.0) or a time (1 h to 7 days); it becomes “finished · sharing stopped”. Links created here never stop on their own.
- Reserve all the space at the start, and refuse (pause with a message) a torrent bigger than the drive's free space.
- Full speed at night: during the chosen hours, downloads run without a limit (the rate during a game still applies).


## [3.1.34] — 2026-11-17

**Create a link: share a file or folder with BitTorrent**

### New

- Downloads › BitTorrent › Create a link: pick a file or folder on the console; it computes the .torrent then shares it from where it is (nothing is copied). The magnet link QR code is in its details.
- Torrent details: save the .torrent file next to the files (and, on the web, download it).

### Improved

- The magnet link also carries the console's local network address: another PS5 or a PC at home connects to it right away.
- A link created here never offers to delete your files: only “Stop sharing”.


## [3.1.33] — 2026-11-17

**“Where to put”: one place picker for everything**

### New

- Settings › Where to put: the default place for backups, downloads and “Send to…”, and “Ask where to put it” (Yes: default first; No: always the default).
- Library: with items checked, ≡ › “Send to…” copies them to a drive of this console (verified copy), a NAS, the cloud or another PS5 (verified backup).

### Improved

- A single “Where to put?” window for Back up (game sheet, Square), adding a torrent and the settings: the default first, then this PS5, other PS5s drive by drive, Switch, NAS and cloud.

### Fixed

- Game sheet: “Back up” without choosing a place went to the first USB drive instead of the default destination you set.


## [3.1.32] — 2026-11-17

**Circle now also closes file windows**

### Fixed

- Devices › Browse and every folder or file picker: Circle closes the window, as everywhere; Left goes up to the parent folder (and Triangle when it isn't used to choose the folder).


## [3.1.31] — 2026-11-17

**Downloads: two clearly separate families**

### Improved

- Downloads in two rows: “Share links” (MEGA, Google Drive, Dropbox, OneDrive: open like a disk, with “+ Share link”) and “BitTorrent” (“+ Magnet link”, “+ .torrent file”, “Create a link” coming soon, “BitTorrent settings”); below, “For both” shows where downloads are stored and the speeds, and opens those settings.
- “+ Magnet link”: type the link with the keyboard or, easier, paste it from your phone (QR code, as for share links).

### Fixed

- Choosing a .torrent file: Circle closes, as in the other windows; Triangle goes up to the parent folder.


## [3.1.30] — 2026-11-17

**Torrents: details, actions, QR code**

### New

- Downloads: Cross on a torrent opens its details — General (progress, speed, time left, received / sent / ratio, seeds and peers, availability, pieces, limits), Files (progress and priority of each: high, normal, low, or don't download), Peers and Trackers; the magnet link is shown as a QR code to scan and share.
- Torrent actions (≡): force start, find peers now, move to the top / up / down in the queue, download in order, limit this torrent, recheck files, remove or delete. In the Trackers tab: add a tracker, change the address of a tracker that moved, or remove it.

### Improved

- Settings › Network: “Downloads at the same time” (the others wait their turn in the queue) and “Maximum connections”.
- The torrent list follows the queue order and says when a torrent is waiting its turn or was force-started.


## [3.1.29] — 2026-11-17

**Fixes: windows return where you came from**

### Fixed

- Downloads: the chosen card keeps its highlight after visiting the top bar; a received link's files, opened from Downloads, go back there with Circle; the .torrent file picker closes with Triangle.
- Library: Circle first unchecks everything that is checked (the bottom help says so).


## [3.1.28] — 2026-11-17

**Downloads: magnet links and .torrent**

### New

- In Downloads, “+ Add a link” also takes a magnet link or a .torrent file: you choose the disk to store it on (the one from Settings › Network comes first), then follow its progress (sources, speed, time left) and pause, resume or remove it (TV and web).

### Improved

- Settings › Network: Right / Left switch columns, Cross opens the list of choices; speed limits also apply on the local network.
- Compare · sort: “Compare all” is there too (Triangle on the bubble, and in the list's ≡ actions; a button on the web).


## [3.1.27] — 2026-11-17

**BitTorrent in the engine (first step)**

### New

- The engine can now receive and share over BitTorrent (libtorrent, built in: nothing to install); the screens come next.


## [3.1.26] — 2026-11-16

**BitTorrent ports: never Backup Center's**

### Improved

- The BitTorrent port range can be set freely on each console (to suit its firewall), except 52370 to 52373 and the “Remote PS5” port, which stay with Backup Center; a range touching them is refused.


## [3.1.25] — 2026-11-16

**Downloads: speed and remote PS5**

### Improved

- Downloads follow the maximum speed set in Settings › Network, and slow down while you play so online games aren't affected.
- What comes from a remote PS5 (over the Internet) shows in Downloads; the TV tab shows how many are running, like the web.


## [3.1.24] — 2026-11-16

**Settings › Network**

### New

- New Settings › Network section (TV and web): the console's ports and what to open on the router or firewall, “Test the ports”, the “Remote PS5” port, and BitTorrent settings (TCP / UDP protocol, one port or a range, incoming or outgoing only, UPnP, DHT, speeds, where to store downloads: internal, USB, NAS or cloud), ready for when it arrives.


## [3.1.23] — 2026-11-16

**All bubbles in the same design**

### Improved

- Bubbles that were still flat (Games players, Transfers actions, Whole games, Switch ↔ Eden, Home cards) now have the same soft shadow and sheen as the others; the other PS5 shows all its drives (Internal, USB 0, USB 1), even empty ones.


## [3.1.22] — 2026-11-16

**Backups: clearer bubbles**

### Improved

- Each Backups bubble says how many copies it holds, then a detail (free space, network, number of places); the other PS5 opens into one bubble per drive, like this PS5.
- Compare · sort and the Trash become two full bubbles side by side, with new icons; Compare says how many games are left to compare.

### Fixed

- Cards no longer have square corners: the blue sheen follows their rounding (Home, Backups, Sync, Transfers, Downloads, Devices).


## [3.1.21] — 2026-11-16

**Device cards: the same design everywhere**

### Improved

- Backups, Sync, Transfers and Downloads show devices with the same card as the Devices tab; a family shows its count as “3 ›” and what it contains, instead of a blue dot or cut-off text; nothing is removed.
- The Cloud family says “See services ›”: they are services, not devices.


## [3.1.20] — 2026-11-16

**Compatibility: the real numbers**

### Improved

- Web: the last browser confirmations (unpairing two PS5s, moving a save to another account, transferring a Switch ↔ Eden save) now use the app's own windows.

### Fixed

- A backport made for a newer system than the console's is no longer reported as “present”: the sheet says “Won't start” with the backport's SDK (e.g. 007 First Light, backport in SDK 4.00, on a PS5 on 3.21). Without a backport, the sheet gives the game's SDK or the required system (TV and web).


## [3.1.19] — 2026-11-16

**Game sheet: actions as lines**

### Improved

- The game sheet follows the window model: what to back up on the left, actions as lines on the right (Back up with its destination, Details, Scheduled plans…, Browse, Send to a PS5…), with what removes or erases at the bottom, in red; Square changes the destination (TV and web).

### Fixed

- After an update, the “Automatic reopen” countdown no longer overlaps the window help.
- “What's new” follows the window model: the support line under a divider, then the help inside the window (Cross: got it, Circle: close) instead of a separate button.


## [3.1.18] — 2026-11-16

**Forms: the same frame as menus**

### Improved

- Forms (adding and setting up a device, cloud sign-in, emulator, plans, folder picker, keyboard) now have the same frame as menus, with their button help inside the window, at the bottom.

### Fixed

- Devices: with many devices (Cloud family open), cards no longer shrink and spill over each other; the page scrolls.


## [3.1.17] — 2026-11-16

**Confirmations: the same shape everywhere**

### Improved

- Hold-to-confirm windows all ask a question, without shouting capitals; the instruction uses the right verb (confirm for an uninstall) and appears only once; a sentence says what will happen (TV and web).


## [3.1.16] — 2026-11-16

**Keys only at the bottom**

### Improved

- TV: texts no longer name a button (“Cross: see the devices” becomes “See the devices ›”); the bottom help does that. Hold-to-confirm windows keep their instruction. Settings › Take everything back shows its buttons at the bottom.
- Windows: they all close the same way (Circle; Escape on the web) and you land exactly where you were; a window opened from a menu takes you back to that menu. The folder browser no longer closes with Options.


## [3.1.15] — 2026-11-16

**Nothing starts at once; “Details” everywhere**

### Improved

- Sync “Bring up to date” and restoring a full backup now ask first (saying what will happen) instead of starting on the first press (TV and web).
- TV: in menus, “All versions” is now called “Details” and always comes right after the expected action.


## [3.1.14] — 2026-11-16

**Windows: one frame**

### Improved

- TV: action menus, confirmations and the game sheet share one frame (soft shadow, title band); in a menu, whatever erases goes to the bottom, in red, under a line; the help (✕ Choose · ○ Close) is inside the window.


## [3.1.13] — 2026-11-16

**Bottom help: the same everywhere**

### Improved

- TV: the bottom help follows the same order everywhere (✕, △, □, L2, R2, Options, ○), the same size, on one line; Options is drawn with its three lines and always opens “Actions”; □ names the next view directly; Library: “R2 Next page”, “○ Back”.
- TV: every tab has the same page header (its name, a short sentence beside it, never cut); Transfers gets its name back. Web: Escape in a confirmation means “Cancel”, the dialog takes focus and gives it back when it closes.


## [3.1.12] — 2026-11-16

**R3 Settings shown at the bottom of the screen**

### Improved

- TV: at the bottom right, “L3 Menus · R3 Settings” on one line, “Home · Hold 2 s: quit” on the other; the left help never runs under it.


## [3.1.11] — 2026-11-16

**L3: the top bar on two rows**

### Improved

- TV: L3 no longer opens a window: the top bar itself grows to two rows (same glass, L1 / R1, active tab line); Cross goes there and it goes back to one row. “L3 Menus” is shown at the bottom right, next to Home.


## [3.1.10] — 2026-11-16

**L3: all menus; R3: Settings**

### New

- TV: L3 (left stick click) opens all menus on two rows, so you go down to one instead of scrolling with L1 / R1; R3 (right stick click) takes you to Settings from anywhere, Circle brings you back. Both are shown at the top of the screen.


## [3.1.9] — 2026-11-16

**Where tasks come from, save dates, details from Saves**

### Improved

- Tasks: a download or a copy shows where it comes from and where it goes (service logo: MEGA, Google Drive…), in the list and in Details.
- Saves: a game whose save does not say where you are shows at least its date (“Save from …”); Cross › “See details (all versions)”.


## [3.1.8] — 2026-11-16

**Sync card you can dismiss, service logo on a download**

### New

- Home: the Sync card announcing available targets can be dismissed (Options › Not now); it only comes back if there is something new (one more game, save or device), marked “new”. A sync in progress is always shown. Sync › Options: show it again.

### Improved

- Home: a download from a received link shows its service logo (MEGA, Google Drive, Dropbox, OneDrive).


## [3.1.7] — 2026-11-16

**Progress for every game in Parties, OneDrive and Dropbox links**

### Improved

- Parties: games whose save does not say where you are (Elden Ring, Hades…) show the progress read from the save, as in Backups; games not on this PS5 show the progress of their most advanced copy (“Copy: …”).

### Fixed

- Received links: a OneDrive file shared on its own now opens (it was refused); an empty Dropbox folder opens too.


## [3.1.6] — 2026-11-16

**Mid-file resume to the cloud**

### Improved

- Backups and copies to Google Drive, OneDrive, Dropbox and S3 (Backblaze…): after a pause, a dropout or a restart, the current file resumes where it stopped (upload session kept on the console for a few days, beginning checked against the original) instead of starting over.


## [3.1.5] — 2026-11-16

**Mid-file resume: restores and NAS**

### Improved

- Restores: after a stop, what is already in place and identical is skipped, and the file in progress resumes where it stopped (manifest checksum verified, redone from scratch if it doesn't match).
- Backups and copies to a NAS over FTP or SMB: the file in progress also resumes mid-file instead of starting over.


## [3.1.4] — 2026-11-16

**Backups: resume mid-file**

### Improved

- Full backups (game, app) to a USB drive: after a pause or a restart, the big file in progress resumes where it stopped instead of starting over; the kept beginning is checked against the original.


## [3.1.3] — 2026-11-16

**Downloads: a menu of its own, and resume without starting over**

### New

- Downloads menu (TV and web): received links, downloads in progress with speed and time left, finished ones with average speed, and its own notifications (done, link expiring soon). Opening a link leads to its files: pick, download.
- Google Drive, Dropbox and OneDrive share links (personal accounts), on top of MEGA: folder or file, read-only, no account needed.

### Improved

- Copies and downloads: after a pause, a dropout or a restart, they resume mid-file where they stopped; the kept beginning is checked against the source.


## [3.1.2] — 2026-11-15

**Temporary share: a received link becomes a place**

### New

- Share link: paste a MEGA link you received (folder or file), from your phone thanks to the QR code; it becomes a read-only place for 1, 7 or 30 days, usable in Transfers, Files and Backups (restore a shared save). Nothing is ever written there, and the link is never shown again.


## [3.1.1] — 2026-11-15

**MEGA arrives in Devices › Cloud**

### New

- MEGA: a MEGA account is added in Devices › Cloud with its e-mail and password (kept on the console); backups, restore and sync just like a NAS. Files are encrypted on the console before upload, as MEGA does.
- Trash: delete a copy for good, or empty a place's trash (TV: Options; web: buttons), always with the hold-to-confirm button.

### Improved

- Same buttons everywhere: Square no longer deletes (Delete is in Options, with the preview); △ pauses or resumes a task, and stopping it goes through Options with a confirmation (Tasks and Transfers); Files and folder picker: ✕ opens, ○ goes up, △ picks this folder, L2 ticks.
- Accurate hints everywhere: Library (the filter row has its own hints, ○ goes back to the filters, R2 shown), Saves (Switch and Eden cards), Settings (nothing wrong in the menu, General shows left / right); Home: △ = Search everywhere and ✕ says what it does on each row.
- Bottom hints: L1, R1, L2, R2 and Options drawn as real buttons; two balanced lines, below the content. Backups: Down goes from Compare and sort to the Trash.

### Fixed

- MEGA has its logo everywhere from startup (Home, Sync, Transfers, Devices, Backups, web page); Circle goes back to the places from Compare and sort or the Trash, and « Back » is shown there.


## [3.1.0] — 2026-11-15

**One hub for your games and saves, everywhere**

### New

- Google Drive, OneDrive and Dropbox are available: everyone connects their account from their phone; backup, restore and sync just like a NAS.
- Compare and sort: every copy of a game side by side, judged by content (Identical, Older, Further along), including copies stored on the other PS5.
- Same save in several places: wanted copies, never treated as a problem; « Tidy up » and « Keep only on… » when you want to consolidate.
- Origin of restored saves: a copy of a restored save keeps the original save time, and both PS5s know it.
- Compress an image on the same disk.

### Improved

- Backups open in a few seconds, even with several NAS and clouds; no more « server busy » or timeouts.
- Each copy carries its fingerprint and the result of its reading: a copy already read is never read again, even by the other PS5, which is told right away.
- Faster game transfers that resume in the middle of large files.
- Clearer Backups: bubbles in order (Everywhere, This PS5, the other PS5, NAS, Cloud, Trash) with a preview of hidden ones, each service's logo, and each place talking about its own copies.
- Updates: never again a rollback on its own.
- Saves: short labels, never cut; « Not on this PS5 » means a copy exists but the save isn't on this console.
- Backups: Compare and sort and the Trash stacked on the right, always visible; places scroll on the left.
- On-screen button hints never cut (smaller, or on two lines, in every language); « △ Stop the task » instead of « Cancel » in Transfers; « Options: actions » in Compare.

### Fixed

- No more false « newer copies » after a restore or a round trip through the cloud.
- OneDrive sign-in and folder creation fixed.
- Each service's logo from startup (Backblaze no longer shows the generic cloud first); at the end of a row the card says « ‹ 2 before » instead of « + 2 more › »; Sync counts targets, not devices.
- Hints fixed: All versions shows △ Compare this game and R2; the Trash shows « Left / right » only on the bubbles; the heart and search have their own hints; □ really changes group in Tasks; « Down: Start button ».


## [3.0.33] — 2026-11-15

**Fingerprints: never a false « different save »**

### Fixed

- A copy never read no longer gets a content letter from its bytes: the same save can be written with different bytes. The fingerprint is used to reuse a reading already done and to flag a byte-for-byte identical copy.


## [3.0.32] — 2026-11-15

**Copies recognised by their fingerprint, readings kept beside the copies**

### New

- Each copy carries its fingerprint (disk, NAS, cloud index): identical copies are recognised right away, without comparing; a copy identical to one already read reuses its analysis without downloading anything.
- After a copy is read, the result is stored beside it (« lecture.json »): another console, or this one after a reinstall, doesn't read it again.

### Improved

- After a backup, a restore or a comparison, the other PS5 is told right away (before: up to 10 minutes).


## [3.0.31] — 2026-11-15

**Clearer Backups: each place talks about its own copies**

### Improved

- Backups: in a place (USB, NAS, cloud), each game says how many copies are HERE, the latest, and how many other places keep it; « duplicates here » or « Nothing to do here ».
- Bubble names never cut; service names without repetition (« Google Drive » instead of « Google Drive (Google Drive) »); the game sheet counts the same copies as the list (not the other PS5's); clearer R2 hint.

### Fixed

- Trash: every place at a glance at the top, the same NAS through two paths shown once, and the game title found even when the file name turns « : » into « _ ».


## [3.0.30] — 2026-11-15

**Same save in several places: wanted copies, tidied only on request**

### New

- Compare and sort: « Several places » filter; « Tidy up » for one game (checked = kept) and « Keep only on… » for all games, with the preview before the trash. The last copy of a save is never removed.

### Improved

- The game sheet and Compare say « Same save in N places: … » instead of talking about duplicates; only a second copy in the same place is still a duplicate.


## [3.0.29] — 2026-11-15

**Compare: copies stored on the other PS5 get their real verdict**

### Improved

- A copy on the other PS5 that it hasn't read yet says so (« not read yet by … ») instead of « content not compared ».

### Fixed

- Compare: a copy stored on the other PS5 is judged like the others (Identical, Older, Further along…); the other PS5 sends the fingerprint of the content it read, with no extra reading.


## [3.0.28] — 2026-11-15

**Preview of hidden bubbles, compress on the same disk**

### New

- Transfers: Compress accepts the same disk as destination; the image is written next to the game and verified, the original game stays in place.

### Improved

- Backups and Transfers: when everything doesn't fit, the last spot shows what's hidden (« + 3 more › NAS · Cloud · Compare »).


## [3.0.27] — 2026-11-15

**Comparing with the other PS5**

### Improved

- Backups: the other PS5's bubble describes its own copies; an already compared copy shows the save date instead of « content not compared ».

### Fixed

- Compare: copies stored on the other PS5 no longer block the comparison (it compares them itself); they show « On … » and Cross brings them here.


## [3.0.26] — 2026-11-15

**Compare and sort**

### New

- Backups › Compare and sort: every copy of each game side by side (active save, identical, older, further along, not compared, to check), the key numbers on top and 7 filters; « Compare » on a game's sheet opens it directly.


## [3.0.25] — 2026-11-15

**Backups: the other PS5 right after this PS5**

### Improved

- Backups: bubbles are ordered Everywhere, This PS5, the other PS5, NAS, Cloud, then the Trash (the other PS5 is no longer last).


## [3.0.24] — 2026-11-14

**Sync without overload**

### Fixed

- Home: sync plans (one per NAS, cloud and other PS5) are requested one after another, no longer all together every 15 s (the engine was overloaded: « server busy »).


## [3.0.23] — 2026-11-14

**No more « server busy »**

### Fixed

- Sync: each NAS and cloud plan shares one backup computation instead of starting one per device (5 at once saturated the console: « server busy »).


## [3.0.22] — 2026-11-14

**OneDrive folders created**

### Fixed

- OneDrive: folders are created through the parent folder's ID (Microsoft refused creating them at the root of the app folder).


## [3.0.21] — 2026-11-14

**Adding OneDrive**

### Fixed

- OneDrive: the app folder is created before « PBC Backup » (adding failed with « cannot write »).


## [3.0.20] — 2026-11-14

**OneDrive without waiting**

### Fixed

- Cloud: no more 2-minute wait when the console moves from one server to another (OneDrive got stuck when added and on every upload).


## [3.0.19] — 2026-11-14

**OneDrive sign-in fixed**

### Improved

- The other PS5 is read again 30 s later if it was still recounting its backups.

### Fixed

- Code sign-in (OneDrive, Google Drive): a successful connection is no longer erased when the TV and the web page check at the same time (« code already used »).


## [3.0.18] — 2026-11-14

**Origin of restored saves**

### Fixed

- A copy made of a restored save (not played since) keeps the original save's time, here and on the other PS5: a round trip through the cloud no longer makes it look « newer ».


## [3.0.17] — 2026-11-14

**Restores recognized, Transfers in order**

### Improved

- Transfers: This PS5, « + Add a service », other PS5s, consoles, NAS, then cloud; Left at the start of an open bubble closes it.
- Backups: the same NAS added twice (FTP and WebDAV) now has a single card; Backblaze shows its logo in tasks and backups.

### Fixed

- A restored save is no longer « newer on the console » until it is played again.


## [3.0.16] — 2026-11-14

**Backups right away**

### Improved

- Backups: the list shows right away (the last known one, kept even after a restart) and updates itself as soon as the new count is ready.


## [3.0.15] — 2026-11-14

**Backups open in seconds**

### Fixed

- Backups: comparisons already made are read once instead of once per game; no more « timed out » right after startup.


## [3.0.14] — 2026-11-14

**Faster Backups at startup**

### Improved

- Backups: the TV, the web page and the other PS5 share one computation of the list instead of each starting its own.


## [3.0.13] — 2026-11-14

**Backups without waiting for the cloud**

### Fixed

- Backups: no more « timed out » or « server busy »; the screen shows the last NAS and cloud list right away, reread in the background.


## [3.0.12] — 2026-11-14

**Grouped devices, cloud on Home**

### New

- Transfers: « + Add a service » bubble always visible and Square to add a NAS, a cloud, a Switch or a PS5.

### Improved

- Devices, Backups and Sync: one bubble per family (this PS5, NAS, cloud, Switch); Cross opens it on its devices, Circle closes it.
- Home: « In the cloud » row (anything with a copy on Google Drive, Backblaze…) and « Cloud » in available backups; Sync shows the service's logo.


## [3.0.11] — 2026-11-14

**Resume in the middle of big files**

### Improved

- Transfers and verifications of an image (one big file): after a pause, an engine stop or a console restart, they resume where they got to instead of starting over.


## [3.0.10] — 2026-11-14

**Saves: no more false « newer »**

### Improved

- Saves and Everywhere open quickly even during a big transfer or verification (they give the screen priority), and the cloud no longer blocks the list.

### Fixed

- A save restored or copied elsewhere (other PS5, NAS, cloud) with the same progress is no longer « to back up » or « newer »: only a game actually played counts.


## [3.0.9] — 2026-11-14

**Take over a copy already made**

### New

- Game transfers: a copy already made (interrupted move) can be checked against the original, both read at the same time, then the game is registered at the new place; the original stays until you remove it.


## [3.0.8] — 2026-11-14

**Updates: no more automatic rollback**

### Fixed

- Updates: a newer version received from another PS5 is no longer replaced by the site's older version (only « Go back to the previous version » and « Available versions » go back).


## [3.0.7] — 2026-11-14

**Game transfers: faster, resumable**

### New

- « Stop verification » keeps the copy (marked unverified); an unverified move keeps the original.

### Improved

- Verification now rereads only the copy (the original's fingerprint is taken during the copy): half as much to reread, several files at once.
- Pause, restart or power cut: the transfer resumes where it stopped instead of copying everything again.
- Full (SHA-256) or quick (presence and size) verification, your choice; verification has its own bar and time left.

### Fixed

- Game transfers: the counter no longer adds copy and verification together (a 318 GB game no longer shows 636 GB or hundreds of hours).


## [3.0.6] — 2026-11-14

**Transfers and tasks: fixes**

### Fixed

- Transfers: each device appears only once in its bubble (Switch and cloud were counted twice, the NAS bubble was missing).
- Tasks: size and file count are shown again next to « from → to ».


## [3.0.5] — 2026-11-14

**Clearer transfers and tasks**

### Improved

- Transfers: one bubble per device or family (this PS5, the other PS5, NAS, cloud, Switch); Cross opens it on its drives or accounts, Circle closes it.
- Transfers: FROM and TO each fit on one scrolling row, grouped by family (PS5, drives, NAS, cloud, Switch); Google Drive and Backblaze have their logo; « What to do? », « Full backup » and « Transfers in progress » always stay on screen.
- Tasks: each backup or restore shows where it comes from and where it goes (with logos) and who started it (plan, sync).
- Transfers in progress: Triangle removes a finished transfer from the list; Options clears all finished ones.

### Fixed

- Restore from Google Drive: the backup is looked up in the right folder (« PBC Backup » chosen as the device folder).
- Cloud: « Backups and Sync » by default for S3 storage; « cloud » instead of « network » when choosing the destination; the TV form closes when you save from the phone.


## [3.0.4] — 2026-11-14

**One logo per cloud service**

### Improved

- Devices and Backups: each cloud storage shows its service's logo (Google Drive, Dropbox, OneDrive, Backblaze, Amazon S3); the others keep the cloud.

### Fixed

- Browse: if the chosen folder does not exist yet (« PBC Backup » before saving), the browser opens the folder above; it shows the device's name.


## [3.0.3] — 2026-11-14

**Google Drive like a NAS**

### Improved

- Google Drive, OneDrive, Dropbox: backups go to « PBC Backup » by default and the account is used for backups and sync, like a NAS; « Browse » to pick another folder.
- Choosing a folder on a NAS, a Switch or in the cloud: « New folder… » creates it with the keyboard, then opens it.
- Sync: a cloud storage is named after its service (« Google Drive ») instead of « FTP ».


## [3.0.2] — 2026-11-14

**Google Drive: saving fixed**

### Fixed

- Google Drive, OneDrive, Dropbox: « Save » and « Test » no longer close the app; the device takes the service's name.
- Sign-in with a code: « Waiting for your approval… » no longer overlaps step 3.


## [3.0.1] — 2026-11-14

**Google Drive is available**

### New

- Google Drive: connect your own account from your phone (QR code, sign-in at Google), without typing anything on the PS5. The app only has access to the files it creates; Google Drive works like a NAS (backups, destination, sync).


## [3.0.0] — 2026-11-14

**PS5 Backup Center 3**

### New

- Cloud storage (Devices › Cloud): Amazon S3, Backblaze B2, Wasabi, Cloudflare R2, Scaleway, OVHcloud, Synology C2 or any other S3 service, picked by name; the address fills itself in.
- pCloud, Koofr and Nextcloud over WebDAV, preset.
- Google Drive, OneDrive and Dropbox: everyone connects their own account from their phone (QR code, sign-in with the service), without typing anything on the PS5; the app only has access to its own folder. Turned on in an upcoming update.
- Cloud storage works just like a NAS (backups, destination, sync); its keys are pasted from your phone with a QR code.
- WebDAV NAS (Synology, QNAP, Nextcloud…) alongside FTP and SMB; the NAS's self-signed certificate is accepted once, then remembered.
- “Used for”: each NAS holds backups, sync, or both; several syncs can use the same NAS, each in its own folder.
- Tasks: “See the backup”, move a backup to the trash, check several tasks at once.
- Community: “Community messages”, “Leave a message” and “Follow us” (X, YouTube, Reddit); Projects to fund: PS5 Center and its gauge.

### Improved

- Full keyboard: special characters (R1), capitals (L1), é è à ç; each button shows its controller button.
- WebDAV NAS much faster; a NAS added through two protocols is only counted once, and its copies show which one they use.
- Home: a single Sync card that cycles through each device; “Available saves” only names the places that actually have some.
- Library: the whole filter row fits on screen; another console shows at a glance, with clear names (“All its games”, “Missing here”, “Also here”).
- Saves: the Trash is always visible, the “Internal” card appears when there are copies, and several checked items can be deleted at once.
- Report a problem: three clear cards (a bug, a question, a star) with their code; Settings reordered.
- New device pictures (Cloud, FTP / SMB) as tall as the others; our logo on the “Update” card.

### Fixed

- Restore from a NAS: a full backup goes through the guided restore.
- NAS: no more “PBC Backup/PBC Backup” folder; at the root of a Synology, the test tells you to pick a shared folder.
- Saves: the list of places no longer repeats under each game; a copy moved to the trash disappears right away.
- Updates: a file that fails is retried once; a version that is installed but not yet active is shown correctly.
- Small display glitches fixed: cut-off text, QR codes of different sizes, overlapping labels.


## [2.99.46] — 2026-11-14

**Final Reddit link, QR codes the same size**

### Improved

- Follow us: Reddit opens the @ConsoleCenterStudio profile.

### Fixed

- Support the project: all QR codes are the same size (the Bitcoin one looked bigger).


## [2.99.45] — 2026-11-14

**Follow us: X, YouTube, Reddit**

### New

- Settings › Support the project: a “Follow us” row with X, YouTube and Reddit (Discord soon); Cross on a badge shows its code to scan.


## [2.99.43] — 2026-11-14

**Back to the 2.99.41 layout**

### Improved

- Add a device: the Switch picture is a bit bigger (+20%).

### Fixed

- Add a device: the USB drive picture is back where it was in 2.99.41.


## [2.99.41] — 2026-11-14

**The font's heart**

### Improved

- Top of the screen: the “Support the project” heart is the same as the one in “What's new”, only bigger.


## [2.99.40] — 2026-11-14

**Heart redrawn, pictures at the right size**

### Fixed

- Top of the screen: the “Support the project” heart is a real heart, with smooth curves down to the tip.
- Add a device: Cloud, FTP / SMB and USB drive are cropped to what shows; they are as tall as the other pictures, and the USB drive is centered.


## [2.99.39] — 2026-11-14

**Heart properly centered**

### Fixed

- Top of the screen: the “Support the project” heart is symmetric and centered in its circle (it looked shifted and cut off).


## [2.99.38] — 2026-11-14

**Bigger USB drive**

### Improved

- Devices › Add a device: the USB drive picture (the PS5 and its drive) is shown bigger, like Cloud and FTP / SMB.


## [2.99.37] — 2026-11-14

**Bigger Cloud and FTP / SMB**

### Improved

- Devices › Add a device: the Cloud and FTP / SMB pictures, wider than tall, are shown bigger and fill the same space as the others.


## [2.99.36] — 2026-11-14

**More readable logos on the Cloud and FTP / SMB icons**

### Improved

- Devices: on the Cloud and FTP / SMB icons, the logos are bigger and sit on the cloud or the folder, readable on the TV.


## [2.99.35] — 2026-11-14

**HD icons: Cloud and FTP / SMB with their logos**

### Improved

- Devices: Cloud gets its HD picture back with the Google Drive, OneDrive, Dropbox, Amazon (AWS), Backblaze and MEGA logos; FTP / SMB gets its HD folder back with Windows, Mac and Linux.


## [2.99.34] — 2026-11-14

**New icons: Cloud and FTP / SMB**

### Improved

- Devices: new picture for Cloud (a cloud resting on drives) and for FTP / SMB (a network folder in front of a laptop).


## [2.99.33] — 2026-11-14

**Dropbox**

### New

- Cloud › Dropbox: everyone connects their own account from their phone (QR code, “Allow”, then paste the code Dropbox gives); the app only has access to its folder.


## [2.99.32] — 2026-11-13

**More cloud services, by name**

### New

- Cloud: Wasabi, Cloudflare R2, Scaleway, OVHcloud and Synology C2 (S3 keys), pCloud, Koofr and Nextcloud (WebDAV account) can be picked by name; the address fills itself in.


## [2.99.31] — 2026-11-13

**Google Drive and OneDrive**

### New

- Settings › Devices › Cloud: connect Google Drive or OneDrive without typing anything on the PS5. A QR code and a short code: you sign in (email, password, verification code) on your phone, with Google or Microsoft.
- The app only has access to its own folder. The account then holds backups and, if you want, sync, like a NAS; free space is shown.


## [2.99.30] — 2026-11-13

**Keyboard: special characters and controller buttons**

### Improved

- Keyboard: a page of special characters (R1), capitals (L1) and the accented letters é è à ç; each button shows the controller button that does the same (L1, R1, △, □, R2).


## [2.99.29] — 2026-11-13

**Cloud storage: Amazon S3 and Backblaze B2**

### New

- Settings › Devices › Cloud: add Amazon S3, Backblaze B2 or another S3 service (Wasabi, MinIO…). It holds backups and, if you want, sync, just like a NAS.
- Paste the keys from your phone: a QR code opens the web page right on the form.


## [2.99.27] — 2026-11-13

**Shorter Library filters**

### Improved

- Library: “Emulator” and “Location” only show their value once something is chosen; the whole filter row fits on screen.
- Another console's library: “All its games / Missing here / Also here” replaces “Here: all”, and “Refresh” replaces “Rescan”.


## [2.99.25] — 2026-11-13

**Places without repeats**

### Fixed

- Backups: under each game, the list of places no longer repeats; the other PS5 is named only once and a NAS shared with it no longer shows twice.
- Updates: a file that fails to download the first time is retried once before giving up.


## [2.99.24] — 2026-11-13

**Places arrow in the right spot**

### Fixed

- Saves: the › arrow to the other places now sits next to the last place card, no longer next to the Trash.


## [2.99.23] — 2026-11-13

**Each NAS does what you choose**

### New

- Settings › Devices: for each NAS, “Used for”: Saves and Sync, Saves only or Sync only. The same NAS can have one entry per use (e.g. FTP for saves, WebDAV for sync).
- Sync: several syncs with the same NAS, each in its own folder; two syncs in the same folder are refused with an explanation.


## [2.99.22] — 2026-11-13

**Our logo on the Update card**

### Fixed

- Home: the “Update” card shows the PS5 Backup Center logo instead of a purple box.


## [2.99.21] — 2026-11-13

**Internal card in Backups**

### Improved

- Backups: the “Internal” card appears when the internal drive holds copies; the Trash always stays visible on the right.


## [2.99.20] — 2026-11-13

**Previous filter chips, Settings reordered**

### Improved

- Settings: What's new first, Thanks at the very bottom (Report a problem, Projects to fund and Support the project just before).

### Fixed

- Library: back to the previous, well-spaced filter chips.


## [2.99.19] — 2026-11-13

**Clearer Home, Library and Backups**

### Improved

- Home › Available backups: the label only names the places that really hold backups (Here, NAS, the other PS5).
- Library: “Console: this PS5 · 1 other ›” shows you can look at another console.
- Backups: the Trash always stays visible at the end of the places row.

### Fixed

- Library: the whole filter row fits on screen; “All” no longer disappears when you go to “Console”.


## [2.99.18] — 2026-11-13

**Back to the 2.99.16 display**

### Fixed

- Home and Library: back to the 2.99.16 display (the 2.99.17 trials are removed).


## [2.99.16] — 2026-11-13

**Library: filters fixed**

### Fixed

- Library: moving across the types to reach Console no longer leaves the filter on “Ignored”, and the filter row slides so “Console” stays whole.


## [2.99.15] — 2026-11-13

**Sync: each device scrolls**

### Improved

- Home: in the Sync card, each device scrolls with its picture (PS5, NAS), like games in the other cards.

### Fixed

- Updates: a version installed but not yet active shows “Installed, active when the engine restarts” instead of “Up to date”.


## [2.99.14] — 2026-11-13

**Sync card: short title**

### Fixed

- Home: the Sync card with no game chosen shows “No game chosen” as its title and the list of devices below (the title is no longer cut off).


## [2.99.13] — 2026-11-13

**Home: a single Sync card**

### Improved

- Home › To do: one Sync card instead of one per device; games to bring up to date first, and devices with no game chosen grouped on one line.


## [2.99.12] — 2026-11-13

**Clearer Report a problem**

### Improved

- Report a problem: three cards of the same size (a bug, a question or an idea, a star), each with its code and what it opens; Cross enlarges the code; the versions to copy below.


## [2.99.11] — 2026-11-13

**PS5 Center, better presented**

### Improved

- Projects to fund: PS5 Center presented as the orchestrator of your PS5s (remote play, update, restart), its three highlights visible and the gauge rounded as on the web.


## [2.99.10] — 2026-11-13

**Community messages, PS5 Center to fund**

### New

- Support the project: “Community messages”, with the messages left on GitHub and a code to leave yours.
- Projects to fund: PS5 Center, goal $2,500, with its gauge.

### Improved

- Report a problem: QR codes for Discussions and “A star on GitHub”, as on the web page.


## [2.99.9] — 2026-11-13

**Tasks: act on the backup, tick, labels**

### New

- Tasks › Details: “See the backup” opens All versions, and “Move this backup to the trash” (hold Square 5 s) does the same as in Backups.
- Tasks: tick several (L2, a box on the web) to remove them from the list or move their backups to the trash.

### Improved

- Backups: with items ticked, Square offers to delete them (preview, then hold Square 5 s).
- Copies on a NAS show how it is reached: “NAS (FTP)”, “NAS (WebDAV)”.


## [2.99.8] — 2026-11-13

**Clearer Sync, PBC Backup folder recognised**

### Improved

- Sync: the text of the small bubbles (Active saves, Emulators, Apps, Settings, DLC) now fits inside its bubble.

### Fixed

- NAS: if the chosen folder is “PBC Backup” itself, backups go straight into it (no more “PBC Backup/PBC Backup”), and the same NAS added with another protocol is recognised.


## [2.99.7] — 2026-11-13

**Faster WebDAV, the same NAS counted once**

### Improved

- WebDAV NAS: much faster — one connection kept open for the whole backup instead of one per request, and folders already created are no longer asked for again.
- The same NAS added with two protocols (FTP and WebDAV, for example) is counted once: its backups no longer show up twice, in the summary or in Sync.
- Saves: the text now says this is true save sync (PS5 ↔ PS5, Switch ↔ emulator, Switch ↔ Switch).

### Fixed

- WebDAV NAS: at the root (where a Synology only accepts its shared folders), the folder is no longer assumed created; the test tells you to pick a shared folder.
- A backup moved to the trash on a NAS disappears from All versions right away.
- Devices: the protocol tag no longer overlaps the device name.


## [2.99.6] — 2026-11-13

**NAS over WebDAV**

### New

- Devices: a NAS can now be added over WebDAV (Synology, QNAP, Nextcloud…). In the form, Protocol offers FTP, SMB or WebDAV; the port follows on its own. The NAS's self-signed certificate is accepted once and remembered: if it changes, the connection is refused.

### Fixed

- Restore from the NAS: a full PS5 backup is no longer put back file by file in its original place (it goes through the guided restore); restoring it into a chosen folder is still possible.


## [2.99.5] — 2026-11-13

**Several games at once on the other PS5**

### New

- Other PS5's library: tick several items (L2 on the TV, a box on the web), then one action for all: back up their saves there, full backup there, or take the newer saves here.


## [2.99.4] — 2026-11-13

**Tasks: easier clearing**

### Improved

- Tasks: a visible Clear the list button, Clear all (Finished, Failed and Cancelled) and Options to remove a single task; nothing is ever removed from the disks.


## [2.99.3] — 2026-11-13

**Projects to fund, gifts and reports**

### New

- Settings › Projects to fund: one card per product (PS5 Center, PS5 Connector) with its highlights and a gauge in US dollars.
- Support the project: a Send a gift card (gift cards, subscriptions, test hardware).
- Settings › Report a problem: a QR code to the GitHub form and the versions to copy.


## [2.99.2] — 2026-11-13

**A little heart to support**

### New

- A heart at the top right opens Support the project: on the TV, R1 from Tasks then Cross; on the web, one click.

### Fixed

- QR codes always black on white, whatever the theme; full network names; the What's new line is no longer cut off.
- Settings › Updates and startup scrolls: the last options are reachable; Available versions shows the official site's list even when updates come from a local folder or a USB key.


## [2.99.1] — 2026-11-12

**Support the project**

### New

- Settings › Support the project: a thank-you note, one QR code per way to support (Ko-fi and Patreon soon; BTC, ETH, BNB and SOL with their network), addresses to copy on the web, and a thank-you wall with the nickname each supporter chose. The app stays free: nothing is locked.

### Improved

- The list of ways to support is read from our site at most once a day, only when the screen is open; the app keeps its own list if the site does not answer.


## [2.99.0] — 2026-11-12

**Same emulator progress**

### New

- Sync › Emulators card: ProsperoEden and Églantine saves stay in step between two PS5s, game by game and profile by profile (profiles matched by name). The most recent wins; the one it replaces is copied first; never while the emulator is open; never back and forth in a loop; if both moved on, you choose.

### Improved

- Each console only writes to its own folders: it fetches the most recent save from the other one, with the files' original times.


## [2.98.5] — 2026-11-12

**Beta channel**

### New

- Settings › Updates › Update channel: Stable (recommended, default) or Beta. On Beta, test versions arrive before stable ones, marked “beta” in Available versions; “Go back to the previous version” is still available.

### Improved

- A beta is never published as a GitHub release: Prospero Store and other catalogs only offer stable versions.


## [2.98.4] — 2026-11-12

**Start-up windows in the right order**

### Fixed

- After an update, "What's new" no longer hides the "Update installed" window and its countdown; the other PS5's card starts from its installed version.


## [2.98.3] — 2026-11-12

**Update already installed on the other PS5**

### Fixed

- Devices: when the new version is already installed on the other PS5 but not running yet, its card says so ("installed, active at next start") instead of offering to send it again.


## [2.98.2] — 2026-11-12

**Other PS5's library: touch-ups**

### Fixed

- Other PS5's library: the newer-progress badge is no longer cut off ("Newer here / there"); its saves without the game are shelved under "Saves without the game".


## [2.98.1] — 2026-11-12

**Lighter package**

### Improved

- Lighter package (−5 MB, 29 fewer files): only what the app uses, plus the required licences. Nothing changes in use.


## [2.98.0] — 2026-11-12

**The other PS5's library**

### New

- Library → “Console” chip (web: “Console” menu): view a paired PS5's library, with the same filters. Each game shows whether it's also here, missing here, or which side has the newer progress; “Missing here / Also here” filter. A banner reminds you that you're looking at the other console.
- Details of a game on the other PS5 (also from Home, “… · installed games” row): Get here, take or send the newer progress, back up its progress or make a full backup there, uninstall there. The work is done on the console that has the game; tracked in Tasks.
- No needless calls: the list comes from the engine's cache; “Rescan” queries the other PS5 once; an offline PS5 is never queried (last known list, actions unavailable).

### Fixed

- “Reopen” is no longer offered when the engine is older than the app.


## [2.97.0] — 2026-11-12

**Uninstall, here or remotely**

### New

- Uninstall a game: game page (web) or the library’s Options menu (TV) → “Uninstall from this PS5…”; keep the image (default) or delete it; TV: hold Square 5 s; web: type DESINSTALLER. Saves backed up first, never while playing.
- Remotely: the other PS5’s card → “Uninstall a game on …” (on the local network or over the Internet); the work and checks happen on the console that has the game. Tracked in Tasks (“Uninstall”).


## [2.96.1] — 2026-11-12

**Safer uninstall**

### Fixed

- Uninstall: if ShadowMountPlus is busy, the engine waits and retries; if a step fails, the game is put back in its list; the image is found even outside the list.


## [2.96.0] — 2026-11-12

**Uninstall a game (engine)**

### New

- The engine can uninstall a game from its PS5, including when the other PS5 asks: game closed, its saves backed up first, removed from ShadowMountPlus's list then uninstalled by it; its image or folder deleted only if asked. TV and web screens follow.


## [2.95.3] — 2026-11-12

**Compressed images on every PS5**

### Fixed

- Compressed image (.ffpfsc) that would not mount on a PS5 with an old system: the app forced a block size (64 KiB) on ShadowMountPlus that this system rejects. It no longer does; compressed images also mount on 3.21. The automatic switch to uncompressed from 2.95.2 is removed: it was not needed.


## [2.95.2] — 2026-11-12

**Games sent to an older PS5**

### Fixed

- Game sent to another PS5 in automatic format: if it has never mounted a compressed image but mounts uncompressed ones, it arrives uncompressed straight away. Before, the first send to a PS5 on an old system failed to mount.
- When an image replaces another one of the same game, the old one is now removed from ShadowMountPlus's list, even when its name has a space after the game number.


## [2.95.1] — 2026-11-12

**Engine restart offered, covers of old backups**

### Improved

- An update that is installed but not running yet opens an « Update X installed » window with a 15 s timer: without an answer, the engine restarts (never over a game or during a copy; « Later » to wait).

### Fixed

- After an update, the engine copy started with the console is replaced at once: a console restarted or crashed before the restart now comes back on the new version, not the old one.
- Old backup stored under the game's name only (e.g. « Split Fiction »): its game is found even when it isn't installed on this PS5, and the cover is back.


## [2.95.0] — 2026-11-12

**Files: delete**

### New

- Files: « Delete » the ticked items, with several confirmations (TV: the list, then hold Square 5 s; web: the list, « permanent », then type SUPPRIMER). Followed in Tasks. Never the system, games and saves managed by the console, installed apps, the engine folder or a disk root; not yet on the other PS5.

### Improved

- ProsperoEden's second folder (/data/eden) is now called « ProsperoEden (/data/eden) » instead of « Eden »: it is not a separate app.


## [2.94.0] — 2026-11-12

**Tasks: clear the list, details**

### New

- Tasks: « Clear the list » for finished, failed and cancelled tasks (one confirmation; only the list, nothing is removed from the disks). A Files copy whose copied files can still be removed stays in the list.
- Tasks: « Details » on a finished task: start, end, duration, average speed, and for Files what was copied, already there, renamed or failed.


## [2.93.1] — 2026-11-12

**No more overload when the other PS5 is off**

### Fixed

- When the other PS5 is off, sync now answers “offline” at once instead of waiting up to 3 minutes per request: the TV app's requests piled up until the console became unreachable (“server busy”).


## [2.93.0] — 2026-11-12

**Églantine (Switch 2)**

### New

- Églantine, the Switch 2 emulator: recognized (icon, game folder read from its settings, even on a USB drive), its .nup / .nsp / .xci games in the library under “Switch 2”, with their covers and saves; its settings in Settings › Sources and emulators.

### Fixed

- Églantine settings shared with the other PS5: only its settings file, never the connection to its save server.


## [2.92.4] — 2026-11-12

**Visible verification**

### Improved

- Files: while a copy is re-read to verify it, the TV and web now show “Verifying x %” with its own bar, instead of a bar stuck at 100 % for several minutes. Same when a file of the same size is already there and both are compared (“Comparing with the existing copy x %”).


## [2.92.3] — 2026-11-12

**Copy diagnostics**

### Improved

- The engine's status answer tells where a « Files » copy stands (step, file, since when): to understand a copy that does not finish.


## [2.92.2] — 2026-11-12

**Saturation diagnostics**

### Improved

- The engine's status answer lists the requests in progress (what, for how long): to find what fills the server when it answers « busy ».


## [2.92.1] — 2026-11-12

**Files: faster, resume, removal**

### New

- Files: each copy keeps a journal; after an interruption or an update it resumes without re-reading what is already done. A cancelled or failed copy offers « Remove the copied files… » (two confirmations): only what it created, never a file that was already there.

### Improved

- Files: reading and writing in parallel in 4 MB blocks (a copy from a NAS no longer waits for each network round trip: much faster on a gigabit network).
- Files (TV): a « Next: choose the destination » button under the list as soon as something is ticked.

### Fixed

- Updates: a file replaced by another of the same size within the same second could be taken as unchanged; the kept fingerprint is now forgotten on every replacement.


## [2.92.0] — 2026-11-12

**Files: copy or move anything**

### New

- Transfers › « Files » (TV and web): copy or move any files or folders from one place to another — disks of this PS5, of the other PS5, NAS, Switch. Nothing is ever overwritten (an identical file is skipped, a different one is kept alongside as « (2) »), every file is checked, and « Move » removes the originals only after a verified copy of everything. The engine's own folder stays out of reach.


## [2.91.1] — 2026-11-12

**Rest mode removed**

### Fixed

- Console power: « Put in rest mode » is removed (TV and web) and refused by the engine, because rest mode can crash a jailbroken console. Restart and turn off remain available.


## [2.91.0] — 2026-11-12

**Available versions**

### New

- Settings › Updates › « Available versions » (TV and web): the list of the latest versions from the source (date, title; installed, recommended or test). Pick one and it installs through the same checked path, newer or older; when going down, the version you left is no longer offered automatically.


## [2.90.9] — 2026-11-11

**Clean public package**

### Improved

- The published package no longer contains any path from the build machine nor the old 0.x engine, and the release notes only use example names.

### Fixed

- Automatic check: an unreadable update source no longer shows as an error (like an unreachable source).


## [2.90.8] — 2026-11-11

**Updates from an https site**

### New

- The update source can be an https address (GitHub, website) and a version repository: the address of latest.json leads by itself to the manifest of the latest version. Only changed files are downloaded, each one checked.

### Fixed

- Web: the version history showed the day before the real date in the Americas.


## [2.90.7] — 2026-11-11

**Go back to the previous version**

### New

- Settings › Updates › « Go back to version 2.x » (TV and web): if a version causes trouble, the previous one is reinstalled (files checked, reversible) and the engine restarts. The version you left is no longer offered or installed automatically; a manual check still shows it.


## [2.90.6] — 2026-11-11

**Engine always available**

### Fixed

- The engine's « I'm here » answer no longer copies the whole task list (about 750 KB) on each call, the game list no longer blocks the engine while it is built, and the answer catches the engine between two operations instead of replying « busy ». The app therefore sees an updated engine right away and offers to reopen.
- No more background sync computation with a device (other PS5, NAS) as long as no sync is set up with it: only when you open its page, result kept 5 min and redone as soon as a choice changes.


## [2.90.5] — 2026-11-11

**Slowness diagnostics**

### Improved

- The engine records any operation that blocks it for more than a quarter of a second (where and for how long), to find the cause of slowdowns.


## [2.90.4] — 2026-11-11

**Update download progress**

### New

- While an update downloads, the Home bubble and Settings show the percentage, the MB received, the current file (5 of 23), the speed and the time left (TV and web).

### Fixed

- Sync: only one computation at a time per device. With a well-filled NAS, preparing the saves sync took 2 minutes and was restarted in parallel over and over, until the engine was full (« server busy », beeps, reconnections). During a computation the app gets the latest result, and a long computation is no longer redone every 15 s.


## [2.90.3] — 2026-11-11

**No more disconnect loops**

### Fixed

- The TV app kept disconnecting and reconnecting, with the « The engine is not responding » window every 30 s: each time it came back it reloaded the whole catalogue, which slowed the engine and caused the next disconnection. It now reloads only what changed, considers itself disconnected only after 3 missed tries, and during an update or an engine restart it shows « Reconnecting… » for 30 s before offering a choice.


## [2.90.2] — 2026-11-11

**A busy engine is no longer « unreachable »**

### Fixed

- While the engine was preparing a sync, the TV app could think it was unreachable: it now answers « busy » in under a second, and the app keeps its display while waiting.


## [2.90.1] — 2026-11-11

**Lighter update check**

### Fixed

- During an update check the engine answered slowly and the TV app could show « The engine is not responding » when opening: the fingerprints of installed files are now kept from one check to the next.


## [2.90.0] — 2026-11-11

**Console power**

### New

- Settings › Updates › « Console power » (TV and web): restart, put in rest mode or turn off the PS5, after confirming; refused during a game or a copy. The engine does it itself (system functions), with no other app; a paired PS5 can also ask the other one.


## [2.89.9] — 2026-11-11

**Install automatically**

### New

- New « Install automatically » setting (Settings › Updates, TV and web; Off by default): an update that is found installs itself, never during a game, then the engine restarts when no copy is running.


## [2.89.8] — 2026-11-11

**Update announced on Home**

### New

- An update that is found now shows in an « UPDATE · 2.x available » bubble on Home (TV: Cross to install it; web: one click), and the PS5 shows a notification, once per version, even if the app is closed or a game is running.


## [2.89.7] — 2026-11-11

**Updates checked automatically**

### New

- Backup Center checks for an update when the engine starts, when the app opens (TV or web), when you go to Settings, and every hour. If the source doesn't answer, no error is shown.


## [2.89.6] — 2026-11-11

**Switch over SMB**

### New

- Add a Switch: choose FTP or SMB (TV and web). Over SMB, the engine reads the Switch's JKSV folder just like over FTP; the port changes from 5000 to 445.


## [2.89.5] — 2026-11-11

**Saves unmounted at startup**

### Fixed

- If the engine stopped abruptly while reading a save (crash, console switched off), the image left mounted is unmounted as soon as it starts, and its temporary copies are deleted.


## [2.89.4] — 2026-11-11

**The engine has its real name**

### Fixed

- When started by Payload Manager or the jailbreak's loader, the engine showed up under the generic name “payload.elf” in the process list: it now names itself “pbc-engine2.elf”.


## [2.89.3] — 2026-11-11

**Stable engine whoever launches it**

### Fixed

- The engine now picks its own thread stack size (512 KB): launched by Payload Manager or by its own restart, it got threads that were too small and crashed at startup.


## [2.89.2] — 2026-11-11

**Self-sufficient engine**

### Improved

- No more calls to Arsenal: the engine restarts and updates itself with its own launcher, and the TV app, only when no engine is running at opening, starts it through the jailbreak's loader (elfldr, etaHEN / kstuff).

### Fixed

- Restart through our built-in launcher: the new engine keeps its elevated privileges (it crashed at startup on some consoles).
- The engine regains its elevated privileges by itself as soon as it starts, whoever launches it (Payload Manager at power-on, the app, its own restart): it crashed when Payload Manager launched it.


## [2.89.1] — 2026-11-11

**A single engine during a restart**

### Fixed

- The TV app no longer starts a second engine while the engine restarts after an update (it only starts it by itself when opened, if it isn't running).


## [2.89.0] — 2026-11-11

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


## [2.88.11] — 2026-11-10

**Mirror updates trial**

### Improved

- Trial version: used to check sending, the « Reopen » window with its timer and fetching from the other PS5. No other change.


## [2.88.10] — 2026-11-10

**Reopen the app with a timer**

### Improved

- « Backup Center X is ready »: a 15 s bar; without « Later » or Circle, the app reopens by itself — only if it is in the foreground, never over a game.

### Fixed

- The Home bubble « UPDATE · X running » opens that same confirmation window; before, it started an invisible countdown that a Cross press cancelled (« Restart cancelled »).
- « Fetch X from… » answered « Unknown or unpaired PS5 »: the console introduced itself to the other one with the wrong identifier. The sending PS5 also recognises it by its address (consoles not yet updated can therefore fetch).


## [2.88.9] — 2026-11-10

**Update finish shown in green**

### Improved

- Devices: when an update finishes (sent or fetched), the badge turns green « Update: X running » for one minute, then disappears.


## [2.88.8] — 2026-11-10

**Mirror updates**

### New

- Devices: the older PS5 shows “Update available” on the other one’s card and can “Get” its version from it, just as the other can send it: it works both ways.
- After an update, if the open app is older than the engine, it offers to reopen (once per version, never automatically); a bubble on the Home screen lets you do it later.


## [2.88.7] — 2026-11-10

**Version badge only when useful**

### Improved

- Devices: another PS5's badge is centred above its name; it shows during an update, if it failed, if the new version isn't running yet or if the versions differ, and disappears when both PS5s are on the same version.


## [2.88.6] — 2026-11-10

**Readable update progress**

### Fixed

- Devices: another PS5's update progress is shown in a badge above its name; with three disks listed, it spilled out of the card.


## [2.88.5] — 2026-11-10

**Less memory**

### New

- On another PS5's card (Devices): « Restart the Backup Center engine » always offered, and « Close Backup Center » (only if it is the app open there, never a game).

### Fixed

- Computing app fingerprints (app sync) read each eboot.bin whole into memory: up to 780 MB used at once, which could make the system close a game (CE-15773-3). It now reads in small chunks.
- Installing a backport from a copy on another disk no longer loads each file whole into memory.


## [2.88.4] — 2026-11-10

**Memory diagnostics**

### Improved

- The engine logs the operation or request that makes its memory rise (more than 16 MB at once), to cut these peaks.


## [2.88.3] — 2026-11-10

**Restart after an update**

### Fixed

- A new engine now properly replaces an older one during an update (2.88.2 quit, thinking it was a duplicate).


## [2.88.2] — 2026-11-10

**No more duplicate engines (error CE-15773-3)**

### New

- Devices › other PS5: « Restart its engine » when a version is installed there but not yet running, and « Open Backup Center » on it (warning if a game is running).
- Home: an « Update » bubble during download and installation, then « 2.88.x installed · received from … » with the engine restart.

### Improved

- Updating one PS5 from the other also works with an older version (before 2.88): its source setting is put back as it was afterwards.

### Fixed

- Error CE-15773-3 (no game or app launches until a restart): duplicate engines stayed in memory. An engine restarted by the app no longer replaces the one that responds; only an update replaces it, and a stopped engine exits immediately.


## [2.88.1] — 2026-11-10

**Engine memory under watch**

### Fixed

- The engine decodes one cover at a time (less memory taken at once) and reports its memory in its status, to fix error CE-15773-3 (no game launches until a restart).
- Web › Devices: the “Update to …” button is readable, and progress shows on the other PS5's card.


## [2.88.0] — 2026-11-10

**Update a PS5 from the other one**

### New

- Devices › another, older PS5: « Update to … ». It downloads this PS5's version, verifies every file (size and checksum), keeps the old version if anything fails, then restarts its engine. Progress is shown on its card.

### Improved

- No more need for a computer to update a second console: one up-to-date PS5 is enough.


## [2.87.2] — 2026-11-10

**SMB form on the web**

### Fixed

- Web › Add a NAS: with SMB, the title says “Add an SMB share” and the FTPS encryption row goes away.


## [2.87.1] — 2026-11-10

**Readable device form**

### Fixed

- Devices › Add a NAS (TV): with the protocol choice, the buttons no longer cover the help at the bottom of the form.


## [2.87.0] — 2026-11-10

**NAS over SMB**

### New

- Devices › Add a NAS: choose FTP / FTPS or SMB (Windows, Synology, QNAP… shares). The first folder is the share (e.g. /Share/Backups). An SMB NAS does everything an FTP one does: saves, sync, depot, transfers.

### Improved

- Sync › NAS › Apps: “Not in the depot yet” instead of “Not installed on the NAS”.


## [2.86.0] — 2026-11-10

**The NAS as a depot: active progress and apps**

### New

- Sync › NAS › Active progress: the NAS keeps the most recent progress of each chosen game. Newer here: a copy goes to it; newer on the NAS: it is restored here, the one here backed up first. Your old “PS5 backup” folders count too.
- Sync › NAS › Apps: the NAS acts as an app depot. Newer here: it is put on the NAS; newer on the NAS: it is installed here, replacing the old one (permissions re-read, rolled back if the installation fails).

### Improved

- Same rule as between two PS5s: the most recent wins, never rolled back, and a log prevents endless back-and-forth.


## [2.85.7] — 2026-11-10

**Near-instant Sync**

### Improved

- Sync: the engine keeps each device's plans ready (saves, active progress, apps, DLC); cards and screens show up right away and refresh every 15 s. A choice or an upgrade triggers an immediate recalculation.


## [2.85.6] — 2026-11-10

**Sync: the cards finally fill in on the TV**

### Fixed

- Sync: on the console, answers over 8 KB (Active progress, Apps, DLC, Settings) reached the TV app truncated, so it stayed on “reading…”. The engine now waits for the app to read instead of giving up.


## [2.85.5] — 2026-11-10

**Sync diagnostics (answers)**

### Improved

- For each sync request from the TV app, the engine notes the answer it sent: code, size, duration.


## [2.85.4] — 2026-11-10

**Sync diagnostics (continued)**

### Improved

- The TV app reports to the engine any sync answer it can't use, with the reason.


## [2.85.3] — 2026-11-10

**Sync diagnostics**

### Improved

- The engine keeps track of the TV app's latest requests, to understand a Sync screen stuck on “reading…”.


## [2.85.2] — 2026-11-10

**Sync: no more endless “reading…”**

### Fixed

- Sync: if the other PS5 doesn't answer (restart, rest mode), the Active progress, Apps, DLC and Settings cards say “other PS5 unreachable” instead of “reading…”, and retry every 20 s.


## [2.85.1] — 2026-11-10

**Switch covers everywhere**

### Fixed

- Sync: a Switch game missing from this PS5 shows its cover (Eden's, from the other PS5, cached) instead of a text tile. Sync › DLC: the “Packages stored here” line no longer touches the edge.


## [2.85.0] — 2026-11-10

**Switch DLC: complete from one PS5 to the other**

### New

- Sync › DLC: Triangle completes Switch games: only the missing DLC and update are copied into the game's folder, here or on the other PS5. Nothing is overwritten or deleted, never while Eden is open, and free space is checked first.
- Sync › DLC: packages (.pkg) stored on your USB drives and download folders appear under their game, with their location. They will be used by the installer.

### Improved

- Each PS5 only writes to its own folders: the receiving one fetches the files, and the other only serves the packages it has declared itself.


## [2.84.0] — 2026-11-10

**Sync: DLC and updates inventory**

### New

- Sync › DLC: for each game, the add-ons and update on each side, and what’s missing where (↓ here, ↑ there). A game missing on one side points to Transfers. Read-only: nothing is copied.

### Improved

- The package storage location (USB drive on the console or NAS) is set: it will be used at the install step.


## [2.83.2] — 2026-11-10

**Home: the NAS picture**

### Fixed

- Home: a NAS Sync bubble now shows the NAS (no longer a PS5). Sync › NAS: the Saves card text is no longer cut off.


## [2.83.1] — 2026-11-10

**Sync: the right wording for the NAS**

### Fixed

- Sync › NAS: the Saves card now talks about the NAS (no longer “the other PS5”).


## [2.83.0] — 2026-11-10

**Sync: the NAS and settings**

### New

- Sync: the NAS appears alongside the PS5 and Switch consoles. Its Saves sync just like with another PS5: chosen games keep their saves on both sides, nothing is overwritten or deleted, and nothing is ever copied back and forth in a loop.
- Sync › Settings: the card opens on apps that have settings (“With settings” filter).


## [2.82.1] — 2026-11-09

**Apps: more patient comparison**

### Fixed

- Sync › Apps: the first comparison no longer fails while the other PS5 computes its apps’ fingerprints (up to 3 minutes).


## [2.82.0] — 2026-11-09

**Apps: settings and fingerprint**

### New

- Sync › Apps: for each app, “App only”, “App + settings” or “Settings only” (L2 on TV). Settings only move between the same version; the receiving console files them under its own profile, after a backup copy.
- Settings: profiles for Shadow Mount+, Prospero Themes, ProsperoEden, Eden, Porpoise and Breeze; for others, only small config files — never games, saves, caches, logs or console-specific data. Same log as Progress: never looping, to decide if changed on both sides.

### Improved

- Apps: same version but different files (rebuilt homebrew) → to decide, never silently replaced again.
- Apps: the header counts chosen apps (not games).


## [2.81.0] — 2026-11-09

**Sync: apps**

### New

- Sync › Apps: the same apps, at the same version, on both PS5s. Same version: nothing; newer on one side: it replaces the older one on the other side (same rule as app updates); missing: installed if you chose it.
- An app update never keeps two versions: the old one is removed only once the new one is installed, verified and its execute permissions re-read; otherwise it is put back.


## [2.80.2] — 2026-11-09

**Active progress: never in a loop**

### New

- Active progress: filters (chosen, to update, to decide, to the other PS5, to here).

### Improved

- Active progress: each sync is recorded on both sides. Progress that just arrived is no longer sent back in a loop; only the one you played again goes out. Played on both sides: nothing goes until you choose which one to keep.


## [2.80.1] — 2026-11-09

**Active progress: the same rule as resuming**

### New

- Active progress: choose the account on each side (by default, the same PSN account); the progress is put back on the user ID of the console that receives it.

### Improved

- Active progress: same rule as resuming a game — more play time first, then the newest; if identical, nothing is touched.


## [2.80.0] — 2026-11-09

**Sync: active progress**

### New

- Sync › Active progress: choose the games whose progress should be the same on both PS5s. For each one, the time the game wrote its progress is compared: the most recent wins and goes to the other console, on the right account.
- Before progress is replaced, it is backed up (Saves tab): nothing is lost. Never while the game is running, and always after you confirm.


## [2.79.4] — 2026-11-09

**Devices: the IP address of each paired PS5 is shown.**


## [2.79.3] — 2026-11-09

**Transferred apps: execute permissions**

### Improved

- Devices: the IP address of each paired PS5 is shown.
- The tab bar stays balanced when a tab no longer fits (the space is shared out).

### Fixed

- Apps and folder games transferred from one PS5 to another: eboot.bin and modules keep their execute permission (they arrived as 0666 and the app refused to start). Original permissions are recorded in the copy, restored at install and re-read before saying “installed”; otherwise, a clear error.


## [2.79.2] — 2026-11-09

**Sync: direction filters**

### Improved

- Sync: the device column scrolls when there are many (Switch, NAS…).

### Fixed

- Choose games: the “↑ To the other PS5” and “↓ To here” filters are back.


## [2.79.1] — 2026-11-09

**Sync: full names**

### Fixed

- Sync: section names (“Active progress”) are no longer cut off; “Soon” moves below.


## [2.79.0] — 2026-11-09

**Active progress and saves**

### New

- Sync: “Sync now” asks where to store saves when the other PS5 (or this one) has several drives.
- Sync: a “Whole games” card leads to Transfers (copy, compress, send a game).

### Improved

- One vocabulary everywhere: active progress is what's on the console; a save is a stored copy. The Backups tab is now called Saves, and the Sync section reminds you that saves aren't playable as is.

### Fixed

- A large save whose content can't be read (e.g. Gex) is no longer flagged “to check”.


## [2.78.5] — 2026-11-09

**Sync: no more grey in the light theme**

### Fixed

- Sync: the big top panel and the Backups card follow the theme (no more grey background in the light theme).


## [2.78.4] — 2026-11-09

**Sync: the section is called Backups**

### Improved

- Sync: the “Saves” section is now called “Backups” and says what it does: copy your backups to both PS5s, without touching the progress inside the games (that will be the Progress section).

### Fixed

- Home: the Sync bubble shows the PS5 picture without a grey background.


## [2.78.3] — 2026-11-09

**Sync: visible way back**

### Fixed

- Choose games: the help bar says Circle takes you back to the Sync page.


## [2.78.2] — 2026-11-09

**Distinct words**

### Fixed

- Translations: the Progress tab (saves on the console) and Saves (the copies) no longer share the same name.


## [2.78.1] — 2026-11-09

**Sync: web touch-up**

### Fixed

- Web: the Saves card on the Sync tab keeps its shape.


## [2.78.0] — 2026-11-09

**New Sync page**

### New

- Sync: a simpler page. Your devices on the left; at the top, where you stand with the other PS5 and a single “Sync now” button; below, separate sections: Saves, and soon Progress, Apps, DLC and Settings.
- Choose games: a switch per game, the date and play time on both sides, “Also add new games automatically”, choose all / remove all, and sending the whole game just once when it's missing on the other side.

### Improved

- The Sync bubble on Home speaks like the page (“No games chosen”, “N games to update”, “Everything is in sync”) and opens this PS5's page.


## [2.77.5] — 2026-11-09

**Sync: the game is sent only once**

### Improved

- Copy sync: if you turn it on, a followed title's full game is sent only once, when the other side lacks it; after that, only its saves follow (never 150 GB re-sent because a save changed).


## [2.77.4] — 2026-11-09

**Sync: saves only by default**

### Improved

- Copy sync: followed titles send only their saves; full copies only if you choose, and then only the newest (never older versions of a game or app).
- Sync tab: one sentence says exactly what the sync touches (and doesn’t), and the size to send is shown.


## [2.77.3] — 2026-11-09

**Sync: instant display**

### Fixed

- Sync tab: after “Follow everything” or “Follow nothing”, the card and summary change right away (before, you had to go back to Home).


## [2.77.2] — 2026-11-09

**Sync: follow everything or nothing**

### Improved

- Sync tab: the “Mode” button is replaced by “Follow everything” and “Follow nothing”, which start from scratch; the followed games list reminds “Options: all / none”.
- Home: the sync bubble shows the PS5 picture when no game is followed or everything is up to date.


## [2.77.1] — 2026-11-09

**Sync: touch-ups**

### Fixed

- Sync tab: the other PS5's card counts like the bubble (followed games only); buttons wrap instead of overflowing.


## [2.77.0] — 2026-11-09

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


## [2.76.3] — 2026-11-09

**Safer engine restart**

### Improved

- Update: the engine no longer goes silent if the old one is slow to free its place — it retries for 2 minutes and writes its status (engine-status.json); its port can no longer stay stuck because of an engine stopped abruptly.


## [2.76.2] — 2026-11-09

**Clearer top bar**

### Improved

- Top bar redrawn: tabs keep their spacing and scroll with L1 / R1, small ‹ › arrows show the hidden ones; the status dot and version sit on one line next to the search button.


## [2.76.1] — 2026-11-09

**Sync: status right away**

### Fixed

- Sync: the status with the other PS5 shows as soon as you open it (it used to wait a minute).


## [2.76.0] — 2026-11-09

**New Sync tab**

### New

- “Sync” tab (between Backups and Transfers): one card per device — each other paired PS5 and each Switch. For a PS5: “Copies · Backups” (sync, everything with full games, or tick the games), “Saves” (coming soon), history. For a Switch: “With Eden on this PS5” (the existing sync) and “With another Switch” (coming soon).

### Improved

- The tab bar tightens by itself when space runs short.
- Cards for another PS5 (Sync, Backups, Saves) show the PS5 picture.


## [2.75.0] — 2026-11-09

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


## [2.74.0] — 2026-11-09

**Home: here, then elsewhere**

### New

- Home: “PS5-Salon · backups” row — every copy stored on the other PS5, even for games that are also here; Cross opens their versions.

### Improved

- Home in two parts: first what is on this console (recently played, PS5 games, PS4 games, saves without the game), then what is elsewhere, with a badge (available backups, the other PS5's installed games and backups), then retro and apps.


## [2.73.0] — 2026-11-09

**Home: the other PS5's games**

### New

- Home: one “On PS5-Salon” row per paired PS5 — its installed games that aren't here; Cross: receive them here (drive, format, account), the other PS5 sends them and they get installed.

### Improved

- Home: “PS5 games” and “PS4 games” in two rows.


## [2.72.0] — 2026-11-09

**Backups: one card per other PS5**

### New

- Backups: one card per paired PS5 at the top (“PS5-Salon · other PS5 · N copy(ies)”) to see at a glance everything stored there, even for games that are also here.

### Fixed

- Web: the other PS5's drives no longer show up as “missing drives”.


## [2.71.1] — 2026-11-09

**Home: saves without the game moved up**

### Improved

- Home: “Saves without the game” now comes right after games, before retro consoles and apps.


## [2.71.0] — 2026-11-09

**Home: available backups**

### New

- Home: “Available backups” row — games with copies somewhere (here, on the NAS or on the other PS5) that are not on this console; Cross opens their versions to restore or retrieve.

### Improved

- Copy on a NAS this console doesn't have: the message suggests adding it in Devices to manage it from here.

### Fixed

- Saves and Backups: PS4 games (CUSA) missing from the library were labeled “PS5”; they now show “PS4” and their cover.


## [2.70.1] — 2026-11-09

**Copies from the other PS5: covers and labels**

### Improved

- A game stored only on the other PS5 now says “N copy(ies) stored on …: can be retrieved here” and no longer offers “Delete…”.

### Fixed

- Covers: a console without an official icon (offline) uses the one from a paired PS5 that has the game.


## [2.70.0] — 2026-11-09

**Backups: copies from the other PS5 too**

### New

- Backups also lists copies stored on paired PS5s (their drives, their NAS), labeled “PS5-Salon · USB 0”: wherever they are stored.
- On a save copy stored on the other PS5: “Retrieve here” — it is sent to this console and restored to the chosen account.

### Improved

- Copies from the other PS5 are never checked, suggested for cleanup or deleted from here: that's done from the console that stores them.


## [2.69.0] — 2026-11-09

**Delete: hold for 5 seconds**

### New

- Last step when deleting a console save: hold Square for 5 seconds (TV) or the button for 5 seconds (web), a bar fills up; releasing or Circle cancels.

### Improved

- Clear label everywhere: “Delete the console's save…”, and every confirmation reminds you it's the save on the console (your progress), not a copy in Backups.


## [2.68.0] — 2026-11-09

**Clean up a save from anywhere**

### New

- “Delete save…” also in Saves and Backups (“Delete the console's save…”, copies stay).
- Without an up-to-date copy, deleting is still possible by forcing it: two confirmations, “Cancel” preselected on the last screen; a raw safety copy is always made.

### Fixed

- Covers: a game missing from the console (only its save or its copies) shows its official icon everywhere (Home “To do”, Saves, Backups).


## [2.67.0] — 2026-11-09

**Delete a save from the console**

### New

- Details, Home and Library: “Delete save…” removes a game's save from the console, after confirmation — only if an up-to-date copy is in Backups (otherwise: back it up first), and a safety copy is made just before.

### Improved

- Saves and Backups: copies made on another console by the same PSN account are attached to this console's account (“up to date” status, direct restore).

### Fixed

- Backups: covers were missing for a game present only through its save.


## [2.66.0] — 2026-11-09

**Backups visible on every console**

### New

- Home: Options button (TV) or “⋯” (web) on a cover: Open details, All versions, Ignore… — for example to set aside a save whose game is gone.

### Improved

- Backups: copies made on another console by the same PSN account now show up (accounts are recognized automatically, no ID to enter); a copy from another account stays visible, marked, and is never offered for cleanup.
- Backups: full backups stored on internal storage are listed too.
- Game details: Up and Down move between rows of buttons, to the nearest button, without skipping any.


## [2.65.1] — 2026-11-09

**A stuck disk no longer blocks everything**

### Improved

- Image status and compatibility: one read at a time; if a read hangs (stuck disk or mount), later requests get the last known result instead of piling up — the engine stays reachable.


## [2.65.0] — 2026-11-09

**The engine no longer reads inside mounts**

### Improved

- To know whether a game is mounted, the engine reads the system mount table without waiting, instead of looking inside ShadowMountPlus mount folders: a broken mount can no longer block it.


## [2.64.1] — 2026-11-09

**Move without touching the game’s registration**

### Fixed

- Image tools › Move: the ShadowMountPlus registration is updated only when moving the registered game itself; moving a working copy no longer replaces the real game’s registration.


## [2.64.0] — 2026-11-09

**The backport travels with the game**

### New

- A game’s ShadowMountPlus backport (backports/<ID> folder) becomes part of the game: copied with it, sent with it to another PS5, and put back in place on install (never overwritten if one already exists).


## [2.63.2] — 2026-11-09

**Engine: always answers, even when stuck**

### Improved

- Engine: its status always answers, even if an operation gets stuck (it then says “busy”, since when and in which operation); a watchdog writes it to the log to diagnose a stuck disk.


## [2.63.1] — 2026-11-09

**Browse the other PS5’s disks**

### New

- Devices › other PS5 › Disks: each disk opens in the file browser (read-only); on the web, click the disk name on the card.


## [2.63.0] — 2026-11-09

**Other PS5 menu completed**

### New

- Devices › a paired PS5’s card: “Disks” (list with free space), “Saves” (its saves, read-only), “Send a game…”, “Send a save…”, “Close the open app…”, “Forget this console…”.


## [2.62.0] — 2026-11-09

**Close the open app on the other PS5**

### New

- After a send, Tasks offers “Close “…” there”; in Devices, the other PS5’s card offers “Close the open app…” at any time. Named confirmation; only the confirmed app is closed; a game is flagged (unsaved progress lost).


## [2.61.2] — 2026-11-09

**Instant install on the same disk**

### Improved

- Send a game to a PS5, as is: after its verified arrival, it is moved into the games folder with a simple rename (instant) instead of being copied again — a single copy, the network one.


## [2.61.1] — 2026-11-09

**Folder games: installable from a copy**

### New

- Send a folder-format game to a PS5 (or install it from a copy): as is, the folder goes to the games folder; as .exfat, an image is created; as .ffpfsc, it is compressed — then registered in ShadowMountPlus.


## [2.61.0] — 2026-11-09

**No duplicate after a send**

### New

- Send a game to a PS5: once it is installed in the other console’s games folder and its save restored, the transit copy is removed from “PBC Backup” — the game is there only once. Option to also keep a backup copy; if a step fails, the copy is kept.


## [2.60.2] — 2026-11-09

**Reliable speed test**

### Fixed

- Read speed test: a single full read of the game (re-reading the same files measured memory, not the disk); the result shows in Tasks, with a warning if the game is too small for a reliable measure.


## [2.60.1] — 2026-11-09

**Usage bars on the other PS5’s card**

### Improved

- Devices: on a paired PS5’s card, each disk has its own small usage bar, with its free space.


## [2.60.0] — 2026-11-09

**Accounts asked, one card per console**

### New

- Send a game to a PS5 or install it from a copy: you also choose the account that receives its save, as for a save on its own — both ways.

### Improved

- Devices: one card per paired PS5, its name as title and the list of its disks (Internal, USB 0, USB 1…) with their free space.


## [2.59.2] — 2026-11-09

**Console-to-console sends: correct status**

### Fixed

- Send to a PS5: a step accepted by the other console (a restore or install starting) is no longer counted as an error; if refused, the other console’s message is shown.


## [2.59.1] — 2026-11-09

**The console as title**

### Fixed

- Devices: a paired PS5’s disk card shows the console name as title, with “Internal · system 3.21” below.


## [2.59.0] — 2026-11-09

**Send a save to the other PS5**

### New

- Saves › Options and game sheet › “Send to a PS5…”: send just the current save, choosing the console then the account that receives it; it is restored there, re-signed if the ID differs.


## [2.58.0] — 2026-11-09

**The other PS5 visible on both sides**

### Fixed

- Devices: when a paired PS5’s disks appear (it powers on, or the app was already open), the TV app and the web show them right away — before, an app opened too early never saw them.


## [2.57.1] — 2026-11-09

**Readable device names**

### Fixed

- Devices: a device name drops to a smaller size instead of being cut; a paired PS5 shows its disk as title with “console (system)” below.


## [2.57.0] — 2026-11-09

**What each tool is for**

### New

- Image tools: each menu choice says in one line what it does (verify, repair, uncompress, read-only, speed, AMPR index…), on the TV and on the web.


## [2.56.0] — 2026-11-09

**Devices up to date at startup, theme right away**

### New

- Game sheet: games using APR-EMU (replacement libSceAmpr) are flagged, with their ampr_emu.index.

### Improved

- Devices: a paired PS5’s disks appear as soon as it is online (at startup or when switched back on), without waiting 30 s.
- TV: the chosen theme shows as soon as the app opens, even if the engine is slow to answer at console startup.


## [2.55.0] — 2026-11-09

**Image tools in one button**

### New

- Game sheet: an “Image tools…” button opens a menu (verify, repair, uncompress, read-only, read speed test, copy or move, AMPR index, browse) — on the TV (as well as Triangle) and on the web.


## [2.54.1] — 2026-11-09

**Menus at the right width**

### Fixed

- TV: a menu widens to fit its title; no title is cut anymore.


## [2.54.0] — 2026-11-09

**Each console’s system in plain sight**

### New

- Game sheet: its finding for this console and for each paired PS5, with their system (“PS5-Salon (3.21)”). If the game needs a newer system: “Backport needed: available” when a backport exists, otherwise “May not work”, with PS5 Center to help.

### Improved

- Send to a PS5: each disk of the other console shows its system; sheet texts shortened so nothing overflows.


## [2.53.0] — 2026-11-09

**The game’s backport, read and shown**

### New

- Game sheet and pre-send warning: the available backport is read and shown — the game’s fakelib libraries (with their SDK), ShadowMountPlus backports on the console, and the target console’s fakelib backport setting.


## [2.52.1] — 2026-11-09

**SDK also read from signed executables**

### Improved

- Compatibility: the SDK is also read from SELF-format executables when their segments are in clear (prepared games), without changing anything.


## [2.52.0] — 2026-11-09

**A game’s compatibility, with its evidence**

### New

- Game sheet: “Compatibility” line for this console — game version, SDK read from its executables (even inside an .exfat or .ffpfsc image, without mounting it), declared minimum, and a state: system recent enough, patch to verify or not enough information.
- Before sending or installing a game: if the console is below the declared system, the warning shows the findings and their evidence (executables’ SDK, game libraries, backport present) instead of concluding.

### Improved

- Automatic format: chosen from the mounts actually seen on the receiving console (ShadowMountPlus log), no longer from a system number.


## [2.51.0] — 2026-11-09

**Warned if the system is too old**

### New

- Before sending a game to a PS5 or installing it from a copy: if the game needs a newer system than the console’s (for example 11.20 on a console on 3.21), a warning says so before going on.

### Improved

- Tasks: a finished install says when the game will appear on the home screen once the app is closed.


## [2.50.0] — 2026-11-09

**Choose the format of a sent game**

### New

- Send to a PS5 or install from a copy: choose the format — automatic (based on the console’s system), compressed (.ffpfsc), uncompressed image (.exfat) or as is. In automatic mode, an older console gets an uncompressed image it can mount.


## [2.49.2] — 2026-11-09

**ShadowMountPlus notified directly**

### Improved

- Installing a game: ShadowMountPlus is notified through its API (as well as manual.lst); if it waits until no game or app is running, the task and the message say so.


## [2.49.1] — 2026-11-09

**The installed game shows up**

### Fixed

- Installing a received game or a copy: the copied image is now registered in ShadowMountPlus (the game appears on the home screen); if the same image is already on the disk, it is not copied again.


## [2.49.0] — 2026-11-09

**A game from one PS5 to another, installed**

### New

- Game sheet › “Send to a PS5…”: pick a disk on the other PS5; the game is copied and verified there, then installed (ShadowMountPlus) with its save re-signed. Everything is tracked in Tasks.
- All versions › a full copy › “Install on …”: puts the game back on the chosen disk of this console (internal or USB) and restores its re-signed save.

### Improved

- Game sheet: buttons wrap to a new line when space runs out.


## [2.48.1] — 2026-11-09

**The other PS5's drives, like here**

### New

- Devices: each drive of a paired PS5 has its own card, like a drive of this console (name, space bar, free space out of total), on both sides; when it's off, a single “Offline” card.


## [2.48.0] — 2026-11-09

**The other PS5 as a destination**

### New

- A paired PS5's disks become destinations just like the NAS (“PS5-149 · USB 1”): from a game's page (“Where to back up?”), from Transfers (games, apps, saves, with verification) and for the PS5 full backup. Copies land in the other console's “PBC Backup”.

### Improved

- A paired PS5 is shown “online” as soon as it responds, even without an announcement received on the network.


## [2.47.0] — 2026-11-09

**Two PS5 consoles working together**

### New

- One code is enough: when a PS5 pairs with another, both trust each other both ways.
- Devices: each paired PS5 has its card (real name, online or offline, its drives, forget), on the TV and on the web.
- Sends to another PS5 are tasks: “To do” bubble and Tasks tab, with progress.
- “How does it work?” in Devices › PlayStation 5: the three steps to link two consoles.

### Fixed

- Engine: two loops walked through a temporary list that was already destroyed (possible crash); fixed and covered by tests.


## [2.46.3] — 2026-11-09

**Another PS5: clearer menu**

### Fixed

- Another PS5: one line per paired console (“paired ✓, online”), and its actions in a submenu (See its drives, Forget this console… with confirmation). “Forget” no longer looks like an instruction.


## [2.46.2] — 2026-11-09

**Link: lasting pairing, real console name**

### Improved

- Each console announces itself under its real name (the one in the PS5 Settings, as in Remote Play) instead of the first account's name.

### Fixed

- Link between consoles: pairing is kept under the other console's stable ID (it survives its restarts); before, it could be lost and the console appear twice.


## [2.46.1] — 2026-11-09

**Updates without settings**

### Improved

- Updates: nothing to set. Without a chosen source, the app finds its updates by itself: an online source built into the package when there is one, otherwise a “PBC Update” folder on a USB drive, otherwise the folder dropped on the console.


## [2.46.0] — 2026-11-08

**Send a save to another PS5**

### New

- All versions: “Send to <paired PS5>…” sends the copy to the other console (fingerprint checked), which restores it re-signed for its account, with a safety copy first. Works from a drive or the NAS.


## [2.45.1] — 2026-11-08

**Another PS5**

### New

- Devices › PlayStation 5: pair another PS5 running Backup Center — those seen on the network, or by IP address — with the 6-digit code it shows; this console's code is shown for the other one. A paired console shows its drives and free space, and can be forgotten.

### Fixed

- Engine: the link between consoles no longer re-reads the console's accounts every 5 seconds from its own thread (likely cause of an engine stop); the console name comes from the scan loop.


## [2.44.1] — 2026-11-08

**Link: more reliable announce**

### Fixed

- Link between consoles: the announce is also sent to the broadcast address of each of the console's networks, so another PS5 on the same network sees it.


## [2.44.0] — 2026-11-08

**Link between consoles**

### New

- Engine: link between consoles — each Backup Center announces itself on the local network, pairs with another console using its 6-digit code and can read its status, drives, library and saves. Reusable module (also planned for PS5 Center).


## [2.43.1] — 2026-11-08

**Reading the console's accounts**

### Improved

- Engine: read-only view of the account slots in the console registry (name, ID, type, activation), to prepare reactivating an ID. Nothing is changed.


## [2.43.0] — 2026-11-08

**Resume all on my account**

### New

- Settings › Users: “Resume all on …” for an ID that is not on the console. For each game, the console save is compared with the best copy from that ID; the most advanced one is checked (L2 to change), and each chosen game is restored and re-signed for the console account. The current save is copied first, nothing is deleted, and a running game is left out.


## [2.42.4] — 2026-11-08

**Settings › Users**

### Improved

- Settings: the “Accounts” section is now called “Users”, like on the PS5; you link an ID to the same user.


## [2.42.3] — 2026-11-08

**Delete a copy, choose the account**

### Improved

- Restore: if there are several PS5 accounts on the console, the app asks which account to restore to.

### Fixed

- All versions: Square deletes the copy under the cursor (or the checked copies), with confirmation, without going through a menu; the button is shown at the bottom of the screen.
- IDs: a copy made under an ID that is not on this console is marked “Other ID”, with “not on this console”; Settings › Accounts says “On this console” or “Not on this console”. No more guessed names.


## [2.42.2] — 2026-11-08

**Duplicates and Devices: fixes**

### Fixed

- Backups: R2 cleans safe duplicates again (checked games, otherwise all) with the preview before the trash; also in the Options menu, along with “Compare all”. In a game's detail, R2 cleans that game.
- Devices (TV): cards adapt to the number of devices; the “Add a device” row no longer goes under the hints at the bottom.


## [2.42.1] — 2026-11-08

**Home and Library: fixes**

### Fixed

- Home: the category rows (games, one row per retro console, apps, saves without the game) are back under “Recently played”, where you open an item to back it up.
- Library (TV): each category is shown as a grid of 8 per row; Down goes further down the list (for example all apps), instead of a single row to scroll.


## [2.42.0] — 2026-11-08

**Wider generic reader**

### New

- Wider generic progress reader: Unreal durations (Timespan), Blueprint struct fields (“CurrentLevel_49_…”), and a “detected automatically” note when progress comes from this reader.

### Improved

- Survey: on each comparison, useful field names (time, level, chapter, map…) from saves of unknown games are noted, to pick the next games for the catalogue.


## [2.41.0] — 2026-11-08

**Generic progress reader**

### New

- Generic progress reader: for every game not in the catalogue, play time, level, chapter and place are looked up under their common names in Unreal Engine and JSON saves. The catalogue keeps priority for the games it knows.

### Improved

- Copies already compared with the old reader go back to “Not yet compared”: “Compare all” reads them once more to get their progress. Safe duplicates are still offered meanwhile.


## [2.40.0] — 2026-11-08

**Progress: readable places, Little Nightmares, settings only**

### New

- Progress read from the save: Little Nightmares Enhanced Edition (place, including the expansion chapter); places are readable (“Spring Meadows” instead of “Level_SpringMeadows_Main_V2”), also for copies already compared.

### Improved

- A copy that only holds the game's settings, with no playthrough (e.g. Hades), is marked “settings only, no playthrough”.

### Fixed

- Engine: an old ID's name in All versions is now read safely (2.39.0 bug caught by the tests).


## [2.39.0] — 2026-11-08

**Settings › Accounts**

### New

- Settings › Accounts: every ID seen in your backups, with its name, copies, games and dates. You can rename it and link it to the same player (or unlink it); an ID with the same save as yours is flagged “Probably you”.

### Improved

- All versions: copies from an old ID show the name you gave it.


## [2.38.0] — 2026-11-08

**Resume on my account**

### New

- “Resume on my account”: a copy made under an old ID (before a format, on another console) is marked “Old ID”, with that ID's name when known; the action re-signs it for your current account (checked first, your current save copied beforehand, never while the game is running).


## [2.37.4] — 2026-11-08

**One color per task family**

### Improved

- Task bubbles: one color per family — backup and restore in purple, transfer and sync in teal, compression and images in pink.


## [2.37.3] — 2026-11-08

**Bubble colors**

### Fixed

- “To do” bubble colors made consistent: each kind has its color (to back up orange, duplicates green, not yet compared blue, to check red, tasks purple), applied to the edge, background, title and bar, on the TV and the web, in dark and light themes.


## [2.37.2] — 2026-11-08

**Time left in the bubbles**

### Improved

- Home: task bubbles (full game backup, compression, transfer…) show the percentage and time left; covers no longer flicker on the web.


## [2.37.1] — 2026-11-08

**Tasks in “To do”**

### Improved

- Home: one bubble per kind of running task (compression, transfer, decompression…), with the progress bar of the task shown.


## [2.37.0] — 2026-11-08

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


## [2.36.1] — 2026-11-08

**Players and identifiers**

### Improved

- Engine: the account identifiers seen in your saves are listed (dates, games), and the ones that are clearly you (the same playthrough under two identifiers) are flagged; they can be given a name. The screens come with the new layout.


## [2.36.0] — 2026-11-08

**Select all, delete, ignore**

### New

- Manage backups (TV and web): everything can be selected and sent to the trash — a whole item or copy by copy, even without a comparison — including full backups of games, apps, homebrew and ROMs. “By disk” can also move a copy to the trash.
- “Stop backing up…” on the page of any item (game, app, homebrew, ROM) and multiple selection in the Library (“Ignored” view); ignored items no longer appear on Home.


## [2.35.1] — 2026-11-08

**Old account identifiers**

### Improved

- Manage backups: an old identifier of your account (for example from before a console format) can be declared as yours; its copies are then treated as yours (duplicates offered for cleanup).


## [2.35.0] — 2026-11-08

**Ignore a game, faster comparison**

### New

- “Ignore this game” (Backups, TV and web): the game is no longer backed up or shown, and its copies can go to the trash on all disks and the NAS in one go; the “Ignored games” list in Settings lets you undo it. “Check all / none” in a game's details.

### Improved

- Faster comparison: a copy that is byte-for-byte identical to one already read is no longer opened. An old save without a game ID (e.g. “Split Fiction” on the NAS) is linked to the right game instead of creating a second entry.
- Comparison: backups made by the app are no longer copied or downloaded when their content has already been read (each file's fingerprint is in their manifest); temporary folders of an interrupted comparison are emptied at startup.


## [2.34.0] — 2026-11-08

**New backup management**

### New

- New backup management (TV and web): a clear summary per game (copies, contents, duplicates, progress), a cleanup proposal — one copy per different content and the most recent one on each disk —, a preview before cleaning up, “Compare all”, and the 30-day trash with “Put back”.

### Improved

- Copies from another account and protected copies are never offered for cleanup.


## [2.33.0] — 2026-11-08

**Progress of every copy**

### New

- Progress of copies: each new backup records the game's progress at the time of the copy; “Compare all” reads existing copies once (disks and NAS), never opening the save of the game being played.


## [2.32.1] — 2026-11-08

**More accurate progress**

### Improved

- Game progress: the actual playthrough comes before profile or settings saves, and the game's name is no longer repeated before the text.


## [2.32.0] — 2026-11-08

**Game progress and trash**

### New

- Saves: next to each game, the progress the game writes in its save (mission, percentage, level, time) and the play time counted by the app from now on. Comparing copies uses it to tell which one goes furthest.
- Manage backups: deleting moves copies to a trash (“PBC Backup/Corbeille”, same disk or NAS) for 30 days, from where they can be put back.


## [2.31.1] — 2026-11-08

**Web language protected**

### Fixed

- The web page language can no longer be changed by mistake by a test of the app run on a computer; the TV's release notes stay to be seen on the TV.


## [2.31.0] — 2026-11-08

**TV and web aligned**

### Improved

- Saves: the 6 views are on the TV as on the web (“Backup newer” was missing on the TV), same names and order. Web: the language follows the one chosen on the TV, spinner while the content is read, no more “Restore” on an incomplete version or for a non-PS5 account, USB disks silently re-read after a minute.


## [2.30.9] — 2026-11-08

**By disk on the web**

### Improved

- Web › Saves › By disk: like on the TV, covers for old backups, list kept for each disk, Refresh button and number of versions.


## [2.30.7] — 2026-11-08

**Square everywhere**

### Improved

- Saves: Square changes the view even when the cursor is on the left column (accounts, By disk, Sync), without moving right first. In the Library, Square also works from the top row.


## [2.30.6] — 2026-11-08

**Clearer hints**

### Improved

- Clearer on-screen hints: Square says what it shows next (“Show: On the console”, “Go to: USB 1”) instead of “or Up: filters”. In the Library, Square changes category (All, PS5, PS4, Applications, ROM).


## [2.30.5] — 2026-11-08

**Handier By disk**

### Improved

- Saves › By disk: each disk keeps its list when switching between them (Triangle to refresh), spinner while the NAS is read, scrolling through every version and covers for old backups.

### Fixed

- Transfers: the “Full PS5 backup” and “Transfers in progress” cards are no longer cut off.


## [2.30.4] — 2026-11-08

**More memory**

### Improved

- The TV app now has a much larger memory reserve (up to 96 MB instead of about 13 MB): no more stops when lists grow. A memory reading is written to the log every 30 seconds.
- Saves › By disk: the hint says “or Up: filters”, like everywhere else.


## [2.30.2] — 2026-11-08

**Magnifier and Square**

### Improved

- The magnifier can be selected with L1 / R1, between Tasks and Settings: Cross opens search.
- Saves › Sync: Square (filters) stays shown even when the Switch doesn't answer.


## [2.30.1] — 2026-11-08

**Airier Transfers**

### Improved

- Transfers: airier page, FROM, TO and WHAT TO DO? spaced out a little more.


## [2.30.0] — 2026-11-08

**Vietnamese**

### New

- New language: Vietnamese (Tiếng Việt), on the TV and the web, chosen automatically when the console is set to Vietnamese.

### Improved

- “Hold Circle 2 s: quit” is now shown at the bottom right of every view (before: Library only).
- The magnifier at the top right no longer has the small triangle under it.


## [2.29.5] — 2026-11-08

**17 languages**

### Fixed

- The TV app launches again: it now reads only the displayed language, and the task list it receives no longer includes the large content-comparison reports (the app's memory is limited to 13 MB).
- The console language is detected again when the TV app starts (on a French console, the app was showing English).


## [2.29.1] — 2026-11-08

**17 languages**

### Improved

- “Manage backups”, the release notes and the language list (translated name alongside: “Русский · Russian”) follow the chosen language.

### Fixed

- The “engine not responding” message no longer appears while measuring sizes on the NAS or comparing contents.


## [2.29.0] — 2026-11-08

**17 languages**

### New

- The app speaks 17 languages, on the TV and the web: French, French (Canada), English, Spanish (Spain and Latin America), Portuguese (Brazil and Portugal), German, Italian, Dutch, Polish, Turkish, Russian, Simplified and Traditional Chinese, Japanese, Korean and Arabic. Defaults to the console language; change it in Settings › General.

### Improved

- Only one engine at a time: a new engine waits until the old one has really stopped (otherwise it doesn't start), and stopping takes at most 20 seconds, even if an operation is dragging on.
- Save images mounted by the app are unmounted on shutdown, and the app uses its own mount folders (no more conflicts with Garlic SaveMgr if it's running too).
- /status shows the engine's PID, instance, role and current task.


## [2.28.0] — 2026-11-08

**Themes, sounds and world scripts**

### New

- 8 themes to choose from (Settings › General), on the TV and the web.
- The TV can display Chinese, Japanese, Korean, Russian, Greek and Arabic (joined letters, right to left) — ready for the translations.
- Sound is back: tile music, interface sounds (move, confirm, back, confirmation, error, task done) and background music (off by default). Settings: sounds, music and volume in Settings › General, on the TV and the web.

### Fixed

- The engine no longer gets stuck on “server busy”: only one NAS read at a time (other requests get the latest list), and a silent connection no longer holds its slot.


## [2.27.1] — 2026-11-08

**Manage backups**

### New

- Saves › Manage backups (TV and web): for each game, every version on the disks and the NAS — date and time, location, size, “old”, protected, and what their content says (same letter = identical content, progress). Actions: refresh, compare content, tick duplicates (one copy of each content is kept), keep the last 5, protect, delete (with confirmation, never a protected version), restore.
- The NAS appears in Saves › By disk, next to the USB drives.


## [2.26.0] — 2026-11-08

**What's inside a save**

### New

- Compare versions of a PS5 save by their content, not just their date: progress written by the game, play time when the game reports it, save time, and a fingerprint to spot identical copies. Console, disks and NAS, read-only on copies.

### Improved

- Old saves stored by game on a USB drive are read and can be restored, just like on the NAS.


## [2.25.0] — 2026-11-08

**Clearer saves**

### Improved

- Games that only exist in a backup show their real name and official icon (Indiana Jones, Split Fiction, games shown by their number).
- Versions: date and time; “old” for those made by script; “size?” instead of “0 B” when the size isn't known yet.

### Fixed

- Saves: Cross on a game opens a menu (Back up now / Restore a version) instead of starting a backup on every press.


## [2.24.1] — 2026-11-08

**Bring a save back from the NAS**

### Fixed

- “Newer in a backup” compares the time of the game save inside the backup (not the time of the copy): a recent copy of the same save is no longer seen as newer.


## [2.24.0] — 2026-11-08

**Bring a save back from the NAS**

### New

- Restore a PS5 save straight from the NAS (including old ones made by script: images only): the app downloads the chosen version and puts it back on the account, with the same safety (checked first, safety copy, game closed).
- TV: Saves › Cross on a game → Back up now, or Restore a version (date and location, NAS included), checked before it's put back.

### Improved

- Console ↔ NAS comparison down to the minute (NAS file times); new “Newer in a backup” status and filter.
- Same buttons for every device (Switch included): Save, Test, Tidy under PBC Backup, Forget.


## [2.23.1] — 2026-11-08

**Everything under PBC Backup**

### Improved

- NAS saves are recognized even when the folder name has extras (“(2025) (PPSA17599) (US) v1.400”) or no “[PS5]” (“Split Fiction”); “_…” folders are ignored.


## [2.23.0] — 2026-11-08

**Everything under PBC Backup**

### New

- A single “PBC Backup” folder on the NAS, sorted by platform underneath (PBC Backup/PS5 backup/[PS5] Name (ID)/saves/<date>, PBC Backup/Switch backup/…).
- “Tidy under PBC Backup” (NAS settings, TV and web): moves your “PS5 backup”, “Switch backup”… folders into “PBC Backup” by simply renaming them — nothing is copied or deleted; if a folder exists on both sides, it's merged date by date without overwriting anything.

### Improved

- The NAS shows all its saves, inside and outside “PBC Backup”, during and after tidying.


## [2.22.4] — 2026-11-08

**See everything on the NAS**

### Improved

- A NAS shows every save under its folder, whatever the layout: “by platform” ones (PS5 backup/[PS5] Name (ID)/saves/<date>, including those made outside the app) and those in the “PBC Backup” folder. The layout only decides where new ones go.


## [2.22.3] — 2026-11-08

**Choose the NAS folder**

### New

- “Test connection” also works before saving a device (NAS, FTP, Switch), using what you've entered: connection, then whether the chosen folder is writable. Nothing is kept.


## [2.22.2] — 2026-11-08

**Choose the NAS folder**

### Improved

- Web: the page reloads by itself after an app update (waiting until you finish typing).


## [2.22.1] — 2026-11-08

**Choose the NAS folder**

### New

- Add a NAS: connect first, browse its folders and pick the one for backups, then save — no typing, no paths to know (TV: Cross on “Folder”; web: Browse…).

### Improved

- If the account can't write to the chosen folder, the message says so clearly and offers to pick another folder.
- A saved NAS is shown with a NAS enclosure (Destinations and Transfers, TV and web).


## [2.21.1] — 2026-11-08

**Search and NAS**

### Improved

- QNAP tile with a real QNAP enclosure (TS-264), Synology with its own (DS224+), TV and web.
- Transfers (TV): all devices visible at once, across several rows; Switch consoles show up without scrolling. Up / Down move between rows.


## [2.21.0] — 2026-11-08

**Search and NAS**

### New

- Web: the magnifier in the header opens search (so does the “/” key).
- Transfers: an “Add a NAS” card in FROM and TO; the added NAS shows up there right away.

### Improved

- Search ignores case and accents, also works by ID (PPSA…), on the TV and the web.
- TV: the magnifier shows the button that opens it (Triangle) on Home and Library.
- Synology and QNAP tiles: same NAS image as the other devices, with the name below.
- Web: Transfers devices wrap onto new lines, everything is visible.


## [2.20.3] — 2026-11-08

**The web catches up with the TV**

### Improved

- Device settings (TV): Cross on “Folder” browses the device, Square picks the folder shown; it stays saved in the connection.
- Synology and QNAP tiles: logo centered like the other images, “NAS” below (TV and web).


## [2.20.2] — 2026-11-08

**The web catches up with the TV**

### Fixed

- Web: the Synology and QNAP logos no longer sit on top of the app logo, and device images are the right size.


## [2.20.1] — 2026-11-08

**The web catches up with the TV**

### New

- Web interface on par with the TV app: Home, Transfers (with NAS and full backup), PS4 filter, editable Destinations (Switch, FTP / FTPS, folder layout, test, browse), full plans with rotation, plans and image tools from the game page, emulator settings, file browser.

### Improved

- The version shown on the web is the app's version.
- The web uses the same device images as the TV (PS5, USB disk, Switch, consoles, Synology, QNAP, FTP / SMB, cloud).

### Fixed

- Pairing page: the cursor no longer jumps back into the code field while you type the device name.


## [2.19.0] — 2026-11-08

**From the NAS**

### New

- Transfers from a NAS: choose the NAS as source, tick its saves, and put them back on the PS5 (each file in its original place, registration included, without overwriting anything) or into a folder on a disk. Verified on arrival.

### Improved

- To a NAS: verified copy, organized like your other backups.


## [2.18.0] — 2026-11-08

**Image tools**

### New

- Game page, Triangle: tools for its image — verify, repair, compress, decompress (folder or .exfat), extract, read-only, speed test, AMPR index, or send it to another disk. Only tools that make sense for that game are offered (installed, image or folder).

### Improved

- A game's page shows the disk that actually holds its data and its form (.ffpfsc image, folder, installed).

### Fixed

- Copying or moving a game to a disk from the Transfers tab is no longer wrongly refused.


## [2.17.1] — 2026-11-08

**Game images**

### Fixed

- PS5 system apps are no longer listed among installed games.


## [2.17.0] — 2026-11-08

**Game images**

### New

- Compress a PS5 game into a .ffpfsc image, decompress it, create or extract a .exfat image, verify and repair an image: all done by the app's engine (PS5 Game Compressor code built in), with no other tool or port.
- Copy or move a game between internal storage and a USB disk: the copy is re-read (SHA-256), then re-registered with ShadowMount; the original is only deleted after verification.
- Each game shows how it's installed (native, .ffpfsc / .exfat / .ffpkg image, or folder) and the disk that actually holds its data.

### Improved

- A game being played is never touched: compressing, moving and deleting are refused while it's running.
- Transfer tracking shows the current step (compression, verification…).


## [2.16.1] — 2026-11-08

**Full backup at the right size**

### Fixed

- Full backup: each account's game saves are counted under “Saves” (no longer under “Accounts”), and the app's working folders are no longer counted as homebrews.


## [2.16.0] — 2026-11-08

**Transfers**

### New

- New Transfers tab: choose from where and to where (PS5, USB disks, Switch, NAS), then Copy, Move, Compress or Sync, with game selection, options and transfer tracking.
- Full PS5 backup: accounts, saves, game registrations, homebrews and their data, boot settings, captures — each category with its size, all in one dated backup on a disk or the NAS.

### Improved

- Restoring PS5 saves is built into the engine: no need to run another tool alongside.

### Fixed

- All six tabs fit in the header.


## [2.15.1] — 2026-11-08

**The real games**

### Fixed

- PS5 games mounted from an image (shadowmount, Game Compressor) finally count their real size: backing up the game copies the image (.ffpfsc…) and its registration, not just the shortcut of a few dozen MB.


## [2.15.0] — 2026-11-08

**Plans, editable with the controller**

### New

- Game page: “Scheduled plans…” to add it to one or more plans (or create one with this game), and “Browse location” to see its folders.
- Full plan editor on the TV: name, when (on demand, daily, weekly), days ticked one by one, time (in quarter hours), content (chosen games or rule), what to back up, destination, versions kept, active or paused.
- A loading spinner appears when the app is waiting for a response (folders, connecting to a device).

### Improved

- Tasks › Plans: Cross opens the plan to edit it; R2 runs it right away.


## [2.14.2] — 2026-11-08

**Small touch-ups**

### Fixed

- Browsing a Switch or a NAS no longer shows “the engine isn't responding” when the device takes a few seconds to answer (waking from sleep): the app waits up to a minute, without freezing.


## [2.14.1] — 2026-11-08

**Browse your devices**

### New

- Destinations: Triangle on a device opens its folders (internal storage, USB disks, Switch, NAS), with file sizes.


## [2.14.0] — 2026-11-08

**Your backups, organized your way**

### New

- A NAS or FTP server can store saves by platform, like your existing backups: “PS5 backup/[PS5] Name (PPSA…)/saves/<date>/”. “Folder layout” setting on the device page.
- Backups already on the NAS (made by your scripts) appear in Saves, one version per date.

### Improved

- Folders whose names contain brackets (“[PS5] …”) now list correctly on Synology NAS units.


## [2.13.0] — 2026-11-07

**Encrypted FTP**

### New

- NAS units and FTP servers that support it are reached over encrypted FTP (FTPS): passwords and files no longer travel in clear text on the network. Devices without encryption (ftpd on the Switch) stay on plain FTP.

### Improved

- NAS certificates (often self-signed) are accepted on the local network; their fingerprint is recorded.


## [2.12.1] — 2026-11-07

**Small touch-ups**

### Improved

- An emulator's settings window takes just the space it needs.


## [2.12.0] — 2026-11-07

**Configurable emulators**

### New

- Settings › Sources and emulators: Cross on an emulator opens its settings. Add, change or remove its game folders, choose its settings file and its saves folder.
- A console file browser to pick a folder or file without typing a path (Cross opens, Square picks the folder, Circle goes up).
- “Back to automatic” clears your choices: the emulator is detected on its own again.

### Improved

- Manually configured emulators are flagged, and the collection is rescanned right away.


## [2.11.0] — 2026-11-07

**Destinations, configurable from the PS5**

### New

- Destinations can be set up with the controller: Cross on a disk makes it the default destination; Cross on a Switch opens its page (name, address, port, user, password, folder) with Save, Test and Forget.
- Add a device: Switch, Synology, QNAP or FTP / SMB open a blank form to fill in with the on-screen keyboard; the connection is tested before saving.
- The on-screen keyboard has capitals and symbols (L1); passwords are shown as dots.


## [2.10.3] — 2026-11-07

**Filters with room to breathe**

### New

- PS4 filter in the Library, next to PS5.

### Improved

- More space between the counter, the game name and the filters.
- The types (Everything, PS5, PS4, Apps, ROM) form a group; Emulator, Location and Refresh are separate bubbles.


## [2.10.2] — 2026-11-07

**Small touch-ups**

### Improved

- More space between the header and the top of the Library.


## [2.10.1] — 2026-11-07

**The magnifier**

### New

- A magnifier at the top right: Triangle starts a search, from Home or the Library.

### Improved

- The top of the Library is less cramped.
- Add a device starts with PlayStation 5, then USB disk.


## [2.10.0] — 2026-11-07

**A roomier library**

### New

- The Library is browsed in rows, like Home: PS5 games, PS4 games, one row per console, Apps, Saves without the game. Filters and search stay.

### Improved

- Home: rows start right under the carousel, so you see the covers straight away.


## [2.9.2] — 2026-11-07

**Colored labels**

### Improved

- Cover labels take their family's color: PlayStation blue, Nintendo red, Sega turquoise, purple for apps, amber for the rest, with a subtle relief.


## [2.9.1] — 2026-11-07

**Small touch-ups**

### Improved

- Destinations › Add a device, in a more logical order: USB disk, PlayStation 5, Switch, Consoles, Synology, QNAP, FTP / SMB, Cloud, with a line of explanation under each.
- The “Quit” window is half the size.


## [2.9.0] — 2026-11-07

**A home screen to browse, and search**

### New

- Home scrolls down: “To back up”, “PS5 and PS4 games”, one row per retro console, “Apps”, “Saves without the game” rows.
- Search the Library with Triangle: an on-screen keyboard, results appear as you type.
- Settings are behind a gear at the top right.

### Improved

- The Home background follows the game selected in the rows.


## [2.8.2] — 2026-11-07

**Consistent covers**

### Improved

- All covers share the same full square format, like on PS5 and Switch; retro game boxes are framed on their title.
- A single small, discreet badge (N64, MD, PS5, App…) at the bottom of the cover, instead of the two green labels that hid the title.


## [2.8.1] — 2026-11-07

**Real images, even without the game**

### New

- Games with only their save left get the game's official icon (ELDEN RING, Cyberpunk 2077, Split Fiction…), just like in BlackBox.

### Improved

- An image that arrives later replaces the save's small image without restarting the app.


## [2.8.0] — 2026-11-07

**HTTPS in the engine**

### New

- Switch games that are only in Eden get their official icon in Sync.
- The engine can now connect over HTTPS, with certificate verification.

### Improved

- Destinations: “Other consoles” tile.


## [2.7.2] — 2026-11-07

**Sharper covers**

### Improved

- Large covers (Home, Library banner) are loaded in high definition.
- Modified ROMs get the original game's cover back (e.g. Majora's Mask Redux).
- The Square button is shown everywhere at the bottom of the screen: it switches filters, like Up then Left / Right.
- Holding Circle to quit shows the seconds remaining.

### Fixed

- The cross on Home's “Back up the game…” button is properly centered.


## [2.7.1] — 2026-11-07

**Small touch-ups**

### Improved

- More compact Destinations: Switch consoles get their own card, the Synology and QNAP logos are shown large.
- The Library banner shows the game's name without region tags.


## [2.7.0] — 2026-11-07

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


## [2.6.1] — 2026-11-07

**Small touch-ups**

### Fixed

- A Switch game icon that can't be found online no longer holds up the display.


## [2.6.0] — 2026-11-07

**A home screen, and a nicer sync**

### New

- New Home tab: a carousel highlights games to back up, with a large cover, and your collection right below.
- Switch games that are only in Eden now have their name and icon in Sync.

### Improved

- Filters are picked with the arrows everywhere: Up to reach the filters, Left / Right to change (Saves, By disk, Sync, Tasks).
- Sync shows “Reading…” with a spinner during the comparison.
- A comparison already done is kept: switching filter, Switch or tab no longer loses it. Triangle to re-read.
- The logo is cut out, with no frame around it, and the header is lighter.


## [2.5.2] — 2026-11-07

**Small touch-ups**

### Fixed

- In Sources and emulators, text no longer runs under the emulator icon.


## [2.5.1] — 2026-11-07

**Images everywhere**

### New

- In Settings › Sources and emulators, each emulator shows its small icon.

### Improved

- Covers also appear in Saves › By disk and in Sync.


## [2.5.0] — 2026-11-07

**A new look**

### New

- The PS5 Backup Center logo is back, on the TV and on the web.
- Controller buttons are shown in their real colors: blue cross, red circle, pink square, green triangle.

### Improved

- The selected cover glows with a halo in the library.


## [2.4.0] — 2026-11-07

**Scheduled plans, on the TV too**

### New

- Tasks → Scheduled plans on the TV: create a plan with one press (PS5 saves every evening, ROMs every Sunday, apps every Monday, all games on demand).
- Cross runs a plan right away, Triangle pauses or resumes it. To fine-tune the details, use the web.

### Improved

- Each plan shows when it runs, what it backs up, how many versions it keeps and the result of the last run.


## [2.3.2] — 2026-11-07

**An image for every task**

### Improved

- In Tasks, each row has its image: the game or app cover, or a card in the console's color if no image exists.

### Fixed

- Restores appeared without an image in the task list.


## [2.3.1] — 2026-11-07

**Guided all the way**

### New

- After sending to the Switch, a step-by-step guide explains how to finish the restore in JKSV.

### Improved

- Library filters: reach them with Up, and Emulator / Location change directly with one press, no window.


## [2.3.0] — 2026-11-07

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


## [2.2.0] — 2026-11-07

**Your saves synced between consoles**

### New

- New Sync screen (Saves): your Switch and ProsperoEden side by side, game by game and profile by profile.
- Send a save from the Switch to Eden, or from Eden to the Switch: it lands in JKSV, ready to be restored.
- Backup Center recognizes identical saves by comparing their content, without downloading everything.

### Improved

- Before each transfer, everything is checked and you're shown what will change; nothing is ever deleted on the Switch.

### Fixed

- Comparing saves no longer blocks the rest of the app while it works.


## [2.1.1] — 2026-11-07

**What's new, always at hand**

### New

- Settings → What's new: the history of each version, what's new, improved and fixed.

### Improved

- After an update, “What's new” shows you what changed, just once.

### Fixed

- Long sentences were cut off with “…” on screen: they now wrap.
- The acknowledgments list overflowed the bottom of the screen.


## [2.1.0] — 2026-11-07

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


## [2.0.9] — 2026-11-07

**Your saves, account by account**

### New

- New Saves tab: your accounts on the left, and each game's status — up to date, console newer, never backed up.
- Back up a game's progress with one press, or your whole account at once.
- Uninstalled games keep their save in the list, with their image.


## [2.0.8] — 2026-11-07

**Thanks to those who helped us**

### New

- Acknowledgments screen, at the very bottom of Settings: every project used, its author, its license and its sources.


## [2.0.5] — 2026-11-07

**Updates, right in the app**

### New

- Settings → Updates: check, install, then restart the engine or the app with a countdown.
- Updates possible without a network, from a USB drive.

### Improved

- Only changed files are downloaded: an update installs in a few seconds.
- If something goes wrong during installation, the previous version is restored automatically.

### Fixed

- After an update, the app could refuse to start: fixed.


## [2.0.0] — 2026-11-07

**A new app, a new engine**

### New

- TV app completely rebuilt: faster, with filters that respond instantly.
- The backup engine keeps running when you close the app, and resumes an interrupted backup.
- The same app on your phone or computer, with a 6-digit code.

### Fixed

- The app crashed at startup on some consoles: fixed.


[3.2.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.2.0
[3.1.76]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.76
[3.1.75]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.75
[3.1.74]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.74
[3.1.73]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.73
[3.1.72]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.72
[3.1.71]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.71
[3.1.70]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.70
[3.1.69]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.69
[3.1.68]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.68
[3.1.67]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.67
[3.1.66]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.66
[3.1.65]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.65
[3.1.64]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.64
[3.1.63]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.63
[3.1.62]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.62
[3.1.61]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.61
[3.1.60]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.60
[3.1.59]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.59
[3.1.58]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.58
[3.1.57]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.57
[3.1.56]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.56
[3.1.55]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.55
[3.1.54]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.54
[3.1.53]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.53
[3.1.52]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.52
[3.1.51]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.51
[3.1.50]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.50
[3.1.49]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.49
[3.1.48]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.48
[3.1.47]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.47
[3.1.46]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.46
[3.1.45]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.45
[3.1.44]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.44
[3.1.43]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.43
[3.1.42]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.42
[3.1.41]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.41
[3.1.40]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.40
[3.1.39]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.39
[3.1.38]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.38
[3.1.37]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.37
[3.1.36]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.36
[3.1.35]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.35
[3.1.34]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.34
[3.1.33]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.33
[3.1.32]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.32
[3.1.31]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.31
[3.1.30]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.30
[3.1.29]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.29
[3.1.28]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.28
[3.1.27]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.27
[3.1.26]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.26
[3.1.25]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.25
[3.1.24]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.24
[3.1.23]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.23
[3.1.22]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.22
[3.1.21]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.21
[3.1.20]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.20
[3.1.19]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.19
[3.1.18]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.18
[3.1.17]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.17
[3.1.16]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.16
[3.1.15]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.15
[3.1.14]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.14
[3.1.13]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.13
[3.1.12]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.12
[3.1.11]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.11
[3.1.10]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.10
[3.1.9]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.9
[3.1.8]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.8
[3.1.7]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.7
[3.1.6]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.6
[3.1.5]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.5
[3.1.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.4
[3.1.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.3
[3.1.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.2
[3.1.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.1
[3.1.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.1.0
[3.0.33]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.33
[3.0.32]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.32
[3.0.31]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.31
[3.0.30]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.30
[3.0.29]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.29
[3.0.28]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.28
[3.0.27]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.27
[3.0.26]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.26
[3.0.25]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.25
[3.0.24]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.24
[3.0.23]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.23
[3.0.22]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.22
[3.0.21]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.21
[3.0.20]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.20
[3.0.19]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.19
[3.0.18]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.18
[3.0.17]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.17
[3.0.16]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.16
[3.0.15]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.15
[3.0.14]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.14
[3.0.13]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.13
[3.0.12]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.12
[3.0.11]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.11
[3.0.10]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.10
[3.0.9]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.9
[3.0.8]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.8
[3.0.7]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.7
[3.0.6]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.6
[3.0.5]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.5
[3.0.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.4
[3.0.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.3
[3.0.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.2
[3.0.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.1
[3.0.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v3.0.0
[2.99.46]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.46
[2.99.45]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.45
[2.99.43]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.43
[2.99.41]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.41
[2.99.40]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.40
[2.99.39]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.39
[2.99.38]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.38
[2.99.37]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.37
[2.99.36]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.36
[2.99.35]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.35
[2.99.34]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.34
[2.99.33]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.33
[2.99.32]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.32
[2.99.31]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.31
[2.99.30]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.30
[2.99.29]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.29
[2.99.27]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.27
[2.99.25]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.25
[2.99.24]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.24
[2.99.23]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.23
[2.99.22]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.22
[2.99.21]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.21
[2.99.20]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.20
[2.99.19]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.19
[2.99.18]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.18
[2.99.16]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.16
[2.99.15]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.15
[2.99.14]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.14
[2.99.13]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.13
[2.99.12]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.12
[2.99.11]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.11
[2.99.10]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.10
[2.99.9]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.9
[2.99.8]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.8
[2.99.7]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.7
[2.99.6]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.6
[2.99.5]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.5
[2.99.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.4
[2.99.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.3
[2.99.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.2
[2.99.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.1
[2.99.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.99.0
[2.98.5]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.98.5
[2.98.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.98.4
[2.98.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.98.3
[2.98.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.98.2
[2.98.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.98.1
[2.98.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.98.0
[2.97.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.97.0
[2.96.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.96.1
[2.96.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.96.0
[2.95.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.95.3
[2.95.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.95.2
[2.95.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.95.1
[2.95.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.95.0
[2.94.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.94.0
[2.93.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.93.1
[2.93.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.93.0
[2.92.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.92.4
[2.92.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.92.3
[2.92.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.92.2
[2.92.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.92.1
[2.92.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.92.0
[2.91.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.91.1
[2.91.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.91.0
[2.90.9]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.90.9
[2.90.8]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.90.8
[2.90.7]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.90.7
[2.90.6]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.90.6
[2.90.5]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.90.5
[2.90.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.90.4
[2.90.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.90.3
[2.90.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.90.2
[2.90.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.90.1
[2.90.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.90.0
[2.89.9]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.89.9
[2.89.8]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.89.8
[2.89.7]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.89.7
[2.89.6]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.89.6
[2.89.5]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.89.5
[2.89.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.89.4
[2.89.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.89.3
[2.89.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.89.2
[2.89.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.89.1
[2.89.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.89.0
[2.88.11]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.88.11
[2.88.10]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.88.10
[2.88.9]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.88.9
[2.88.8]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.88.8
[2.88.7]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.88.7
[2.88.6]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.88.6
[2.88.5]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.88.5
[2.88.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.88.4
[2.88.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.88.3
[2.88.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.88.2
[2.88.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.88.1
[2.88.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.88.0
[2.87.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.87.2
[2.87.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.87.1
[2.87.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.87.0
[2.86.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.86.0
[2.85.7]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.85.7
[2.85.6]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.85.6
[2.85.5]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.85.5
[2.85.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.85.4
[2.85.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.85.3
[2.85.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.85.2
[2.85.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.85.1
[2.85.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.85.0
[2.84.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.84.0
[2.83.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.83.2
[2.83.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.83.1
[2.83.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.83.0
[2.82.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.82.1
[2.82.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.82.0
[2.81.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.81.0
[2.80.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.80.2
[2.80.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.80.1
[2.80.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.80.0
[2.79.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.79.4
[2.79.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.79.3
[2.79.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.79.2
[2.79.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.79.1
[2.79.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.79.0
[2.78.5]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.78.5
[2.78.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.78.4
[2.78.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.78.3
[2.78.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.78.2
[2.78.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.78.1
[2.78.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.78.0
[2.77.5]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.77.5
[2.77.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.77.4
[2.77.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.77.3
[2.77.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.77.2
[2.77.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.77.1
[2.77.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.77.0
[2.76.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.76.3
[2.76.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.76.2
[2.76.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.76.1
[2.76.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.76.0
[2.75.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.75.0
[2.74.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.74.0
[2.73.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.73.0
[2.72.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.72.0
[2.71.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.71.1
[2.71.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.71.0
[2.70.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.70.1
[2.70.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.70.0
[2.69.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.69.0
[2.68.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.68.0
[2.67.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.67.0
[2.66.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.66.0
[2.65.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.65.1
[2.65.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.65.0
[2.64.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.64.1
[2.64.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.64.0
[2.63.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.63.2
[2.63.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.63.1
[2.63.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.63.0
[2.62.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.62.0
[2.61.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.61.2
[2.61.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.61.1
[2.61.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.61.0
[2.60.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.60.2
[2.60.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.60.1
[2.60.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.60.0
[2.59.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.59.2
[2.59.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.59.1
[2.59.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.59.0
[2.58.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.58.0
[2.57.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.57.1
[2.57.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.57.0
[2.56.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.56.0
[2.55.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.55.0
[2.54.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.54.1
[2.54.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.54.0
[2.53.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.53.0
[2.52.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.52.1
[2.52.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.52.0
[2.51.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.51.0
[2.50.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.50.0
[2.49.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.49.2
[2.49.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.49.1
[2.49.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.49.0
[2.48.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.48.1
[2.48.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.48.0
[2.47.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.47.0
[2.46.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.46.3
[2.46.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.46.2
[2.46.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.46.1
[2.46.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.46.0
[2.45.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.45.1
[2.44.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.44.1
[2.44.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.44.0
[2.43.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.43.1
[2.43.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.43.0
[2.42.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.42.4
[2.42.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.42.3
[2.42.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.42.2
[2.42.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.42.1
[2.42.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.42.0
[2.41.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.41.0
[2.40.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.40.0
[2.39.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.39.0
[2.38.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.38.0
[2.37.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.37.4
[2.37.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.37.3
[2.37.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.37.2
[2.37.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.37.1
[2.37.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.37.0
[2.36.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.36.1
[2.36.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.36.0
[2.35.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.35.1
[2.35.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.35.0
[2.34.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.34.0
[2.33.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.33.0
[2.32.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.32.1
[2.32.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.32.0
[2.31.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.31.1
[2.31.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.31.0
[2.30.9]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.30.9
[2.30.7]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.30.7
[2.30.6]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.30.6
[2.30.5]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.30.5
[2.30.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.30.4
[2.30.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.30.2
[2.30.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.30.1
[2.30.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.30.0
[2.29.5]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.29.5
[2.29.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.29.1
[2.29.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.29.0
[2.28.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.28.0
[2.27.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.27.1
[2.26.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.26.0
[2.25.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.25.0
[2.24.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.24.1
[2.24.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.24.0
[2.23.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.23.1
[2.23.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.23.0
[2.22.4]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.22.4
[2.22.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.22.3
[2.22.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.22.2
[2.22.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.22.1
[2.21.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.21.1
[2.21.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.21.0
[2.20.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.20.3
[2.20.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.20.2
[2.20.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.20.1
[2.19.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.19.0
[2.18.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.18.0
[2.17.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.17.1
[2.17.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.17.0
[2.16.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.16.1
[2.16.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.16.0
[2.15.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.15.1
[2.15.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.15.0
[2.14.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.14.2
[2.14.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.14.1
[2.14.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.14.0
[2.13.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.13.0
[2.12.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.12.1
[2.12.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.12.0
[2.11.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.11.0
[2.10.3]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.10.3
[2.10.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.10.2
[2.10.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.10.1
[2.10.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.10.0
[2.9.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.9.2
[2.9.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.9.1
[2.9.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.9.0
[2.8.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.8.2
[2.8.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.8.1
[2.8.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.8.0
[2.7.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.7.2
[2.7.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.7.1
[2.7.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.7.0
[2.6.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.6.1
[2.6.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.6.0
[2.5.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.5.2
[2.5.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.5.1
[2.5.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.5.0
[2.4.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.4.0
[2.3.2]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.3.2
[2.3.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.3.1
[2.3.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.3.0
[2.2.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.2.0
[2.1.1]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.1.1
[2.1.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.1.0
[2.0.9]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.0.9
[2.0.8]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.0.8
[2.0.5]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.0.5
[2.0.0]: https://github.com/consolecenterstudio/ps5-backup-center/releases/tag/v2.0.0
