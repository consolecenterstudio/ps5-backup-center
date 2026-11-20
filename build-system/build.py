#!/usr/bin/env python3
"""Compilation de PS5 Backup Center.

Usage :
    python3 build-system/build.py [engine|app|package|release|all] [-j N] [--clean]

engine  : le moteur, out/engine/pbc-engine2.elf (sources dans engine/, appli web comprise)
app     : l'appli télé, out/app/eboot.bin (sources dans app/)
package : out/package/PPSA99950/ (dossier de l'appli) et out/package/PPSA99950.zip
release : releases/<version>/ — PPSA99950.zip, ps5-backup-center-<version>-engine.elf, SHA256SUMS
all     : engine + app + package (par défaut)

Les sorties vont dans out/. Rien n'est envoyé sur la console.
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import hashlib
import json
import re
import os
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BS = ROOT / "build-system"
OUT = ROOT / "out"
TC = ROOT / "toolchain/.local/toolchain"
BIN = Path(os.environ["LLVM"])
SDK = Path(os.environ["SDK"])
NATIVE_TOOL = Path(os.environ["PS5_NATIVE_TOOL"])
ENV = dict(os.environ)

COMMON = [
    "-target", "x86_64-sie-ps5", "-fvisibility-nodllstorageclass=default",
    "-isysroot", str(SDK),
    "-isystem", str(SDK / "target/include/c++/v1"),
    "-isystem", str(SDK / "target/include"),
    "-fno-stack-protector", "-fno-plt", "-O2", "-Wall", "-Wextra", "-Werror",
    "-DPBCK_PS5=1",
    # Aucun chemin de la machine de construction dans les binaires publiés (assertions, __FILE__, débogage).
    f"-ffile-prefix-map={ROOT}/=",
    f"-ffile-prefix-map={Path.home()}/=",
]
CXX = ["-femulated-tls", "-fno-exceptions", "-fno-rtti"]
# Adresse officielle des mises à jour, intégrée au moteur (rien à saisir sur la console). Une clé USB « PBC Update »
# ou le dossier local /data/pbc-update-source restent prioritaires.
UPDATE_URL = "https://consolecenterstudio.github.io/ps5-backup-center/updates/latest.json"



RECORD: list | None = None  # build.sh public : les commandes sont notées au lieu d'être lancées


# Notre ShadowMount intégré (docs/PLAN-integration-shadowmount.md) : ShadowMountPlus 1.7beta12 (GPL-3.0) tel quel, sauf
# ses crochets SceShellCore, son API web, son serveur de paquets, sa mise à jour d'ampr-emu, son ventilateur, sa lecture
# du journal du noyau et son raccourci de menu (remplacés par engine/src/images/smp_stubs.c). Une copie de travail
# (out/smp/src) reçoit une seule retouche : les images restent montées (persistent_image_mounts), puisque sans crochets
# rien ne les monterait au lancement. json-c 0.19 (MIT) compilé avec, avec nos en-têtes de configuration.
SMP_SKIP = {"sm_api_service", "sm_pkg_server", "sm_pkg_ws", "sm_ampr_updater", "sm_shellcore_hooks", "sm_shellcore_service",
            "sm_shellcore_remote", "sm_shellcore_flags", "sm_fan", "sm_icon_thumb", "sm_shell_icon", "sm_mdbg", "libkernel_sys_ext",
            "sm_shellcore_offsets"}
JSONC = ["arraylist", "debug", "json_c_version", "json_object", "json_object_iterator", "json_pointer", "json_patch", "json_tokener",
         "json_util", "json_visit", "linkhash", "printbuf", "random_seed", "strerror_override"]


# Les retouches (sans crochets, rien ne monterait un jeu à son lancement : tout reste monté, comme ShadowMountPlus 1.6).
SMP_PRIVATE_CONFIG = "/data/pbc-backup-center/shadowmount"  # smp_bridge.c : copie des réglages du ShadowMountPlus installé

SMP_PATCHES = [
    # 1. Les images restent montées entre deux parties.
    ("sm_config_mount.c", "  runtime_config_t cfg = g_runtime_state.cfg;\n  pthread_mutex_unlock(&g_runtime_state_mutex);\n  return cfg;",
     "  runtime_config_t cfg = g_runtime_state.cfg;\n  cfg.persistent_image_mounts = true;  /* PS5 Backup Center : sans crochets */\n"
     "  pthread_mutex_unlock(&g_runtime_state_mutex);\n  return cfg;"),
    # 2. Les couches des titres (/system_ex/app/<TID>) ne sont pas relâchées après chaque parcours.
    ("sm_scan.c", "bool release_scan_runtime_mounts(void) {\n",
     "bool release_scan_runtime_mounts(void) {\n  return true;  /* PS5 Backup Center : sans crochets, les couches restent posées */\n"),
    # 3. Un titre inscrit dont la couche manque est remonté (le 1.7 compte sur ses crochets pour la poser au lancement).
    ("sm_scan.c", "  if (source_valid && appmeta_present) {\n",
     "  if (source_valid && appmeta_present && is_data_mounted(info->title_id)) {  /* PS5 Backup Center */\n"),
]


def c_array(name: str, data: bytes) -> str:
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    return f"unsigned char {name}[] = {{\n  " + ",\n  ".join(rows) + f"\n}};\nunsigned int {name}_len = {len(data)};\n"


def smp_units() -> list[dict]:
    src = ROOT / "engine/third_party/shadowmountplus"
    work = OUT / "smp/src"
    work.mkdir(parents=True, exist_ok=True)
    for f in sorted((src / "src").glob("*.c")):
        if f.stem in SMP_SKIP:
            continue
        text = f.read_text()
        for name, old, new in SMP_PATCHES:
            if f.name == name:
                if text.count(old) != 1:
                    raise SystemExit(f"ShadowMountPlus : {name} a changé, revoir la retouche « {old[:60]} »")
                text = text.replace(old, new)
        dst = work / f.name
        if not dst.exists() or dst.read_text() != text:
            dst.write_text(text)
    assets = {"smp_assets.c": c_array("config_ini_example", (src / "config.ini.example").read_bytes()) + c_array("smp_icon_png", (src / "smp_icon.png").read_bytes())}
    for n, t in assets.items():
        if not (work / n).exists() or (work / n).read_text() != t:
            (work / n).write_text(t)
    # 4. Ses réglages : une copie privée, jamais ceux du ShadowMountPlus installé (sa conversion v1 → v2 les réécrirait).
    winc = OUT / "smp/include"
    winc.mkdir(parents=True, exist_ok=True)
    for h in sorted(f for f in (src / "include").rglob("*") if f.is_file()):
        text = h.read_text()
        if h.name == "sm_paths.h":
            for name in ("config.ini", "profiles.ini", "autotune.ini", "smb.json", "config-v1-backup"):
                old_p = f'"/data/shadowmount/{name}"'
                if text.count(old_p) != 1:
                    raise SystemExit(f"ShadowMountPlus : sm_paths.h a changé, revoir {name}")
                text = text.replace(old_p, f'"{SMP_PRIVATE_CONFIG}/{name}"')
        dst = winc / h.relative_to(src / "include")
        dst.parent.mkdir(parents=True, exist_ok=True)
        if not dst.exists() or dst.read_text() != text:
            dst.write_text(text)
    inc = ["-I", str(winc), "-I", str(work), "-I", str(ROOT / "engine/third_party/json-c-pbc/inc"), "-I", str(ROOT / "engine/third_party/json-c-pbc"),
           "-I", str(ROOT / "engine/third_party/json-c"), "-I", str(ROOT / "engine/third_party/sqlite"), "-I", str(ROOT / "engine/third_party/libsmb2/include")]
    defs = ["_BSD_SOURCE", "NDEBUG", "SM_VFS_ENABLED", 'SHADOWMOUNT_VERSION="1.7beta12-pbc"', "main=pbc_smp_payload_main_unused"]
    units = [{"src": str(p.relative_to(ROOT)), "std": "-std=gnu11", "extra": ["-w"] + inc, "defines": defs}
             for p in sorted(work.glob("*.c")) if (p.stem in {f.stem for f in (src / "src").glob("*.c")} and p.stem not in SMP_SKIP) or p.name == "smp_assets.c"]
    units += [{"src": f"engine/third_party/json-c/{j}.c", "std": "-std=gnu11", "extra": ["-w", "-I", str(ROOT / "engine/third_party/json-c-pbc"), "-I", str(ROOT / "engine/third_party/json-c")], "defines": []}
              for j in JSONC]
    units += [{"src": f"engine/src/images/{n}.c", "std": "-std=gnu11", "extra": inc, "defines": defs[:4]} for n in ("smp_bridge", "smp_stubs")]
    return units


def sh(cmd: list[str], **kw) -> None:
    if RECORD is not None:
        RECORD.append([str(c) for c in cmd])
        return
    r = subprocess.run([str(c) for c in cmd], env=ENV, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.stderr.write(" ".join(map(str, cmd))[:2000] + "\n" + r.stdout + r.stderr)
        raise SystemExit(f"échec : {Path(str(cmd[0])).name}")


def needs_rebuild(obj: Path, dep: Path) -> bool:
    if not obj.exists() or not dep.exists():
        return True
    t = obj.stat().st_mtime
    text = dep.read_text().replace("\\\n", " ").replace("\\ ", "\0")
    for d in text.split(": ", 1)[1].split():
        p = Path(d.replace("\0", " "))
        if not p.exists() or p.stat().st_mtime > t:
            return True
    return False


def compile_all(label: str, units: list[dict], jobs: int, includes: list[str], extra: list[str]) -> list[Path]:
    objdir = OUT / label / "obj"
    objdir.mkdir(parents=True, exist_ok=True)
    tasks, objs = [], []
    for u in units:
        src = ROOT / u["src"]
        stem = u["src"].replace("/", "__").rsplit(".", 1)[0]
        obj, dep = objdir / f"{stem}.o", objdir / f"{stem}.d"
        objs.append(obj)
        is_c = u["std"].startswith("-std=c1")
        cmd = [BIN / "clang-18", *COMMON, u["std"], *(u.get("extra") or [])]
        cmd += extra
        if not is_c:
            # libtorrent et notre module BitTorrent : exceptions et RTTI (eux seulement) ; le reste du moteur sans.
            cmd += ["-femulated-tls", "-fexceptions", "-frtti"] if u.get("exceptions") else CXX
        for i in includes:
            cmd += ["-I", ROOT / i]
        cmd += [f"-D{d}" for d in u.get("defines", [])]
        # Une option changée (version, définitions) recompile aussi : la commande est retenue à côté de l'objet.
        stamp, line = objdir / f"{stem}.cmd", " ".join(map(str, cmd))
        if RECORD is not None:
            RECORD.append([str(c) for c in cmd] + ["-c", str(src), "-o", str(obj)])
            continue
        if not needs_rebuild(obj, dep) and stamp.exists() and stamp.read_text() == line:
            continue
        cmd += ["-MMD", "-MF", dep, "-c", src, "-o", obj]
        tasks.append((u["src"], cmd, stamp, line))
    print(f"[{label}] {len(units)} unités, {len(tasks)} à compiler")
    t0 = time.time()
    with cf.ThreadPoolExecutor(jobs) as ex:
        futs = {ex.submit(sh, t[1]): t for t in tasks}
        for f in cf.as_completed(futs):
            f.result()
            futs[f][2].write_text(futs[f][3])  # retenue seulement après une compilation réussie
    print(f"[{label}] compilation : {time.time() - t0:.1f} s")
    return objs


def version() -> dict:
    """Version du produit (moteur + appli) : la première entrée de build-system/changelog.json
    (les nouveautés de chaque version, affichées dans l'app à la façon d'Orbit)."""
    latest = json.loads((BS / "changelog.json").read_text())["versions"][0]
    return {"version": latest["version"], "notes": latest.get("new", []) + latest.get("improved", []) + latest.get("fixed", [])}


def build_fpkg() -> Path:
    """Paquets FPKG : notre façade Rust (engine/fpkg/pbc-fpkg) sur le code de Dump Forge (engine/third_party/fpkg), en
    bibliothèque statique pour la PS5. Rust 1.100.0 (celui que Dump Forge épingle), std reconstruite pour l'ABI FreeBSD 11."""
    cargo = Path.home() / ".cargo/bin/cargo"
    env = dict(os.environ, RUSTUP_TOOLCHAIN="1.100.0", RUSTC_BOOTSTRAP="1", PS5_PAYLOAD_SDK=str(SDK),
               CARGO_TARGET_DIR=str(ROOT / "engine/out/fpkg-target"),
               # Aucun chemin du Mac dans le moteur livré (qa/scan_package.py) : les chemins des sources sont remplacés.
               CARGO_TARGET_X86_64_PS5_FREEBSD_RUSTFLAGS=" ".join([
                   '--cfg libc_unstable_freebsd_version="11"',
                   # rustc garde la DERNIÈRE règle qui correspond : du plus général au plus précis.
                   f"--remap-path-prefix={Path.home()}=/src",
                   f"--remap-path-prefix={ROOT}=/pbc",
                   f"--remap-path-prefix={Path.home() / '.rustup'}=/rust",
                   f"--remap-path-prefix={Path.home() / '.cargo'}=/crates"]),
               CC_x86_64_ps5_freebsd=str(SDK / "bin/prospero-clang"), AR_x86_64_ps5_freebsd=str(SDK / "bin/prospero-ar"),
               CFLAGS_x86_64_ps5_freebsd=f"--target=x86_64-sie-ps5 -mno-red-zone -femulated-tls -ffile-prefix-map={Path.home()}=/src")
    env["PATH"] = str(Path.home() / ".cargo/bin") + os.pathsep + env.get("PATH", "")
    # pbc-rust : la seule bibliothèque Rust du moteur (paquets .pkg + lancement depuis un .pkg) ; ses bibliothèques à
    # glisser dans les jeux (out/injected) sont compilées d'abord, par nous (build-system/build_injected.sh).
    sh(["zsh", BS / "build_injected.sh"])
    subprocess.run([str(cargo), "build", "--release", "--locked", "-q", "-p", "pbc-rust", "--target", "fpkg/x86_64-ps5-freebsd.json",
                    "-Z", "json-target-spec", "-Z", "build-std=std,panic_abort", "-Z", "build-std-features=panic-unwind"],
                   cwd=ROOT / "engine", env=env, check=True)
    out = ROOT / "engine/out/fpkg-target/x86_64-ps5-freebsd/release/libpbc_rust.a"
    print(f"[fpkg] {out} ({out.stat().st_size // 1024} Kio)")
    return out


