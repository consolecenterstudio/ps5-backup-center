# PS5 Backup Center — notices

PS5 Backup Center is free software, GPL-3.0-or-later (GPL-3.0.txt). Source code:
https://github.com/consolecenterstudio/ps5-backup-center

Every third-party component, its author, licence and role is listed in `credits.json`
(also shown in the app: Settings › About). Licence texts are in this folder.

Files of this package not built from the project's sources:

- `sce_module/libc.prx` and the native-app layout (`sce_sys/`) come from
  ps5-native-app-boilerplate (BlackBearReloaded, GPL-3.0-or-later),
  https://github.com/blackbearreloaded/ps5-native-app-boilerplate.
- `sce_sys/snd0.at9`: « Night Drive », Copyright (C) 2026 BlackBearReloaded, GPL-3.0-or-later,
  from the same project, unmodified.
- `assets/sound-*.wav` and `assets/music.wav` are synthesized by the project (build-system/make_sounds.py),
  no third-party samples; GPL-3.0-or-later.
- `assets/font.rgba`: glyph atlas of the Inter typeface (SIL Open Font License, Inter-OFL.txt).
- The TV app statically links parts of LLVM libc++, libc++abi and libunwind from the public
  PS5 payload SDK (Apache-2.0 WITH LLVM-exception, LLVM-Apache-2.0-with-exceptions.txt) and
  assert.o from the SDK libc (BSD, SDK-assert-BSD.txt). No Sony SDK file is included.
