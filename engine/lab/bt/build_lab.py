#!/usr/bin/env python3
"""Essai BitTorrent (étape 0, docs/ETUDE-bittorrent.md) : compile libtorrent-rasterbar avec NOTRE chaîne (sans CMake,
sans rien installer) et les deux programmes d'essai.

  build_lab.py host   → out/bt-lab/host/bt_seed     (le semeur, sur le Mac)
  build_lab.py ps5    → out/bt-lab/ps5/bt-lab.elf   (l'ELF d'essai, lancé à côté de l'appli)

Sources figées (vérifiées par leur empreinte) dans work/bt/src (ignoré par git) :
  libtorrent-rasterbar-2.0.11.tar.gz  sha256 f0db58580f4f29ade6cc40fa4ba80e2c9a70c90265cd77332d3cdec37ecf1e6d
  boost_1_84_0.tar.bz2                sha256 cc4b893acf645c9d4b698e9a0f08ca8846aa5d6c68275c14c3e7949c24109454
libtorrent et ses deux programmes sont compilés AVEC exceptions et RTTI (ils en ont besoin) ; le moteur, lui, reste
sans (le jour où on l'intègre : derrière une interface C, voir l'étude).
"""
import concurrent.futures, hashlib, os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "work/bt/src"
LT = SRC / "libtorrent-rasterbar-2.0.11"
BOOST = SRC / "boost_1_84_0"
LAB = ROOT / "engine/lab/bt"
OUT = ROOT / "out/bt-lab"
PINS = {"libtorrent-rasterbar-2.0.11.tar.gz": "f0db58580f4f29ade6cc40fa4ba80e2c9a70c90265cd77332d3cdec37ecf1e6d",
        "boost_1_84_0.tar.bz2": "cc4b893acf645c9d4b698e9a0f08ca8846aa5d6c68275c14c3e7949c24109454"}
TC = ROOT / "toolchain/.local/toolchain"
BIN = TC / "llvm@18/18.1.8/bin"
SDK = ROOT / "toolchain/.deps/native/ps5-payload-sdk"
ENV = dict(os.environ, DYLD_LIBRARY_PATH=str(TC / "zstd/1.5.7_1/lib"))

DEFINES = ["TORRENT_BUILDING_LIBRARY", "BOOST_ASIO_ENABLE_CANCELIO", "BOOST_ASIO_NO_DEPRECATED", "BOOST_EXCEPTION_DISABLE",
           "BOOST_ASIO_HAS_STD_CHRONO", "TORRENT_NO_DEPRECATE", "TORRENT_DISABLE_LOGGING", "TORRENT_USE_I2P=0", "NDEBUG"]


def check_pins() -> None:
    for name, want in PINS.items():
        got = hashlib.sha256((SRC / name).read_bytes()).hexdigest()
        if got != want:
            sys.exit(f"empreinte inattendue pour {name} : {got}")
    if not LT.is_dir():
        subprocess.check_call(["tar", "xzf", SRC / "libtorrent-rasterbar-2.0.11.tar.gz"], cwd=SRC)
    if not (BOOST / "boost").is_dir():
        subprocess.check_call(["tar", "xjf", SRC / "boost_1_84_0.tar.bz2", "boost_1_84_0/boost"], cwd=SRC)


def lt_sources() -> list[Path]:
    """Les mêmes listes que le CMakeLists.txt de libtorrent (sources, kademlia, ed25519, try_signal)."""
    cm = (LT / "CMakeLists.txt").read_text()
    out = []
    for var, prefix in [("sources", "src/"), ("kademlia_sources", "src/kademlia/"), ("ed25519_sources", "src/ed25519/"), ("try_signal_sources", "deps/try_signal/")]:
        m = re.search(r"set\(" + var + r"\s+(.*?)\)", cm, re.S)
        out += [LT / (prefix + w) for w in m.group(1).split() if w.endswith((".cpp", ".c"))]
    out.append(LT / "src/pe_crypto.cpp")  # ajouté à part par le CMakeLists quand le chiffrement du protocole est actif
    return out


