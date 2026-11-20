// Les fichiers de Kraken Mount v0.3.0 (engine/third_party/kraken-mount/src, copiés tels quels) sont recopiés ici dans
// OUT_DIR avec quatre changements, listés pour la provenance :
//   1. dossiers : /data/krakenmount/{caches,ampr} → /data/pbc-backup-center/kraken/{caches,ampr} (jamais ceux de Kraken) ;
//   2. bibliothèques embarquées : les nôtres, compilées par build-system/build_injected.sh (out/injected), au lieu de
//      leurs binaires (retirés) — pour l'instant pgo_stub seul (dlc_emu pas encore compilé) ;
//   3. versions : celles de nos copies ;
//   4. commentaires de module (//!) retirés (include! ne les accepte pas).
use std::{env, fs, path::PathBuf};

fn main() {
    let root = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap()).join("../../..");
    let src = root.join("engine/third_party/kraken-mount/src");
    let injected = env::var("PBC_INJECTED").map(PathBuf::from).unwrap_or_else(|_| root.join("out/injected"));
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    // Sur la console, nos vraies bibliothèques (build-system/build_injected.sh) ; sur le Mac (tests), des fichiers
    // d'essai si elles ne sont pas encore construites — jamais livrés.
    let console = env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("freebsd");
    const LIBS: [&str; 5] = ["libSceAmpr.sprx", "libScePlayGo.sprx", "libSceAppContent.sprx", "libSceNpEntitlementAccess.sprx", "libSceGameUpdate.sprx"];
    let have = LIBS.iter().all(|n| injected.join(n).is_file());
    assert!(have || !console, "{} incomplet : lancer build-system/build_injected.sh", injected.display());
    let lib = |n: &str| -> PathBuf {
        if have { return injected.join(n); }
        let p = out.join(format!("essai-{n}"));
        fs::write(&p, format!("essai {n} (Mac seulement)")).unwrap();
        p
    };
    let (ampr, pgo, appc, npea, gupd) = (lib(LIBS[0]), lib(LIBS[1]), lib(LIBS[2]), lib(LIBS[3]), lib(LIBS[4]));
    println!("cargo:rustc-env=PBC_AMPR_SPRX={}", ampr.display());
    println!("cargo:rerun-if-changed={}", ampr.display());
    for name in ["cache.rs", "ampr_index.rs", "scan.rs", "config.rs"] {
        let p = src.join(name);
        println!("cargo:rerun-if-changed={}", p.display());
        let mut s: String = fs::read_to_string(&p).unwrap().lines().filter(|l| !l.starts_with("//!")).collect::<Vec<_>>().join("\n");
        // Dossiers : une racine réglable (crate::root(), /data/pbc-backup-center/kraken sur la console) au lieu des
        // constantes de Kraken Mount ; chaque usage de CACHES / AMPR_DIR devient un appel.
        s = s.replace("pub const CACHES: &str = \"/data/krakenmount/caches\";", "pub fn caches_dir() -> &'static str { crate::dir_of(\"caches\") }")
             .replace("pub const AMPR_DIR: &str = \"/data/krakenmount/ampr\";", "pub fn ampr_dir_path() -> &'static str { crate::dir_of(\"ampr\") }");
        for (from, to) in [
            ("crate::cache::CACHES", "crate::cache::caches_dir()"),
            ("Path::new(CACHES)", "Path::new(caches_dir())"),
            ("fs::read_dir(CACHES)", "fs::read_dir(caches_dir())"),
            ("fs::read_dir(AMPR_DIR)", "fs::read_dir(ampr_dir_path())"),
            ("Path::new(AMPR_DIR)", "Path::new(ampr_dir_path())"),
            ("format!(\"{AMPR_DIR}, inject_ampr", "format!(\"{}, inject_ampr"),
            ("or the bundled emulators changed\"))", "or the bundled emulators changed\", ampr_dir_path()))"),
        ] {
            s = s.replace(from, to);
        }
        for w in ["CACHES", "AMPR_DIR"] {
            assert!(!s.contains(&format!("({w})")) && !s.contains(&format!("{{{w}}}")) && !s.contains(&format!("::{w}")), "{name} : {w} encore utilisé");
        }
        if name == "config.rs" { s = s.replace("/data/krakenmount", "/data/pbc-backup-center/kraken"); }  // textes d'aide, jamais lus
        let code_left = s.lines().filter(|l| !l.trim_start().starts_with("//")).any(|l| l.contains("/data/krakenmount"));
        assert!(!code_left, "{name} : un chemin de Kraken Mount reste dans le code");
        if name == "cache.rs" {
            let a = s.find("const EMUS: [(&str, &[u8], &str, bool); 4] = [").expect("EMUS");
            let b = a + s[a..].find("];").expect("EMUS end") + 2;
            let inc = |p: &PathBuf| format!("include_bytes!({:?})", p.display().to_string());
            s.replace_range(a..b, &format!(
                "const EMUS: [(&str, &[u8], &str, bool); 4] = [\n    (\"libSceAppContent.sprx\", {}, \"dlc_emu\", true),\n    (\"libSceNpEntitlementAccess.sprx\", {}, \"dlc_emu\", true),\n    (\"libSceGameUpdate.sprx\", {}, \"dlc_emu\", true),\n    (\"libScePlayGo.sprx\", {}, \"pgo_stub\", false),\n];",
                inc(&appc), inc(&npea), inc(&gupd), inc(&pgo)));
            s = s.replace("include_str!(\"../vendor/dlc_emu/VERSION\")", "\"0.3\"")
                 .replace("include_str!(\"../vendor/pgo_stub/VERSION\")", "\"0.6\"");
            assert!(!s.contains("include_bytes!(\"../vendor"), "binaire de Kraken encore référencé");
        }
        fs::write(out.join(name), s).unwrap();
    }
    // Côté console (lib.rs les compile seulement pour la PS5) : inscription, surveillance des lancements, montages.
    //   5. sys.rs : son journal passe par le nôtre (crate::log_line), sa macro log! retirée (lib.rs a la sienne) ;
    //   6. leurs fichiers à eux (journal, icône de notification, tuile) : sous notre dossier.
    for name in ["register.rs", "launch.rs", "sys.rs", "storage.rs"] {
        let p = src.join(name);
        println!("cargo:rerun-if-changed={}", p.display());
        let mut s: String = fs::read_to_string(&p).unwrap().lines().filter(|l| !l.starts_with("//!")).collect::<Vec<_>>().join("\n");
        if name == "sys.rs" {
            let a = s.find("#[macro_export]").expect("macro log");
            let b = a + s[a..].find("\n}\n").expect("fin de la macro") + 3;
            s.replace_range(a..b, "");
            s = s.replace("pub fn log_line(line: &str) {", "pub fn log_line(line: &str) { crate::log_line(line); }\npub fn log_line_file(line: &str) {");
        }
        s = s.replace("/user/data/krakenmount", "/user/data/pbc-backup-center/kraken").replace("/data/krakenmount", "/data/pbc-backup-center/kraken");
        // Chemins relatifs à leur fichier d'origine (icône de leur tuile / notification, jamais affichée par nous).
        s = s.replace("include_bytes!(\"../ps5/", &format!("include_bytes!(\"{}/ps5/", src.join("..").display()));
        fs::write(out.join(name), s).unwrap();
    }
    for p in [&pgo, &appc, &npea, &gupd] { println!("cargo:rerun-if-changed={}", p.display()); }
    println!("cargo:rerun-if-env-changed=PBC_INJECTED");
}
