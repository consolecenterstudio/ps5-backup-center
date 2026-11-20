# DLC with dlc_emu

[dlc_emu](https://github.com/drakmor/dlc_emu) by drakmor replaces three system libraries:
`libSceAppContent.sprx`, `libSceNpEntitlementAccess.sprx` and `libSceGameUpdate.sprx`. The game then
sees the DLC listed in `/app0/dlc_emu.ini` as owned. Kraken Mount bundles release 0.3 (GPL-3.0).

## When Kraken Mount adds it

When a cache is built, Kraken Mount looks for `dlc_emu.ini` (exactly this name, in lowercase) at
the game root or at the backport root. If it finds one, it copies each of the 3 files into the
cache's `fakelib/` when the game (or the backport) ships no file of that name, so a copy the game
brings wins. A known older dlc_emu release in the game (0.1a or 0.2) is upgraded to the bundled one
(log line `fakelib: upgraded`); any other copy is kept as it is.

## Write dlc_emu.ini

One section per DLC. A DLC with files:

```ini
[PSAC]
content_id=UP9000-PPSA01234_00-SAMPLECONTENT001
mount_point=/app0/addcont0
```

A DLC that is only an entitlement, with no files:

```ini
[PSAL]
content_id=UP9000-PPSA01234_00-SAMPLELICENSE001
download_status=NO_EXTRA_DATA
```

`content_id` is exactly 36 characters. `mount_point` is at most 15 characters, and the directory must
already exist and hold the DLC files: dlc_emu does not create it. Other sections and keys are in
the upstream README and `dlc_emu.example.ini`.

## Where it goes

- Folder game: the root of the game folder, next to `eboot.bin`. That folder is `/app0`, so an edit
  applies at the next launch.
- pkg game: the root of the pkg. Kraken Mount extracts it into the cache, which is the game's
  `/app0`: `/data/krakenmount/caches/<TID>/dlc_emu.ini`. You can edit that copy, but a Rebuild writes
  it again from the pkg, because the build extracted it. To keep a change, put `dlc_emu.ini` at the
  backport root instead (backport files are copied over pkg files) and Rebuild.

## Check it worked

Open `/data/krakenmount/kraken.log`. Each added file logs one line:

```
<TID>: fakelib: added libSceAppContent.sprx (dlc_emu 0.3)
```

If a file has no line, the game ships its own copy, no `dlc_emu.ini` was found, or `add_emulators`
is off.

## Turn it off

Set `add_emulators = false` in `/data/krakenmount/config.toml`, or clear the matching checkbox in the
web UI. This also turns off the bundled PlayGo emulator (pgo_stub). The change only recomposes
`fakelib/`, with no full rebuild.

## Known limits

- 0.3 is the latest dlc_emu release. Two later upstream fixes are not released yet: a stack
  overrun in every RPC, and an RPC hang on firmware 5.02.
- A game whose dump ships its own dlc_emu files keeps them; Kraken Mount does not update them.
- dlc_emu does not merge DLC that is really installed: the game sees only the list in the ini.