def build_engine(jobs: int) -> Path:
    """Le moteur : démon autonome (payload ELF), avec l'appli web intégrée."""
    sh([sys.executable, ROOT / "engine/tools/embed_web.py"])
    srcs = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "engine/src").rglob("*.cpp"))
    units = [{"src": s, "std": "-std=c++20", "defines": [f'PBC_VERSION="{version()["version"]}"', f'PBC_UPDATE_URL="{UPDATE_URL}"']} for s in srcs]
    units.append({"src": "engine/third_party/self_update_sha256.c", "std": "-std=c11", "defines": []})
    # Paquets FPKG (docs/PLAN-paquets-fpkg.md) : le strict nécessaire pour lier notre bibliothèque Rust (voir le fichier).
    units.append({"src": "engine/src/images/fpkg_ps5_shim.c", "std": "-std=c11", "defines": []})
    # Lancement depuis un .pkg (docs/PLAN-lancement-kraken.md) : la retouche de app.db de Kraken Mount (SQLite du moteur).
    units.append({"src": "engine/third_party/kraken-mount/ps5/appdb.c", "std": "-std=c11", "defines": []})
    # BitTorrent (docs/ETUDE-bittorrent.md) : libtorrent-rasterbar 2.0.11 et le sous-ensemble de Boost qu'il utilise, figés
    # dans engine/third_party ; notre module engine/src/bt (interface sans exceptions pour le reste du moteur).
    lt_root = ROOT / "engine/third_party/libtorrent"
    lt_inc = ["-I", str(lt_root / "include"), "-I", str(lt_root / "deps/try_signal"), "-isystem", str(ROOT / "engine/third_party/boost"),
              "-include", str(ROOT / "engine/src/bt/ps5_compat.hpp")]
    lt_def = ["TORRENT_BUILDING_LIBRARY", "BOOST_ASIO_ENABLE_CANCELIO", "BOOST_ASIO_NO_DEPRECATED", "BOOST_EXCEPTION_DISABLE",
              "BOOST_ASIO_HAS_STD_CHRONO", "TORRENT_NO_DEPRECATE", "TORRENT_DISABLE_LOGGING", "TORRENT_USE_I2P=0", "NDEBUG", "PBC_WITH_BT"]
    cm = (lt_root / "CMakeLists.txt").read_text()
    lt_srcs = []
    for var, prefix in [("sources", "src/"), ("kademlia_sources", "src/kademlia/"), ("ed25519_sources", "src/ed25519/"), ("try_signal_sources", "deps/try_signal/")]:
        m = re.search(r"set\(" + var + r"\s+(.*?)\)", cm, re.S)
        lt_srcs += ["engine/third_party/libtorrent/" + prefix + w for w in m.group(1).split() if w.endswith((".cpp", ".c"))]
    lt_srcs.append("engine/third_party/libtorrent/src/pe_crypto.cpp")
    for u2 in lt_srcs:
        units.append({"src": u2, "std": "-std=c++17", "extra": ["-w", *lt_inc], "defines": lt_def, "exceptions": True})
    for u in units:
        if u["src"] == "engine/src/bt/bt.cpp":
            u["extra"] = lt_inc; u["defines"] = u["defines"] + lt_def; u["exceptions"] = True
    # Notre propre lanceur d'ELF (relance du moteur après une mise à jour, sans chargeur externe) : elfldr, GPL-3.0.
    for c in ("elfldr", "pt"):
        units.append({"src": f"engine/third_party/elfldr/{c}.c", "std": "-std=gnu11", "extra": ["-w"], "defines": ["ELFLDR_KEEP_PRIVILEGES"]})
    # Cœur de restauration des sauvegardes adapté de Garlic SaveMgr (GPL-3.0), compilé dans le moteur.
    units.append({"src": "engine/src/saves/garlic_core.c", "std": "-std=c11", "defines": ["_BSD_SOURCE"]})
    # SQLite (domaine public) : lecture seule des bases système ; ses avertissements ne sont pas les nôtres.
    units.append({"src": "engine/third_party/sqlite/sqlite3.c", "std": "-std=c11", "extra": ["-w"],
                  "defines": ["SQLITE_THREADSAFE=0", "SQLITE_OMIT_LOAD_EXTENSION", "SQLITE_DEFAULT_MEMSTATUS=0", "SQLITE_OMIT_DEPRECATED", "SQLITE_DQS=0"]})
    # BearSSL (MIT) : TLS du HTTPS (jaquettes, noms, nuage plus tard) ; autorités racines générées.
    bssl = ROOT / "engine/third_party/bearssl"
    for c in sorted((bssl / "src").rglob("*.c")) + [ROOT / "engine/third_party/bearssl_tas.c"]:
        units.append({"src": str(c.relative_to(ROOT)), "std": "-std=c11", "extra": ["-w", "-I", str(bssl / "inc"), "-I", str(bssl / "src")], "defines": []})
    # libwebp (BSD), décodeur seulement : jaquettes PS4 / PS5 en WebP.
    smb2 = ROOT / "engine/third_party/libsmb2"  # SMB 2 / 3 (LGPL-2.1)
    for c in sorted((smb2 / "lib").glob("*.c")):
        units.append({"src": str(c.relative_to(ROOT)), "std": "-std=gnu11", "extra": ["-w", "-include", "engine/third_party/libsmb2/pbc_smb2_config.h", "-Iengine/third_party/libsmb2/include", "-Iengine/third_party/libsmb2/include/smb2", "-Iengine/third_party/libsmb2/lib"], "defines": []})
    webp = ROOT / "engine/third_party/libwebp"
    for c in sorted((webp / "src").rglob("*.c")):
        units.append({"src": str(c.relative_to(ROOT)), "std": "-std=c11", "extra": ["-w", "-I", str(webp)], "defines": []})
    for z in sorted((ROOT / "engine/third_party/zlib").glob("*.c")):
        units.append({"src": str(z.relative_to(ROOT)), "std": "-std=c11", "defines": ["Z_SOLO", "NO_GZIP"]})
    units.append({"src": "engine/third_party/qrcodegen/qrcodegen.c", "std": "-std=c11", "extra": ["-w"], "defines": []})  # codes QR (Project Nayuki, MIT)
    # Images de jeux : code de calcul de PS5 Game Compressor v1.0.4 (juma-sayeh, GPL-3.0) tel quel,
    # relié au moteur par engine/src/images/gc_bridge.c (zlib Z_SOLO, niveau réglable, état de travail).
    gc = ROOT / "engine/third_party/game-compressor/src"
    for u in ["pfs_compress", "pfs_decompress", "pfs_repair", "pfs_block_pipeline", "pfs_validate_hash", "miniz_tinfl", "ampr_index"]:
        units.append({"src": str((gc / f"{u}.c").relative_to(ROOT)), "std": "-std=c11",  # -std=gnu11 ci-dessous (extensions BSD)
                      "extra": ["-w", "-std=gnu11", "-include", str(ROOT / "engine/src/images/gc_zlib_compat.h"), "-I", str(gc)],
                      "defines": ["Z_SOLO", "GC_PFSC_ZLIB_LEVEL=pbc_gc_zlib_level", "MINIZ_USE_UNALIGNED_LOADS_AND_STORES=1"]})
    units.append({"src": "engine/src/images/gc_bridge.c", "std": "-std=c11", "extra": ["-I", str(gc)], "defines": ["Z_SOLO"]})
    # Lecture seule des fichiers d'une image (.exfat / .ffpfsc) : inclut pfs_ampr_hotswap.c tel quel.
    units.append({"src": "engine/src/images/gc_image_files.c", "std": "-std=c11",
                  "extra": ["-w", "-std=gnu11", "-include", str(ROOT / "engine/src/images/gc_zlib_compat.h"), "-I", str(gc)],
                  "defines": ["Z_SOLO", "GC_PFSC_ZLIB_LEVEL=pbc_gc_zlib_level", "MINIZ_USE_UNALIGNED_LOADS_AND_STORES=1"]})
    units += smp_units()
    for u in units:
        if u["std"] == "-std=c++20":
            u["defines"] = u["defines"] + ["Z_SOLO"]
    objs = compile_all("engine", units, jobs, ["engine/src", "engine/third_party", "engine/third_party/zlib", "engine/third_party/stb", "engine/third_party/sqlite",
                                                "engine/third_party/bearssl/inc", "engine/third_party/libwebp", "engine/third_party/libsmb2/include"], ["-femulated-tls"])
    d = OUT / "engine"
    lib = SDK / "target/lib"
    fpkg = build_fpkg()
    sh([BIN / "ld.lld", "-pie", "--script", SDK / "ldscripts/elf_x86_64.x", "--eh-frame-hdr",
        "-z", "max-page-size=0x4000", "-mllvm", "-emulated-tls", "--hash-style=gnu", "--no-undefined",
        "--wrap=pthread_create",  # pile des fils choisie par le moteur (engine/src/core/thread_stack.cpp)
        f"-Map={d / 'link.map'}", "-L", lib, lib / "crt1.o", *objs, fpkg,
        # NOS libunwind / libc++abi / libc++ avant les bibliothèques du système : sinon __cxa_throw est pris dans
        # libSceLibcInternal, qui ne connaît pas nos tables, et la 1re exception (libtorrent) plante (essai du 16/11).
        "--start-group", "-lunwind", "-lc++abi", "-lc++", "-lc", "--end-group",
        "-lkernel_sys", "-lSceLibcInternal", "-lSceSystemService", "-lSceUserService", "-lSceNet", "-lSceFsInternalForVsh", "-lSceRegMgr",
        "-lSceAppInstUtil", "-lSceNotification",  # lancement depuis un .pkg (inscription, notifications de Kraken Mount)
        "-lSceIpmi",  # notre ShadowMount intégré (ShadowMountPlus : sm_usb, état des disques USB)
        "-o", d / "pbc-engine2.elf"])
    print(f"[engine] {d / 'pbc-engine2.elf'}")
    return d / "pbc-engine2.elf"


