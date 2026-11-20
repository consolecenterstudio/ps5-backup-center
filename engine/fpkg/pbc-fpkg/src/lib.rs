//! Façade C de PS5 Backup Center au-dessus de `ps5upload-fpkg` (Dump Forge / PS5Upload, GPL-3.0-or-later, copié tel
//! quel dans engine/third_party/fpkg) : créer un paquet FPKG `.pkg` (Kraken) depuis un jeu en dossier ou une image
//! (.exfat, .ffpkg, .ffpfsc), le vérifier. Appelé par le moteur (engine/src/images/fpkg_bridge.hpp), comme gc_bridge.c
//! pour Game Compressor : une bibliothèque liée au moteur, aucun processus ni port.
//!
//! Règles : le paquet s'écrit dans `<sortie>.part` puis n'est renommé qu'une fois relu et vérifié ; une annulation ou
//! une erreur ne laisse rien ; la source n'est jamais modifiée. Une panique est rattrapée (le moteur continue).

use std::ffi::{c_char, c_void, CStr};
use std::path::{Path, PathBuf};
use std::sync::atomic::AtomicBool;

use ps5upload_fpkg::build::{self, BuildControl, BuildRequest, Stage};
use ps5upload_fpkg::kraken::Level;

/// Avancement : (données, étape, fait, total). Étapes : 1 lecture et contrôles, 2 plan, 3 compression, 4 écriture,
/// 5 vérification. `fait` / `total` en octets quand l'étape les connaît (0 / 0 sinon).
pub type Progress = Option<extern "C" fn(ud: *mut c_void, stage: u32, done: u64, total: u64)>;

pub const PBC_FPKG_OK: i32 = 0;
pub const PBC_FPKG_CANCELLED: i32 = 1;
pub const PBC_FPKG_ERROR: i32 = 2;

fn stage_code(s: Stage) -> u32 {
    match s {
        Stage::Check => 1,
        Stage::Plan => 2,
        Stage::Compress => 3,
        Stage::Write => 4,
        Stage::Verify => 5,
    }
}

fn level_of(code: u32) -> Level {
    match code {
        0 => Level::Stored,
        1 => Level::Fast,
        3 => Level::Smallest,
        _ => Level::Balanced,
    }
}

/// Copie `msg` (UTF-8, coupé proprement) dans le tampon C, terminé par un zéro.
unsafe fn put(buf: *mut c_char, len: usize, msg: &str) {
    if buf.is_null() || len == 0 {
        return;
    }
    let mut n = msg.len().min(len - 1);
    while n > 0 && !msg.is_char_boundary(n) {
        n -= 1;
    }
    std::ptr::copy_nonoverlapping(msg.as_ptr(), buf as *mut u8, n);
    *buf.add(n) = 0;
}

unsafe fn path_arg(p: *const c_char) -> Option<PathBuf> {
    if p.is_null() {
        return None;
    }
    CStr::from_ptr(p).to_str().ok().map(PathBuf::from)
}

fn is_cancel(e: &ps5upload_fpkg::Error) -> bool {
    matches!(e, ps5upload_fpkg::Error::Cancelled)
}

/// Le fichier du paquet, pour le code tiers. Ses « copies » (relecture, tampon Kraken) ROUVRENT le fichier au lieu de
/// dupliquer le descripteur : Rust duplique avec fcntl(F_DUPFD_CLOEXEC), que la PS5 refuse (EOPNOTSUPP, essai du 18/11
/// sur la PS5-02). Le code tiers se positionne avant chaque lecture ou écriture : une position propre à chaque copie suffit.
struct Out {
    f: std::fs::File,
    path: PathBuf,
}
impl std::io::Read for Out {
    fn read(&mut self, b: &mut [u8]) -> std::io::Result<usize> { self.f.read(b) }
}
impl std::io::Write for Out {
    fn write(&mut self, b: &[u8]) -> std::io::Result<usize> { self.f.write(b) }
    fn flush(&mut self) -> std::io::Result<()> { self.f.flush() }
}
impl std::io::Seek for Out {
    fn seek(&mut self, p: std::io::SeekFrom) -> std::io::Result<u64> { self.f.seek(p) }
}
impl ps5upload_fpkg::PkgOut for Out {
    fn try_clone_out(&self) -> std::io::Result<Box<dyn ps5upload_fpkg::PkgOut>> {
        let f = std::fs::OpenOptions::new().read(true).write(true).open(&self.path)?;
        Ok(Box::new(Out { f, path: self.path.clone() }))
    }
    fn out_len(&self) -> std::io::Result<u64> { Ok(self.f.metadata()?.len()) }
    fn sync_out(&self) -> std::io::Result<()> { self.f.sync_all() }
}

