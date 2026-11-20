//! Sur le Mac : un faux jeu → .pkg (notre bibliothèque de paquets) → cache de démarrage et index (pbc-kraken).
use std::ffi::{c_char, CString};
use std::fs;

#[test]
fn cache_d_un_pkg() {
    let base = std::env::temp_dir().join(format!("pbc-kraken-test-{}", std::process::id()));
    let _ = fs::remove_dir_all(&base);
    let game = base.join("jeu/PPSA01234");
    fs::create_dir_all(game.join("sce_sys")).unwrap();
    fs::write(game.join("sce_sys/param.json"), r#"{"titleId":"PPSA01234","contentId":"UP0001-PPSA01234_00-JEUTEST000000000","applicationCategoryType":0,"contentVersion":"01.000.000","masterVersion":"01.01","requiredSystemSoftwareVersion":"0x1120000000000000","sdkVersion":"0x1000000000000000","localizedParameters":{"defaultLanguage":"en-US","en-US":{"titleName":"Jeu Test"}}}"#).unwrap();
    let mut eboot = vec![0u8; 300_000];
    for (i, b) in eboot.iter_mut().enumerate() { *b = (i * 7 % 251) as u8; }
    fs::write(game.join("eboot.bin"), &eboot).unwrap();
    fs::create_dir_all(game.join("data")).unwrap();
    fs::write(game.join("data/niveau1.dat"), vec![3u8; 2_000_000]).unwrap();
    let pkg = base.join("PPSA01234.pkg");
    let (src, out) = (CString::new(game.to_str().unwrap()).unwrap(), CString::new(pkg.to_str().unwrap()).unwrap());
    let mut info = vec![0 as c_char; 4096];
    let r = unsafe { pbc_fpkg::pbc_fpkg_build(src.as_ptr(), out.as_ptr(), 1, 2, None, std::ptr::null_mut(), std::ptr::null(), info.as_mut_ptr(), info.len()) };
    assert_eq!(r, 0, "paquet : {:?}", unsafe { std::ffi::CStr::from_ptr(info.as_ptr()) });

    let root = CString::new(base.join("kraken").to_str().unwrap()).unwrap();
    pbc_kraken::pbc_kraken_setup(root.as_ptr(), None);
    let mut tid = vec![0 as c_char; 32];
    let mut err = vec![0 as c_char; 1024];
    let r = pbc_kraken::pbc_kraken_build(out.as_ptr(), tid.as_mut_ptr(), tid.len(), err.as_mut_ptr(), err.len());
    assert_eq!(r, 0, "cache : {:?}", unsafe { std::ffi::CStr::from_ptr(err.as_ptr()) });
    assert_eq!(unsafe { std::ffi::CStr::from_ptr(tid.as_ptr()) }.to_str().unwrap(), "PPSA01234");
    let c = base.join("kraken/caches/PPSA01234");
    for f in ["sce_sys/param.json", "eboot.bin", "fakelib/libSceAmpr.sprx", "kraken.image", "ampr_emu.index", ".done"] {
        assert!(c.join(f).exists(), "absent du cache : {f}");
    }
    assert_eq!(fs::read(c.join("eboot.bin")).unwrap(), eboot, "eboot.bin extrait tel quel");
    assert_eq!(fs::read_to_string(c.join("kraken.image")).unwrap(), "kraken/PPSA01234.pkg");
    assert!(!c.join("data/niveau1.dat").exists(), "les données restent dans le .pkg (lues par ampr-emu-ng)");
    let ours = fs::read(base.join("../../../out/injected/libSceAmpr.sprx")).ok();
    let _ = ours;
    println!("cache : {:?}", fs::read_dir(&c).unwrap().flatten().map(|e| e.file_name()).collect::<Vec<_>>());
    let _ = fs::remove_dir_all(&base);
}