def build_app(jobs: int) -> Path:
    """L'appli télé (eboot.bin) : affiche l'état du moteur et lui envoie les commandes (aucun scan, aucune copie elle-même)."""
    srcs = ["app/src/gfx.cpp", "app/src/fonts.cpp", "app/src/i18n.cpp", "app/src/model.cpp", "app/src/ui.cpp", "app/src/ui_transfers.cpp", "app/src/ui_manage.cpp", "app/src/ui_synchro.cpp", "app/src/ui_downloads.cpp", "app/src/ui_where.cpp", "app/src/main_ps5.cpp", "app/src/audio.cpp",
            "app/src/demo_renderer.cpp", "app/src/pbck_ui_memory.cpp", "app/src/pbck_language.cpp", "engine/src/core/json.cpp",
            "app/src/app_crt.cpp", "app/src/heap.cpp"]  # notre réserve mémoire à la place du runtime commun
    extra = [d for d in os.environ.get("APP2_DEFINES", "").split(",") if d] + [f'PBC_VERSION="{version()["version"]}"']
    units = [{"src": s, "std": "-std=c++20", "defines": extra} for s in srcs]
    objs = compile_all("app", units, jobs, ["app/src", "engine/src", "app/src/generated", "engine/third_party/stb"], ["-femulated-tls"])
    d = OUT / "app"
    lib = SDK / "target/lib"
    sh([BIN / "ld.lld", "-m", "elf_x86_64", "-pie", "-T", ROOT / "app/ld/ps5-pie-r3.ld", "--eh-frame-hdr",
        f"-L{lib}", "--no-undefined", f"-Map={d / 'link.map'}", "-z", "max-page-size=0x4000",
        "-mllvm", "-emulated-tls", "--version-script", ROOT / "app/ld/app-symbols.map", "-e", "_start",
        "-o", d / "llvm-pie.elf", *objs, "--as-needed",
        lib / "libkernel.so", lib / "libc++.a", lib / "libc++abi.a", lib / "libunwind.a", lib / "libc.a",
        *(lib / f"lib{n}.so" for n in ["SceAudioOut", "SceLibcInternal", "SceNet", "SceNetCtl", "ScePad", "SceSystemService", "SceUserService", "SceVideoOut"]),
        "--as-needed", "--hash-style=gnu"])
    sh([NATIVE_TOOL, "link", "--in", d / "llvm-pie.elf", "--out", d / "eboot.elf", "--stub-dir", lib,
        "--module-sdk", "0x02000009", "--companion-sdk", "0x08050001", "--file-name", "eboot.elf"])
    sh([NATIVE_TOOL, "self", "--sign", "--in", d / "eboot.elf", "--out", d / "eboot.bin", "--magic", "0x1D3D154F"])
    print(f"[app] {d / 'eboot.bin'}")
    return d / "eboot.bin"


