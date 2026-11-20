#!/usr/bin/env python3
"""Fabrique un module PS5 (.sprx) depuis des objets compilés avec notre kit (prospero-clang), pour les bibliothèques
glissées dans les jeux lancés depuis un .pkg (pgo_stub, dlc_emu — docs/PLAN-lancement-kraken.md).

Même chaîne que le Makefile d'ampr-emu-ng v0.4.1 (quer3q, GPL-3.0-or-later), dont les outils sont repris tels quels
dans ampr_tools/ ; seul le nom du module (« libSceAmpr » chez eux) devient un paramètre :
  objets → un objet relogeable → listes nm → NID (exports / imports libc et noyau) → renommage → bibliothèques
  d'import factices → liaison ld.lld (prx.script) → identité SCE → table de hachage → signature « fake » → vérification.

  build_prx.py --module libScePlayGo --export '^scePlayGo' --out out/x/libScePlayGo.sprx a.o b.o
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "ampr_tools"))
import generate_payload_exports as gpe  # noqa: E402
import prepare_prx_link_script as pls  # noqa: E402
import prx_hash_fix as phf  # noqa: E402
import stamp_payload_prx as spp  # noqa: E402

FSELF_VERSION = "0x02000001"
B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-"

# Fonctions fournies par le noyau (libkernel) ; IPMI (C++) par libSceIpmi ; tout le reste par la bibliothèque C.
KERNEL_EXACT = {"__error", "accept", "bind", "listen", "recv", "send", "setsockopt", "socket", "sched_yield",
                "usleep", "nanosleep", "getpid", "open", "close", "read", "write", "lseek", "fstat", "stat", "mmap",
                "munmap", "mprotect", "clock_gettime", "gettimeofday"}
KERNEL_PREFIX = ("sceKernel", "scePthread", "pthread_")


def sh(cmd: list[str], **kw) -> str:
    r = subprocess.run([str(c) for c in cmd], check=True, capture_output=True, text=True, **kw)
    return r.stdout


def read_pemd(path: Path) -> tuple[str, list[tuple[str, list[str]]]]:
    """Fichier .pemd (définition des exports) : nom du module et, par bibliothèque, ses fonctions (dans l'ordre)."""
    module, libs, cur = "", [], None
    for raw in path.read_text().splitlines():
        line = raw.split("#")[0].strip()
        if line.startswith("Module:"): module = line.split(":", 1)[1].strip()
        elif line.startswith("Library:"): cur = (line.split(":", 1)[1].split("{")[0].strip(), []); libs.append(cur)
        elif line.startswith("function:") and cur: cur[1].append(line.split(":", 1)[1].strip())
    return module, libs


def dynamic_block(imports: list[str], exports: list[str]) -> str:
    """Les entrées SCE de .dynamic : modules et bibliothèques importés (identifiants 1.. et 0..), le module, les
    bibliothèques exportées (à partir de len(imports) + 1), comme dans les modules du kit officiel."""
    q = []
    for i, _ in enumerate(imports):
        q += [(0x61000045, ((i + 1) << 48) | (1 << 40) | (1 << 32)), (0x61000049, (i << 48) | (1 << 32)), (0x61000019, (i << 48) | 9)]
    q += [(0x61000043, (1 << 40) | (1 << 32)), (0x61000011, 0), (0x61000041, 0)]
    for k, _ in enumerate(exports):
        lid = len(imports) + 1 + k
        q += [(0x61000047, (lid << 48) | (1 << 32)), (0x61000017, (lid << 48) | 1)]
    out = "        /* SCE entries supplied at link time; ordinary ELF entries follow. */\n"
    out += "".join(f"        QUAD (0x{t:08x}); QUAD (0x{v:016x})\n" for t, v in q)
    out += "        QUAD (0x6100003f); QUAD (SIZEOF (.dynsym))\n        QUAD (0x6100003d); QUAD (SIZEOF (.hash))\n        *(.dynamic)"
    return out


def stamp(prx: Path, imports: list[str], module: str, exports: list[str], filename: str) -> None:
    """Identité SCE et décalages des noms dans .dynstr (stamp_payload_prx d'ampr-emu-ng, généralisé)."""
    data = bytearray(prx.read_bytes())
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 0x10)[0] != 3:
        raise ValueError("expected an ELF64 ET_DYN image")
    sm = spp.sections(data)
    dynstr, dynamic = sm.get(".dynstr"), sm.get(".dynamic")
    off = {n: spp.dynstr_offset(data, dynstr[0], dynstr[1], n) for n in {*imports, module, *exports, filename}}
    needed = imp = exp = 0
    for e in range(dynamic[0], dynamic[0] + dynamic[1], dynamic[2]):
        tag, value = struct.unpack_from("<QQ", data, e)
        name = None
        if tag == spp.DT_SCE_NEEDED_MODULE: name = imports[needed]; needed += 1
        elif tag == spp.DT_SCE_IMPORT_LIB: name = imports[imp]; imp += 1
        elif tag == spp.DT_SCE_MODULE_INFO: name = module
        elif tag == spp.DT_SCE_EXPORT_LIB: name = exports[exp]; exp += 1
        elif tag == spp.DT_SCE_MODULE_FILENAME: name = filename
        if name is not None:
            struct.pack_into("<Q", data, e + 8, (value & 0xFFFFFFFF00000000) | off[name])
    if needed != len(imports) or imp != len(imports) or exp != len(exports):
        raise ValueError("unexpected SCE metadata layout")
    data[7] = 9
    data[8] = 2
    struct.pack_into("<H", data, 0x10, 0xFE18)
    prx.write_bytes(data)