struct Cb {
    f: Progress,
    ud: *mut c_void,
}
unsafe impl Send for Cb {}
impl Cb {
    fn call(&self, stage: u32, done: u64, total: u64) {
        if let Some(f) = self.f {
            f(self.ud, stage, done, total);
        }
    }
}

fn build_one(src: &Path, out: &Path, level: Level, threads: usize, cb: &Cb, cancel: Option<&AtomicBool>) -> Result<String, (i32, String)> {
    if out.exists() {
        return Err((PBC_FPKG_ERROR, format!("{} existe déjà", out.display())));
    }
    let part = PathBuf::from(format!("{}.part", out.display()));
    let _ = std::fs::remove_file(&part);  // un reste d'un essai coupé : c'est le nôtre
    let mut tree = ps5upload_fpkg::source::open(src).map_err(|e| (PBC_FPKG_ERROR, format!("source illisible : {e}")))?;
    let mut req = BuildRequest::production(src, out.parent().unwrap_or(Path::new(".")));
    req.level = level;
    req.kraken = true;
    req.threads = if threads == 0 { None } else { Some(threads) };
    let stage_now = std::cell::Cell::new(0u32);
    let mut on_stage = |s: Stage| {
        stage_now.set(stage_code(s));
        cb.call(stage_code(s), 0, 0);
    };
    let mut on_bytes = |d: u64, t: u64| cb.call(stage_now.get().max(3), d, t);
    let mut on_files = |s: Stage, d: u64, t: u64| cb.call(stage_code(s), d, t);
    let mut control = BuildControl::default();
    control.cancel = cancel;
    control.stage = Some(&mut on_stage);
    control.bytes = Some(&mut on_bytes);
    control.files = Some(&mut on_files);
    let fail = |e: ps5upload_fpkg::Error| if is_cancel(&e) { (PBC_FPKG_CANCELLED, "annulé".to_string()) } else { (PBC_FPKG_ERROR, e.to_string()) };
    let prepared = build::prepare(tree.as_mut(), &req, &mut control).map_err(fail)?;
    let f = std::fs::OpenOptions::new().read(true).write(true).create_new(true).open(&part)
        .map_err(|e| (PBC_FPKG_ERROR, format!("{} : {e}", part.display())))?;
    let mut file = Out { f, path: part.clone() };
    let res = build::write_package(&prepared, tree.as_mut(), &mut file, &part, &mut |_| {}, &mut control);
    drop(file);
    match res {
        Ok(report) => {
            std::fs::rename(&part, out).map_err(|e| {
                let _ = std::fs::remove_file(&part);
                (PBC_FPKG_ERROR, format!("renommage : {e}"))
            })?;
            let warn: Vec<String> = report.warnings.iter().map(|w| w.replace('"', "'")).collect();
            Ok(format!("{{\"size\":{},\"content_id\":\"{}\",\"warnings\":[{}]}}", report.size, report.content_id,
                       warn.iter().map(|w| format!("\"{w}\"")).collect::<Vec<_>>().join(",")))
        }
        Err(e) => {
            let _ = std::fs::remove_file(&part);
            Err(fail(e))
        }
    }
}

/// Créer `out` (un `.pkg`) depuis `src` (dossier de jeu ou image). `level` : 0 sans compression (Stored), 1 Fast,
/// 2 Balanced, 3 Smallest. `threads` : 0 = tous les cœurs. `cancel` : un booléen atomique (nul : pas d'annulation).
/// Retour : 0 fait (`info` reçoit {"size","content_id","warnings"}), 1 annulé, 2 erreur (`info` reçoit le message).
#[no_mangle]
pub unsafe extern "C" fn pbc_fpkg_build(src: *const c_char, out: *const c_char, level: u32, threads: u32, progress: Progress,
                                        ud: *mut c_void, cancel: *const AtomicBool, info: *mut c_char, info_len: usize) -> i32 {
    let (Some(src), Some(out)) = (path_arg(src), path_arg(out)) else {
        put(info, info_len, "chemin invalide");
        return PBC_FPKG_ERROR;
    };
    let cb = Cb { f: progress, ud };
    let cancel = cancel.as_ref();
    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| build_one(&src, &out, level_of(level), threads as usize, &cb, cancel)));
    match r {
        Ok(Ok(j)) => { put(info, info_len, &j); PBC_FPKG_OK }
        Ok(Err((code, m))) => { put(info, info_len, &m); code }
        Err(_) => {
            let _ = std::fs::remove_file(format!("{}.part", out.display()));
            put(info, info_len, "erreur interne pendant la création du paquet");
            PBC_FPKG_ERROR
        }
    }
}