def make_logo() -> bytes:
    """Logo 128×128 (assets/logo.rgba) depuis app/artwork/icon-v1.png."""
    target = OUT / "package/logo.rgba"
    target.parent.mkdir(parents=True, exist_ok=True)
    sh([ROOT / ".venv-tools/bin/python", BS / "make_logo.py", target])
    return target.read_bytes()


def make_devices() -> dict:
    """Images d'appareils (assets/device-*.rgba) pour Destinations."""
    target = OUT / "package/devices"
    sh([ROOT / ".venv-tools/bin/python", BS / "make_devices.py", target])
    return {f"PPSA99950/assets/{p.name}": p.read_bytes() for p in sorted(target.glob("device-*.rgba"))}


def fallback_fonts() -> dict:
    """Polices de secours (assets/fonts/*.ttf, voir make_fonts.py) et leurs licences OFL, vérifiées."""
    d = ROOT / "app/assets/fonts"
    prov = json.loads((d / "provenance.json").read_text())
    out = {}
    for f in prov["files"]:
        data = (d / f["file"]).read_bytes()
        if hashlib.sha256(data).hexdigest() != f["sha256"]:
            raise SystemExit(f"{f['file']} : SHA-256 différent de provenance.json")
        out[f"PPSA99950/assets/fonts/{f['file']}"] = data
    for name in prov["licenses"]:
        out[f"PPSA99950/licenses/{name}"] = (d / name).read_bytes()
    return out