def fix_hash(data: bytes) -> tuple[bytes, int]:
    """prx_hash_fix.fix_hash d'ampr-emu-ng, sans sa vérification « un seul groupe libSceAmpr »."""
    elf = phf.Elf64LE(data)
    hs, nbucket, nchain, buckets, chains = elf.hash_table()
    sb = phf.buckets_to_symbol_map(buckets, chains)
    groups = elf.export_groups()
    n = 0
    for sym in elf.dynsyms()[1:]:
        parts = sym.nid_parts
        if sym.st_shndx == 0 or parts is None: continue
        nid, lib_id, mod_id = parts
        g = groups.get((lib_id, mod_id))
        if g is None: continue
        sb[sym.index] = phf.elf_hash(g.canonical(nid)) % nbucket
        n += 1
    if n == 0: raise ValueError("aucun export SCE")
    nb, nc = phf.rebuild_hash(nbucket, nchain, sb)
    out = bytearray(data)
    words = [nbucket, nchain, *nb, *nc]
    struct.pack_into(f"<{len(words)}I", out, hs.offset, *words)
    return bytes(out), nbucket


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--module", help="nom du module (sinon : celui du .pemd)")
    ap.add_argument("--file", help="nom de fichier du module (défaut : <module>.prx)")
    ap.add_argument("--export", help="expression des noms exportés, une seule bibliothèque au nom du module (ex. ^scePlayGo)")
    ap.add_argument("--pemd", type=Path, help="définition des exports (bibliothèques et fonctions)")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("objects", nargs="+", type=Path)
    a = ap.parse_args()
    sdk = Path(os.environ["PS5_PAYLOAD_SDK"])
    llvm = Path(sh([os.environ["LLVM_CONFIG"], "--bindir"]).strip())
    cc, ld, nm, objcopy = sdk / "bin/prospero-clang", llvm / "ld.lld", llvm / "llvm-nm", llvm / "llvm-objcopy"
    combined_dir = a.out.parent / (a.out.stem + ".work")
    combined_dir.mkdir(parents=True, exist_ok=True)
    work = combined_dir
    combined = work / "combined.o"
    sh([ld, "-m", "elf_x86_64", "-r", "-o", combined, *a.objects])
    undefined = sorted(set(sh([nm, "-u", "-j", combined]).split()))
    defined = set(sh([nm, "-g", "-j", "--defined-only", combined]).split())

    # Exports : (bibliothèque, symbole, NID). Un nom de 11 caractères déjà en NID (ex. « AS45QoYHjc4 ») reste tel quel.
    if a.pemd:
        module, libs = read_pemd(a.pemd)
        module = a.module or module
    else:
        module = a.module
        rx = re.compile(a.export)
        libs = [(module, sorted(n for n in defined if rx.match(n)))]
    is_raw = lambda n: len(n) == 11 and all(c in B64 for c in n) and not n.startswith("sce")
    missing = [f for _, fs in libs for f in fs if f not in defined]
    if missing: raise SystemExit(f"exports absents des objets : {missing}")
    export_libs = [l for l, _ in libs]
    filename = a.file or module + ".prx"

    exported = {f for _, fs in libs for f in fs}
    imports = sorted((set(undefined) - exported - gpe.CRT_PROVIDED_SYMBOLS) | {"__cxa_finalize"})
    ipmi = [n for n in imports if n.startswith("_ZN4IPMI")]
    kernel = [n for n in imports if n in KERNEL_EXACT or n.startswith(KERNEL_PREFIX)]
    libc = [n for n in imports if n not in kernel and n not in ipmi]
    # Ordre des bibliothèques importées : celui d'ampr-emu-ng (C, noyau) ; avec IPMI, celui des modules de drakmor.
    order = [("libSceIpmi", ipmi), ("libkernel", kernel), ("libSceLibcInternal", libc)] if ipmi else [("libSceLibcInternal", libc), ("libkernel", kernel)]
    import_names = [n for n, _ in order]
    suffix = {n: f"#{B64[i]}#{B64[i + 1]}" for i, (n, _) in enumerate(order)}
    lib_suffix = {l: f"#{B64[len(order) + 1 + k]}#A" for k, l in enumerate(export_libs)}

    meta = (*import_names, module, *export_libs, filename)
    gpe.METADATA_SYMBOLS = tuple(dict.fromkeys(meta))
    nid = lambda n: n if is_raw(n) else gpe.name_to_nid(n)
    export_syms = [f"{nid(f)}{lib_suffix[l]}" for l, fs in libs for f in fs]
    (work / "exports.rsp").write_text("".join(f'"--export-dynamic-symbol={x}"\n' for x in export_syms) +
                                      "".join(f'"--export-dynamic-symbol={x}"\n' for x in gpe.METADATA_SYMBOLS))
    (work / "version.script").write_text("{\n  global:\n" + "".join(f'    "{x}";\n' for x in (*export_syms, *gpe.METADATA_SYMBOLS)) + "  local: *;\n};\n")
    # Une fonction exportée par plusieurs bibliothèques (ex. GetGameTrialsFlag) : renommée une fois, les autres noms
    # deviennent des alias ajoutés dans l'objet (même section, même adresse), comme le module officiel qui exporte la même
    # adresse deux fois.
    rename, aliases, first = [], [], {}
    for l, fs in libs:
        for f in fs:
            target = f"{nid(f)}{lib_suffix[l]}"
            if f in first: aliases.append((target, first[f]))
            else: first[f] = target; rename.append(f'"--redefine-sym={f}={target}"')
    for name, syms in order:
        rename += [f'"--redefine-sym={x}={gpe.name_to_nid(x)}{suffix[name]}"' for x in syms]
    (work / "rename.txt").write_text("\n".join(rename) + "\n")
    (work / "metadata.S").write_text(gpe.render_metadata_assembly())
    (work / "imports.txt").write_text("".join(f"{n}: {' '.join(s)}\n" for n, s in order))
    renamed = work / "renamed.o"
    sh([objcopy, f"@{work / 'rename.txt'}", combined, renamed])
    if aliases:
        objdump = llvm / "llvm-objdump"
        table = {}
        for line in sh([objdump, "--syms", renamed]).splitlines():
            parts = line.split()
            if len(parts) >= 5 and parts[-1]: table[parts[-1]] = (parts[0], parts[-3])  # adresse, section
        add = []
        for alias, target in aliases:
            addr, sect = table[target]
            add += ["--add-symbol", f"{alias}={sect}:0x{addr},global,function"]
        sh([objcopy, *add, renamed, renamed])
    aliases = []
    sos = []
    for name, syms in order:
        asm = work / f"{name}.S"
        asm.write_text(gpe.render_stub_assembly(syms, suffix[name]))
        so = work / f"{name}.prx.so"
        sh([cc, "-shared", "-nostdlib", "-nodefaultlibs", "-Wl,--hash-style=sysv", f"-Wl,-soname,{name}.prx", "-o", so, asm])
        sos.append(so)
    meta_o = work / "metadata.o"
    sh([cc, "-c", work / "metadata.S", "-o", meta_o])
    text = (HERE / "prx.script").read_text()
    for t, v in pls.PHDR_TYPES.items(): text = text.replace(t, v)
    assert text.count(pls.DYNAMIC_INPUT) == 1
    script = work / "prx.script"
    script.write_text(text.replace(pls.DYNAMIC_INPUT, dynamic_block(import_names, export_libs)))
    linked = work / "linked.prx"
    sh([ld, "-m", "elf_x86_64", "--shared", "--eh-frame-hdr", "-mllvm", "-emulated-tls", "-T", script, "--hash-style=sysv",
        "--build-id=sha1", "-z", "relro", "-z", "max-page-size=0x4000", "-z", "common-page-size=0x4000",
        f"--version-script={work / 'version.script'}", "--gc-sections", *aliases, "-o", linked,
        "--whole-archive", renamed, "--no-whole-archive", meta_o, "--no-as-needed", *sos, "--as-needed",
        f"@{work / 'exports.rsp'}"])
    stamp(linked, import_names, module, export_libs, filename)
    fixed, buckets = fix_hash(linked.read_bytes())
    prx = work / "module.prx"
    prx.write_bytes(fixed)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    sh([sys.executable, HERE / "ampr_tools/make_fself.py", prx, a.out, "--ptype", "fake", "--app-version", FSELF_VERSION, "--fw-version", FSELF_VERSION])
    print(f"{a.out} : module {module}, {len(export_syms)} export(s) dans {len(export_libs)} bibliothèque(s), imports "
          + ", ".join(f"{n} {len(s)}" for n, s in order) + f", hachage {buckets}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