def flags(target: str) -> tuple[list[str], list[str]]:
    inc = ["-I", str(LT / "include"), "-I", str(LT / "deps/try_signal"), "-isystem", str(BOOST)]
    d = [f"-D{x}" for x in DEFINES]
    if target == "host":
        return ["clang++"], ["-std=c++17", "-O2", "-w", *d, *inc]
    common = ["-target", "x86_64-sie-ps5", "-fvisibility-nodllstorageclass=default", "-isysroot", str(SDK),
              "-isystem", str(SDK / "target/include/c++/v1"), "-isystem", str(SDK / "target/include"),
              "-fno-stack-protector", "-fno-plt", "-O2", "-w", "-DPBCK_PS5=1", "-femulated-tls", "-fexceptions", "-frtti",
              f"-ffile-prefix-map={ROOT}/=", f"-ffile-prefix-map={Path.home()}/=",
              "-include", str(LAB / "ps5_compat.hpp")]  # getrandom… (écrit par nous, sans toucher aux sources de libtorrent)
    return [str(BIN / "clang++")], ["-std=c++17", *common, *d, *inc]


def compile_all(target: str, srcs: list[Path], objdir: Path) -> list[Path]:
    cxx, fl = flags(target)
    objdir.mkdir(parents=True, exist_ok=True)

    def one(src: Path) -> Path:
        obj = objdir / (src.parent.name + "__" + src.stem + ".o")
        if obj.exists() and obj.stat().st_mtime > src.stat().st_mtime:
            return obj
        r = subprocess.run([*cxx, *fl, "-c", str(src), "-o", str(obj)], env=ENV, capture_output=True, text=True)
        if r.returncode:
            errs = [l for l in r.stderr.splitlines() if "error:" in l][:4]
            print(f"ÉCHEC {src.name} : " + " | ".join(errs), flush=True)
            return None
        return obj

    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 8) as ex:
        objs = list(ex.map(one, srcs))
    if None in objs:
        raise SystemExit(f"{objs.count(None)} fichier(s) en échec")
    return objs


def link(cmd: list[str]) -> None:
    r = subprocess.run(cmd, env=ENV, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("échec du lien :\n" + "\n".join(l for l in r.stderr.splitlines() if "undefined" in l or "error" in l)[:6000])


def main() -> None:
    target = sys.argv[1] if len(sys.argv) > 1 else "host"
    check_pins()
    objs = compile_all(target, lt_sources(), OUT / target / "obj")
    print(f"[{target}] libtorrent : {len(objs)} fichiers compilés")
    d = OUT / target
    if target == "host":
        main_obj = compile_all("host", [LAB / "bt_seed.cpp"], d / "obj-lab")
        link(["clang++", "-o", str(d / "bt_seed"), *map(str, main_obj), *map(str, objs), "-framework", "CoreFoundation", "-framework", "SystemConfiguration"])
        print(f"[host] {d / 'bt_seed'}")
        return
    # Même pile des fils que le moteur (512 Ko) : engine/src/core/thread_stack.cpp, par --wrap=pthread_create.
    main_obj = compile_all("ps5", [LAB / "bt_lab.cpp", LAB / "ps5_compat.cpp", ROOT / "engine/src/core/thread_stack.cpp"], d / "obj-lab")
    lib = SDK / "target/lib"
    link([str(BIN / "ld.lld"), "-pie", "--script", str(SDK / "ldscripts/elf_x86_64.x"), "--eh-frame-hdr",
                           "-z", "max-page-size=0x4000", "-mllvm", "-emulated-tls", "--hash-style=gnu", "--no-undefined",
                           "--wrap=pthread_create", "-L", str(lib), str(lib / "crt1.o"), *map(str, main_obj), *map(str, objs),
                           # Ordre du SDK : NOS libunwind / libc++abi / libc++ d'abord. Sinon __cxa_throw et le dérouleur sont pris
                           # dans libSceLibcInternal (le système), qui ne connaît pas les tables de l'ELF : la 1re exception plante
                           # (vu sur la PS5-01, 16/11 : SIGSEGV dans .gcc_except_table).
                           "-lunwind", "-lc++abi", "-lc++", "-lc", "-lkernel_sys", "-lSceLibcInternal", "-lSceSystemService", "-lSceNet",
                           "-o", str(d / "bt-lab.elf")])
    print(f"[ps5] {d / 'bt-lab.elf'} ({(d / 'bt-lab.elf').stat().st_size / 1e6:.1f} Mo)")


if __name__ == "__main__":
    main()