def make_sounds() -> dict:
    """Sons de l'app télé (assets/sound-*.wav, assets/music.wav), synthétisés par make_sounds.py (aucun échantillon tiers)."""
    target = OUT / "package/sounds"
    sh([sys.executable, BS / "make_sounds.py", target])
    return {f"PPSA99950/assets/{p.name}": p.read_bytes() for p in sorted(target.glob("*.wav"))}


# Fichiers fixes du paquet, au format d'une appli PS5 (Prospero) : sce_sys/ (icône, fiche, musique de la tuile),
# sce_module/libc.prx, police de l'écran, licences. Le paquet = ce dossier + ce qui est construit ici.
PACKAGE_DIR = ROOT / "app/package/PPSA99950"


def package() -> Path:
    """Le paquet installable : out/package/PPSA99950/ (dossier à copier dans /data/homebrew) et son zip."""
    import zipfile
    eboot, engine = OUT / "app/eboot.bin", OUT / "engine/pbc-engine2.elf"
    label = "PBCK L03 " + hashlib.sha256(eboot.read_bytes() + engine.read_bytes()).hexdigest()[:10].upper()
    v = version()
    zh = (ROOT / "engine/third_party/zlib/zlib.h").read_text(errors="replace")
    zlib_license = zh[zh.index("/* zlib.h"):zh.index("*/") + 2].encode()  # texte de licence officiel, tel quel
    files = {f"PPSA99950/{p.relative_to(PACKAGE_DIR)}": p.read_bytes() for p in sorted(PACKAGE_DIR.rglob("*")) if p.is_file() and p.name != ".DS_Store"}
    # Version de l'appli pour le système (format PlayStation, 2.98.1 → 02.098.001) : la console et les magasins
    # d'applis (Prospero Store) s'en servent pour voir une mise à jour.
    param = json.loads(files["PPSA99950/sce_sys/param.json"])
    major, minor, patch = (int(x) for x in v["version"].split("."))
    param["contentVersion"] = f"{major:02d}.{minor:03d}.{patch:03d}"
    files["PPSA99950/sce_sys/param.json"] = (json.dumps(param, ensure_ascii=False, indent=4) + "\n").encode()
    files.update({
        "PPSA99950/eboot.bin": eboot.read_bytes(),
        "PPSA99950/pbc-engine2.elf": engine.read_bytes(),
        "PPSA99950/licenses/zlib-LICENSE.txt": zlib_license,
        "PPSA99950/licenses/BearSSL-LICENSE.txt": (ROOT / "engine/third_party/bearssl/LICENSE.txt").read_bytes(),
        "PPSA99950/licenses/libwebp-COPYING.txt": (ROOT / "engine/third_party/libwebp/COPYING").read_bytes(),
        "PPSA99950/licenses/libsmb2-LGPL-2.1.txt": (ROOT / "engine/third_party/libsmb2/LICENCE-LGPL-2.1.txt").read_bytes(),
        "PPSA99950/licenses/credits.json": (ROOT / "engine/web/credits.json").read_bytes(),
        "PPSA99950/changelog.json": (BS / "changelog.json").read_bytes(),
        "PPSA99950/assets/logo.rgba": make_logo(),
        "PPSA99950/assets/switch-titles.tsv": (BS / "data/switch-titles.tsv").read_bytes(),
        "PPSA99950/assets/ps-titles.tsv": (BS / "data/ps-titles.tsv").read_bytes(),
        **make_devices(),
        **fallback_fonts(),
        **make_sounds(),
        # Traductions partagées avec le web (engine/web/i18n) : l'appli télé les lit dans /app0/assets/i18n.
        **{f"PPSA99950/assets/i18n/{p.name}": p.read_bytes() for p in sorted((ROOT / "engine/web/i18n").glob("*.json"))},
        "PPSA99950/version.json": json.dumps({"version": v["version"], "build": label, "notes": v.get("notes", [])}, ensure_ascii=False, indent=1).encode(),
    })
    d = OUT / "package"
    shutil.rmtree(d / "PPSA99950", ignore_errors=True)
    for name, data in files.items():
        (d / name).parent.mkdir(parents=True, exist_ok=True)
        (d / name).write_bytes(data)
    for exe in ("eboot.bin", "pbc-engine2.elf"):
        os.chmod(d / "PPSA99950" / exe, 0o755)
    target = d / "PPSA99950.zip"
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as z:
        for name in sorted(files):  # dates fixes : même contenu → même zip, même SHA-256 (catalogues, vérification)
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100755 if name.endswith(("eboot.bin", ".elf")) else 0o100644) << 16
            z.writestr(info, files[name])
    print(f"[paquet] {target}  {label}  version {v['version']}  ({len(files)} fichiers)")
    return target