/// Relire et vérifier tout un `.pkg` (chaque bloc, les empreintes). Retour : 0 bon, 1 annulé, 2 mauvais ou illisible
/// (`info` reçoit la raison).
#[no_mangle]
pub unsafe extern "C" fn pbc_fpkg_verify(path: *const c_char, progress: Progress, ud: *mut c_void, cancel: *const AtomicBool,
                                         info: *mut c_char, info_len: usize) -> i32 {
    let Some(path) = path_arg(path) else {
        put(info, info_len, "chemin invalide");
        return PBC_FPKG_ERROR;
    };
    let cb = Cb { f: progress, ud };
    let cancel = cancel.as_ref();
    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        ps5upload_fpkg::verify::verify_streaming_controlled(&path, ps5upload_fpkg::crypto::DEFAULT_PASSCODE, &mut |d, t| cb.call(5, d, t), cancel)
    }));
    match r {
        Ok(Ok(rep)) if rep.ok() => { put(info, info_len, "ok"); PBC_FPKG_OK }
        Ok(Ok(rep)) => { put(info, info_len, &format!("paquet abîmé : {rep:?}").chars().take(900).collect::<String>()); PBC_FPKG_ERROR }
        Ok(Err(e)) if is_cancel(&e) => { put(info, info_len, "annulé"); PBC_FPKG_CANCELLED }
        Ok(Err(e)) => { put(info, info_len, &e.to_string()); PBC_FPKG_ERROR }
        Err(_) => { put(info, info_len, "erreur interne pendant la vérification"); PBC_FPKG_ERROR }
    }
}

/// Lire l'identité d'un paquet sans le vérifier (rapide : l'en-tête et le conteneur) : {"content_id","title_id","title"}.
/// Retour : 0 lu, 2 pas un paquet ou illisible (`info` reçoit la raison).
#[no_mangle]
pub unsafe extern "C" fn pbc_fpkg_info(path: *const c_char, info: *mut c_char, info_len: usize) -> i32 {
    let Some(path) = path_arg(path) else {
        put(info, info_len, "chemin invalide");
        return PBC_FPKG_ERROR;
    };
    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| -> Result<String, String> {
        use ps5upload_fpkg::{cnt, fih, PkgFile};
        let mut file = PkgFile::open(&path).map_err(|e| e.to_string())?;
        let head = file.read_at(0, ps5upload_fpkg::BLOCK as usize).map_err(|e| e.to_string())?;
        let f = fih::parse(&head).map_err(|e| e.to_string())?;
        let c = cnt::read(&mut file, f.cnt_offset).map_err(|e| e.to_string())?;
        let mut title = String::new();
        if let Some(e) = c.entry(cnt::ids::PARAM_JSON) {
            if let Ok(v) = serde_json::from_slice::<serde_json::Value>(c.payload(e)) {
                let lp = &v["localizedParameters"];
                let lang = lp["defaultLanguage"].as_str().unwrap_or("en-US");
                title = lp[lang]["titleName"].as_str().or_else(|| lp["en-US"]["titleName"].as_str()).unwrap_or("").to_string();
            }
        }
        let tid = if c.content_id.len() >= 16 { c.content_id[7..16].to_string() } else { String::new() };
        Ok(serde_json::json!({"content_id": c.content_id, "title_id": tid, "title": title}).to_string())
    }));
    match r {
        Ok(Ok(j)) => { put(info, info_len, &j); PBC_FPKG_OK }
        Ok(Err(m)) => { put(info, info_len, &m); PBC_FPKG_ERROR }
        Err(_) => { put(info, info_len, "erreur interne"); PBC_FPKG_ERROR }
    }
}
