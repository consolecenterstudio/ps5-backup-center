# elfldr (vendored, one opt-in change)

- Source: https://github.com/ps5-payload-dev/elfldr — commit 2a8da002e5292481ea964daa7342bbf4855f4c33 (2026-10-21)
- Author: John Törnblom — GPL-3.0-or-later (see LICENSE)
- Files kept: elfldr.c, elfldr.h, pt.c, pt.h, log.h (only the "spawn an ELF in a new process" part; no socket server, no port).
- Used by engine/src/update/self_launch.cpp so Backup Center restarts its own engine after an update, without any external loader.

## Change (2026-11-11)
- `elfldr.c`, `elfldr_exec()`: the block that restores the spawned process's original jail/root dir, caps and authid is
  wrapped in `#ifndef ELFLDR_KEEP_PRIVILEGES`. Our build defines it, so the engine keeps raised privileges after launch,
  the way Arsenal's copy of elfldr behaves. Without it, the self-launched engine ran with SceSpZeroConf's normal rights and
  crashed (stack overflow in a worker thread) on PS5-02, 2026-11-11.