def release() -> Path:
    """Ce qu'on publie pour une version, dans releases/<version>/ : PPSA99950.zip (le dossier de l'appli),
    ps5-backup-center-<version>-engine.elf (le moteur seul, pour le chargeur d'ELF) et SHA256SUMS.
    Le zip doit passer qa/scan_package.py (aucune donnée du propriétaire)."""
    v = version()["version"]
    zp = OUT / "package/PPSA99950.zip"
    with zipfile.ZipFile(zp) as zf:  # le paquet assemblé doit être celui de cette version (sinon : build.py all d'abord)
        inside = json.loads(zf.read("PPSA99950/version.json"))["version"]
        if inside != v:
            raise SystemExit(f"le paquet assemblé est en {inside}, pas en {v} : lancer d'abord build.py all")
        if zf.read("PPSA99950/pbc-engine2.elf") != (OUT / "engine/pbc-engine2.elf").read_bytes():
            raise SystemExit("le moteur du paquet n'est pas le dernier compilé : lancer d'abord build.py all")
    if subprocess.run([sys.executable, ROOT / "qa/scan_package.py", zp]).returncode:
        raise SystemExit("paquet refusé par qa/scan_package.py")
    # La télé et le web avancent ensemble : pas de version figée avec un texte télé absent du web (ni justifié).
    if subprocess.run([sys.executable, ROOT / "engine/tools/parity.py"]).returncode:
        raise SystemExit("parité télé/web incomplète (engine/tools/parity.py) : version non figée")
    d = ROOT / "releases" / v
    # Une version publiée ne change plus : ce qui est testé sur les consoles = ce qui est publié = ce que déclarent les
    # catalogues (Prospero Store, SHA256SUMS). Un paquet différent pour la même version est refusé : nouvelle version.
    if (d / "SHA256SUMS").exists():
        old = (d / "SHA256SUMS").read_text().split()[0]
        if old != hashlib.sha256(zp.read_bytes()).hexdigest():
            raise SystemExit(f"releases/{v} existe déjà avec un autre paquet ({old[:12]}…) : version figée, en faire une nouvelle")
        print(f"[version] releases/{v} : déjà figée, identique")
        return d
    d.mkdir(parents=True, exist_ok=True)
    elf = f"ps5-backup-center-{v}-engine.elf"
    shutil.copy2(zp, d / "PPSA99950.zip")
    shutil.copy2(OUT / "engine/pbc-engine2.elf", d / elf)
    sums = "".join(f"{hashlib.sha256((d / n).read_bytes()).hexdigest()}  {n}\n" for n in ("PPSA99950.zip", elf))
    (d / "SHA256SUMS").write_text(sums)
    print(f"[version] {d}\n{sums}", end="")
    return d


