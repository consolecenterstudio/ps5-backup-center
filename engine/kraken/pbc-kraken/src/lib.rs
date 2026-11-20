//! Lancer un jeu depuis son .pkg (docs/PLAN-lancement-kraken.md) : le cache de démarrage et les index AMPR, construits
//! avec le code de Kraken Mount v0.3.0 (engine/third_party/kraken-mount, GPL-3.0-or-later), recopié et adapté par
//! build.rs (nos dossiers, nos bibliothèques). Ici : les petites fonctions qu'il attend de son programme, et notre façade C.
#![allow(dead_code, clippy::all)]

use std::ffi::{c_char, c_int, CStr};
use std::path::PathBuf;
use std::sync::OnceLock;

/// Le journal de Kraken Mount (`log!`) : renvoyé au moteur par une fonction qu'il donne (sinon, rien).
macro_rules! log {
    ($($arg:tt)*) => { $crate::log_line(&format!($($arg)*)) };
}

static LOG: OnceLock<extern "C" fn(*const c_char)> = OnceLock::new();
pub(crate) fn log_line(line: &str) {
    if let Some(f) = LOG.get() {
        if let Ok(c) = std::ffi::CString::new(format!("kraken: {line}")) { f(c.as_ptr()); }
    }
}

static ROOT: OnceLock<String> = OnceLock::new();
/// Un dossier sous la racine (/data/pbc-backup-center/kraken sur la console) ; calculé une fois, gardé.
pub(crate) fn dir_of(sub: &'static str) -> &'static str {
    static CACHES: OnceLock<String> = OnceLock::new();
    static AMPR: OnceLock<String> = OnceLock::new();
    let root = ROOT.get().map(String::as_str).unwrap_or("/data/pbc-backup-center/kraken");
    let cell = if sub == "caches" { &CACHES } else { &AMPR };
    cell.get_or_init(|| format!("{root}/{sub}")).as_str()
}

pub(crate) fn parse_version(v: &str) -> Option<(u32, u32, u32)> {
    let v = v.trim();
    let mut it = v.strip_prefix('v').unwrap_or(v).split('.').map(|p| p.parse::<u32>().ok());
    let r = (it.next()??, it.next()??, it.next()??);
    it.next().is_none().then_some(r)
}
pub(crate) fn newer(a: Option<&str>, b: Option<&str>) -> bool { a.and_then(parse_version) > b.and_then(parse_version) }

#[cfg(not(target_os = "freebsd"))]
mod register {
    /// FNV-1a 64, hex (comme Kraken Mount, src/register.rs).
    pub(crate) fn fnv1a(bytes: &[u8]) -> String {
        let h = bytes.iter().fold(0xcbf2_9ce4_8422_2325u64, |h, &b| (h ^ b as u64).wrapping_mul(0x0000_0100_0000_01b3));
        format!("{h:016x}")
    }
    /// Le démontage d'un titre se fait dans le moteur (C++), pas ici.
    pub(crate) fn unmount_title(_tid: &str) -> std::io::Result<()> { Ok(()) }
}
#[cfg(not(target_os = "freebsd"))]
mod storage {
    pub(crate) fn drive_of(_p: &std::path::Path) -> String { "/data".into() }
}
// Sur la console : le code de Kraken Mount lui-même (inscription, lancements, montages, disques).
#[cfg(target_os = "freebsd")]
pub mod sys { include!(concat!(env!("OUT_DIR"), "/sys.rs")); }
#[cfg(target_os = "freebsd")]
pub mod storage { include!(concat!(env!("OUT_DIR"), "/storage.rs")); }
#[cfg(target_os = "freebsd")]
pub mod register { include!(concat!(env!("OUT_DIR"), "/register.rs")); }
#[cfg(target_os = "freebsd")]
pub mod launch { include!(concat!(env!("OUT_DIR"), "/launch.rs")); }
mod i18n {
    pub(crate) fn title_tag() -> &'static str { "fr-FR" }
}

pub mod config { include!(concat!(env!("OUT_DIR"), "/config.rs")); }
pub mod ampr_index { include!(concat!(env!("OUT_DIR"), "/ampr_index.rs")); }
pub mod scan { include!(concat!(env!("OUT_DIR"), "/scan.rs")); }
pub mod cache { include!(concat!(env!("OUT_DIR"), "/cache.rs")); }

/// Notre ampr-emu-ng (compilé par build-system/build_injected.sh), posé dans le dossier ampr avant chaque construction.
const AMPR_SPRX: &[u8] = include_bytes!(env!("PBC_AMPR_SPRX"));

fn put(err: *mut c_char, len: usize, msg: &str) {
    if err.is_null() || len == 0 { return; }
    let b = msg.as_bytes();
    let n = b.len().min(len - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(b.as_ptr(), err as *mut u8, n);
        *err.add(n) = 0;
    }
}

/// La racine des dossiers (une seule fois, avant tout appel) et la fonction du journal (peut être nulle).
#[no_mangle]
pub extern "C" fn pbc_kraken_setup(root: *const c_char, log: Option<extern "C" fn(*const c_char)>) {
    if !root.is_null() {
        if let Ok(r) = unsafe { CStr::from_ptr(root) }.to_str() { let _ = ROOT.set(r.trim_end_matches('/').to_string()); }
    }
    if let Some(f) = log { let _ = LOG.set(f); }
}

/// Construit (ou refait) le cache de démarrage d'un .pkg : `<racine>/caches/<TID>/` qui deviendra /app0 au lancement.
/// Renvoie 0, sinon -1 et un message. Écrit le numéro du jeu dans `tid` (au moins 16 octets).
#[no_mangle]
pub extern "C" fn pbc_kraken_build(pkg: *const c_char, tid: *mut c_char, tid_len: usize, err: *mut c_char, err_len: usize) -> c_int {
    let r = std::panic::catch_unwind(|| -> Result<String, String> {
        let path = PathBuf::from(unsafe { CStr::from_ptr(pkg) }.to_str().map_err(|_| "chemin illisible")?);
        let size = std::fs::metadata(&path).map_err(|e| format!("{}: {e}", path.display()))?.len();
        let (title_id, title_name, version) = cache::peek_pkg(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        let ampr = cache::ampr_dir_path();
        std::fs::create_dir_all(ampr).map_err(|e| format!("{ampr}: {e}"))?;
        let dst = PathBuf::from(ampr).join("libSceAmpr.sprx");
        if std::fs::read(&dst).ok().as_deref() != Some(AMPR_SPRX) {
            std::fs::write(&dst, AMPR_SPRX).map_err(|e| format!("{}: {e}", dst.display()))?;
        }
        std::fs::create_dir_all(cache::caches_dir()).map_err(|e| format!("{}: {e}", cache::caches_dir()))?;
        let src = scan::Source { path, kind: scan::Kind::Pkg, title_id: title_id.clone(), title_name, version,
                                 drive: "/data".into(), size, backport: None };
        let mut s = config::Settings::default();
        s.inject_ampr = true;
        s.add_emulators = true;
        cache::build(&src, &s).map_err(|e| e.to_string())?;
        Ok(title_id)
    });
    match r {
        Ok(Ok(t)) => { put(tid, tid_len, &t); 0 }
        Ok(Err(e)) => { put(err, err_len, &e); -1 }
        Err(_) => { put(err, err_len, "erreur interne (panic) pendant la construction du cache"); -1 }
    }
}

// ---- Côté console : inscrire, surveiller les lancements (code de Kraken Mount) -------------------------------------

#[cfg(target_os = "freebsd")]
mod console {
    use super::*;
    use std::collections::HashSet;
    use std::sync::{Arc, Mutex};

    static TITLES: OnceLock<Arc<Mutex<HashSet<String>>>> = OnceLock::new();
    static RUNNING: OnceLock<Arc<Mutex<HashSet<String>>>> = OnceLock::new();

    fn cstr<'a>(p: *const c_char) -> Option<&'a str> { if p.is_null() { None } else { unsafe { CStr::from_ptr(p) }.to_str().ok() } }

    /// kstuff répond (le .sprx d'ampr-emu-ng a besoin de son support des signatures « fake ») : 1, sinon 0.
    #[no_mangle]
    pub extern "C" fn pbc_kraken_kstuff() -> c_int { crate::sys::kstuff_loaded() as c_int }

    /// Un processus de ce nom tourne (ex. « shadowmountplus.elf », « kraken-mount.elf ») : 1, sinon 0.
    #[no_mangle]
    pub extern "C" fn pbc_kraken_process(name: *const c_char) -> c_int {
        cstr(name).map_or(0, |n| crate::launch::find_pid(n).is_some() as c_int)
    }

    /// Le titre affiché sur l'écran d'accueil est-il une inscription à nous (pas une vraie installation) ? 1 oui, 0 non.
    #[no_mangle]
    pub extern "C" fn pbc_kraken_is_ours(tid: *const c_char) -> c_int {
        cstr(tid).map_or(0, |t| crate::register::is_ours(t).is_ok() as c_int)
    }

    /// Inscrit le jeu dont le cache est prêt (pbc_kraken_build) : sce_sys et appmeta copiés, nullfs du cache sur
    /// /system_ex/app/<TID>, installeur de la console. 0, sinon -1 et un message.
    #[no_mangle]
    pub extern "C" fn pbc_kraken_register(tid: *const c_char, err: *mut c_char, err_len: usize) -> c_int {
        let Some(t) = cstr(tid) else { put(err, err_len, "numéro de jeu illisible"); return -1 };
        let dir = crate::cache::dir(t);
        let r = std::panic::catch_unwind(|| crate::register::register(t, &dir, &dir.join("sce_sys"), None).map_err(|e| e.to_string()));
        match r {
            Ok(Ok(())) => 0,
            Ok(Err(e)) => { put(err, err_len, &e); -1 }
            Err(_) => { put(err, err_len, "erreur interne (panic) pendant l'inscription"); -1 }
        }
    }

    /// Démarre la surveillance des lancements (une fois) ; `tids` : les jeux .pkg à prendre en charge, séparés par « , ».
    #[no_mangle]
    pub extern "C" fn pbc_kraken_watch(tids: *const c_char) {
        let set: HashSet<String> = cstr(tids).unwrap_or("").split(',').map(str::trim).filter(|s| !s.is_empty()).map(String::from).collect();
        let titles = TITLES.get_or_init(|| Arc::new(Mutex::new(HashSet::new()))).clone();
        *titles.lock().unwrap_or_else(|e| e.into_inner()) = set;
        // Le fil de surveillance est créé par le moteur (C++), qui appelle pbc_kraken_watch_loop : std::thread::spawn
        // de Rust échoue sur la PS5 (panique, donc arrêt du moteur — vu le 19/11 au premier jeu .pkg inscrit).
        let _ = RUNNING.get_or_init(|| Arc::new(Mutex::new(HashSet::new())));
    }

    /// La boucle de surveillance des lancements (bloquante) : appelée une fois, dans un fil du moteur.
    #[no_mangle]
    pub extern "C" fn pbc_kraken_watch_loop() {
        let titles = TITLES.get_or_init(|| Arc::new(Mutex::new(HashSet::new()))).clone();
        let running = RUNNING.get_or_init(|| Arc::new(Mutex::new(HashSet::new()))).clone();
        crate::launch::watch(titles, running);
    }

    /// Les jeux .pkg en cours (séparés par « , ») dans `out`.
    #[no_mangle]
    pub extern "C" fn pbc_kraken_running(out: *mut c_char, len: usize) {
        let s = RUNNING.get().map(|r| { let mut v: Vec<String> = r.lock().unwrap_or_else(|e| e.into_inner()).iter().cloned().collect(); v.sort(); v.join(",") }).unwrap_or_default();
        put(out, len, &s);
    }
}
