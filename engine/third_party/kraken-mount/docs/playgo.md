# PlayGo stub (pgo_stub)

Kraken Mount mounts games instead of installing them, so the console has no PlayGo install record for
them. A game that asks PlayGo about its install state can then decide it is not fully installed and ask
you to finish the download. Kraken Mount bundles drakmor's
[pgo_stub](https://github.com/drakmor/pgo_stub) (release 0.6, GPL-3.0), a replacement
`libScePlayGo.sprx` that reports every chunk as available and installed.

## When it is added

When Kraken Mount builds or refreshes a game's cache, it adds `libScePlayGo.sprx` to the cache's
`fakelib/` if both are true:

- the game's `eboot.bin` imports `libScePlayGo` (with a backport that has its own `eboot.bin`, that
  one is checked);
- the game's `fakelib/` (or the backport's) has no `libScePlayGo.sprx` yet.

It replaces a `libScePlayGo.sprx` the game or backport ships only when that file is a known older
pgo_stub release (0.4 or 0.5), upgraded to the bundled one (log line `fakelib: upgraded`); any other
copy is kept as it is.

## Check that it was added

Look in `/data/krakenmount/kraken.log` for a line like this:

```text
<TID>: fakelib: added libScePlayGo.sprx (pgo_stub 0.6)
```

If a game has no such line, its `eboot.bin` did not match, the game already has its own file, or
`add_emulators` is off.

## Optional config: playgo_stub.dat

The stub reads `/app0/playgo_stub.dat` when the game runs. Without it, the stub reports 1000 chunks,
all languages, and everything installed. To change that, put `playgo_stub.dat` here:

- folder game: in the game folder, next to `eboot.bin`;
- pkg game: in the root of the pkg, and Kraken Mount extracts it to the cache. You can also put it
  straight into `/data/krakenmount/caches/<TID>/`, which is the pkg game's `/app0`.

The file has up to 3 lines:

1. a chunk count, or a comma-separated list of chunk IDs;
2. optional: a scenario count from 0 to 5;
3. optional: languages, as a `0x` hex mask or a comma-separated list of language names or IDs.

Example:

```text
1000
5
english_us,russian,english_gb
```

The upstream README lists every language name and the fallback for each invalid value.

## Use another build

Put your own `libScePlayGo.sprx` into the game's `fakelib/`. Kraken Mount sees that the game ships one
and leaves it alone.

## Turn it off

Set `add_emulators = false` in `/data/krakenmount/config.toml`, or clear the "Add the bundled DLC
(dlc_emu) and PlayGo (pgo_stub) emulators" checkbox in the web UI. This also turns off the bundled DLC
emulator (dlc_emu).

## Limits

- Only `eboot.bin` is checked. A PlayGo import that sits only in another prx module is missed.
- An encrypted `eboot.bin` never matches, so such a game gets no stub.
- pgo_stub has not been tested on every title.