def public_build_script() -> str:
    """build.sh de la copie publique : les vraies commandes de compilation (moteur, appli) notées telles quelles,
    puis l'assemblage du dossier. Ni nos outils, ni nos générateurs : le dossier app/package/PPSA99950 publié
    contient déjà tout le reste, tout fait."""
    global RECORD
    RECORD = []
    try:
        build_engine(1)
        build_app(1)
    finally:
        cmds, RECORD = RECORD, None
    ver = version()["version"]
    subs = [(f"-ffile-prefix-map={ROOT}/=", "-ffile-prefix-map=$ROOT/="), (f"-ffile-prefix-map={Path.home()}/=", None),
            (str(NATIVE_TOOL), "$PS5_NATIVE_TOOL"), (str(SDK), "$SDK"), (str(BIN), "$LLVM"), (str(OUT), "$OUT"),
            (str(ROOT) + "/", ""), (sys.executable, "python3"), (f'PBC_VERSION="{ver}"', 'PBC_VERSION=\\"$VERSION\\"')]
    def arg(a: str):
        for old, new in subs:
            if a == old and new is None:
                return None
            if old in a:
                a = a.replace(old, new)
        if '"' in a and "$" not in a:
            return f"'{a}'"  # -DNOM="texte" : garder les guillemets pour le compilateur
        return f'"{a}"' if "$" in a or " " in a else a
    lines, dirs = [], set()
    for c in cmds:
        out = [x for x in (arg(a) for a in c) if x is not None]
        if "-o" in out:
            dirs.add(str(Path(out[out.index("-o") + 1].strip('"')).parent))
        lines.append(" ".join(out))
    return f"""#!/bin/sh
# Build PS5 Backup Center: engine (pbc-engine2.elf), TV app (eboot.bin), then the app folder out/PPSA99950/
# and out/PPSA99950.zip. Licence: GPL-3.0-or-later (LICENSE).
#
# Requirements (not included):
#   SDK              the free PS5 payload SDK (https://github.com/ps5-payload-dev/sdk)
#   LLVM             LLVM 18 bin directory (clang-18, ld.lld)
#   PS5_NATIVE_TOOL  ELF -> eboot.bin linker/signer (ps5-native-app-boilerplate, tooling/native)
set -e
cd "$(dirname "$0")"
ROOT=$(pwd)
: "${{SDK:?set SDK}}" "${{LLVM:?set LLVM}}" "${{PS5_NATIVE_TOOL:?set PS5_NATIVE_TOOL}}"
OUT=${{OUT:-$ROOT/out}}
VERSION=$(sed -n 's/.*"version": *"\\([^"]*\\)".*/\\1/p' app/package/PPSA99950/version.json | head -1)
mkdir -p {" ".join(sorted(f'"{d}"' for d in dirs))}
{chr(10).join(lines)}
rm -rf "$OUT/PPSA99950" && cp -R app/package/PPSA99950 "$OUT/PPSA99950"
cp "$OUT/app/eboot.bin" "$OUT/engine/pbc-engine2.elf" "$OUT/PPSA99950/"
(cd "$OUT" && rm -f PPSA99950.zip && zip -qrX PPSA99950.zip PPSA99950)
echo "$OUT/PPSA99950/  $OUT/PPSA99950.zip  (version $VERSION)"
"""


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("target", nargs="?", default="all", choices=["engine", "app", "package", "release", "all"])
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--clean", action="store_true")
    a = ap.parse_args()
    if a.clean:
        shutil.rmtree(OUT, ignore_errors=True)
    t0 = time.time()
    if a.target in ("engine", "all"):
        build_engine(a.j)
    if a.target in ("app", "all"):
        build_app(a.j)
    if a.target in ("package", "all"):
        package()
    if a.target == "release":
        release()
    print(f"terminé en {time.time() - t0:.1f} s")


if __name__ == "__main__":
    main()
