//! Protected container entries (`flags1` bit 31): `nptitle.dat` and the two `npbind.dat` files
//! a package keeps only encrypted in its container.
//!
//! key/iv = SHA3-256(row || entry key): the first 16 bytes the IV, the last 16 the AES-128 key,
//! CBC. `row` is the entry's 32-byte table row as stored; the entry key is the passcode-derived
//! key that `flags2 >> 12` names, tested first against the container's entry-keys slot.
//!
//! Ported from ps5-dump-forge (`crates/ps5-dump-forge-fpkg/src/protected.rs` and the vendored
//! `ps5upload-fpkg` `crypto.rs`), GPL-3.0-or-later. SHA3-256 and AES-128 are written out here
//! because the crate takes no dependencies.

use alloc::format;
use alloc::string::String;
use alloc::vec;
use alloc::vec::Vec;

use crate::pkg::{pread_exact, CntEntry, Container};
use crate::PRead;

/// `flags1` bit 31: the entry is stored encrypted.
pub(crate) const FLAG: u32 = 0x8000_0000;

/// The container slot with the passcode key checks.
const ENTRY_KEYS: u32 = 0x0010;

/// The most a protected entry may be and still be decrypted: they are a few KiB.
const MAX_SIZE: u64 = 1 << 20;

/// The entry-keys slot holds keys 0..=6.
const KEY_COUNT: u32 = 7;

// ponytail: only the debug passcode (what dump-forge and other debug FPKG builders write). A
// package built with another passcode fails the key check and its protected entries are left
// out; taking a passcode per package would need a setting.
const DEBUG_PASSCODE: &str = "00000000000000000000000000000000";

/// The plaintext of protected entry `e` (debug passcode), or why it can't be had.
pub(crate) fn plaintext<R: PRead>(io: &R, len: u64, cnt: &Container, e: &CntEntry) -> Result<Vec<u8>, String> {
    if e.size > MAX_SIZE {
        return Err(format!("{} bytes, over the {MAX_SIZE} it decrypts", e.size));
    }
    let k = (e.flags2 >> 12) & 0xF;
    if k >= KEY_COUNT {
        return Err(format!("it names key index {k}, which the container has no key for"));
    }
    let key = derive_pfs_key(&cnt.content_id, DEBUG_PASSCODE, k);
    match key_check(io, cnt, &key, k)? {
        None => return Err("the container has no key check for its key index".into()),
        Some(false) => return Err("the debug passcode fails the container's key check".into()),
        Some(true) => {}
    }
    // Stored padded to the cipher's block; `size` is the file's own length.
    let padded = (e.size + 15) & !15;
    if e.at.checked_add(padded).map_or(true, |end| end > len) {
        return Err("its padded data runs past the end of the package".into());
    }
    let mut data = vec![0u8; padded as usize];
    pread_exact(io, e.at, &mut data).map_err(|x| format!("{x}"))?;
    decrypt_entry(&e.row, &key, &mut data);
    data.truncate(e.size as usize);
    Ok(data)
}

/// Whether `key`, the passcode-derived key `k`, passes the entry-keys slot (the 32-byte seed
/// digest, then `SHA3(k) ^ k` for keys 0..=6); `None` when the slot has no digest for it.
fn key_check<R: PRead>(io: &R, cnt: &Container, key: &[u8; 32], k: u32) -> Result<Option<bool>, String> {
    let digest_at = 32 + 32 * u64::from(k);
    let Some(keys) = cnt.entry(ENTRY_KEYS).filter(|ek| ek.size >= digest_at + 32) else {
        return Ok(None);
    };
    let mut stored = [0u8; 32];
    pread_exact(io, keys.at + digest_at, &mut stored).map_err(|x| format!("{x}"))?;
    let mut want = sha3(key);
    for (w, b) in want.iter_mut().zip(key) {
        *w ^= b;
    }
    Ok(Some(stored == want))
}

/// Decrypt a protected entry in place (whole 16-byte blocks): the inverse of dump-forge's
/// `crypto::encrypt_entry`.
pub(crate) fn decrypt_entry(row: &[u8; 32], entry_key: &[u8; 32], data: &mut [u8]) {
    let mut pre = [0u8; 64];
    pre[..32].copy_from_slice(row);
    pre[32..].copy_from_slice(entry_key);
    let iv_key = sha3(&pre);
    let mut key = [0u8; 16];
    let mut iv = [0u8; 16];
    key.copy_from_slice(&iv_key[16..]);
    iv.copy_from_slice(&iv_key[..16]);
    aes128_cbc_decrypt(&key, iv, data);
}

/// The entry key `index` for a content id and passcode.
pub(crate) fn derive_pfs_key(content_id: &str, passcode: &str, index: u32) -> [u8; 32] {
    let mut cid = [0u8; 48];
    let id = content_id.as_bytes();
    let n = id.len().min(48);
    cid[..n].copy_from_slice(&id[..n]);
    let mut buf = Vec::with_capacity(64 + passcode.len());
    buf.extend_from_slice(&sha3(&index.to_be_bytes()));
    buf.extend_from_slice(&sha3(&cid));
    buf.extend_from_slice(passcode.as_bytes());
    sha3(&buf)
}

// SHA3-256 (FIPS 202): Keccak-f[1600], rate 136 bytes, domain padding 0x06.

const RC: [u64; 24] = [
    0x0000000000000001, 0x0000000000008082, 0x800000000000808a, 0x8000000080008000,
    0x000000000000808b, 0x0000000080000001, 0x8000000080008081, 0x8000000000008009,
    0x000000000000008a, 0x0000000000000088, 0x0000000080008009, 0x000000008000000a,
    0x000000008000808b, 0x800000000000008b, 0x8000000000008089, 0x8000000000008003,
    0x8000000000008002, 0x8000000000000080, 0x000000000000800a, 0x800000008000000a,
    0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008,
];
const ROT: [u32; 25] = [0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43, 25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14];

fn keccak_f(a: &mut [u64; 25]) {
    for rc in RC {
        let mut c = [0u64; 5];
        for x in 0..5 {
            c[x] = a[x] ^ a[x + 5] ^ a[x + 10] ^ a[x + 15] ^ a[x + 20];
        }
        for x in 0..5 {
            let d = c[(x + 4) % 5] ^ c[(x + 1) % 5].rotate_left(1);
            for y in 0..5 {
                a[x + 5 * y] ^= d;
            }
        }
        // rho + pi: B[y, 2x + 3y] = rot(A[x, y]).
        let mut b = [0u64; 25];
        for x in 0..5 {
            for y in 0..5 {
                b[y + 5 * ((2 * x + 3 * y) % 5)] = a[x + 5 * y].rotate_left(ROT[x + 5 * y]);
            }
        }
        for y in 0..5 {
            for x in 0..5 {
                a[x + 5 * y] = b[x + 5 * y] ^ (!b[(x + 1) % 5 + 5 * y] & b[(x + 2) % 5 + 5 * y]);
            }
        }
        a[0] ^= rc;
    }
}

/// SHA3-256 of `data`.
pub(crate) fn sha3(data: &[u8]) -> [u8; 32] {
    const RATE: usize = 136;
    let mut a = [0u64; 25];
    let absorb = |a: &mut [u64; 25], block: &[u8; RATE]| {
        for (i, w) in block.chunks_exact(8).enumerate() {
            a[i] ^= u64::from_le_bytes([w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]]);
        }
        keccak_f(a);
    };
    let mut chunks = data.chunks_exact(RATE);
    for c in &mut chunks {
        let mut block = [0u8; RATE];
        block.copy_from_slice(c);
        absorb(&mut a, &block);
    }
    let rest = chunks.remainder();
    let mut last = [0u8; RATE];
    last[..rest.len()].copy_from_slice(rest);
    last[rest.len()] ^= 0x06;
    last[RATE - 1] ^= 0x80;
    absorb(&mut a, &last);
    let mut out = [0u8; 32];
    for (i, o) in out.chunks_exact_mut(8).enumerate() {
        o.copy_from_slice(&a[i].to_le_bytes());
    }
    out
}

// AES-128 (FIPS 197) decryption, CBC.

/// Multiplication in GF(2^8) modulo x^8 + x^4 + x^3 + x + 1.
const fn gmul(mut a: u8, mut b: u8) -> u8 {
    let mut p = 0u8;
    while b != 0 {
        if b & 1 != 0 {
            p ^= a;
        }
        let hi = a & 0x80;
        a <<= 1;
        if hi != 0 {
            a ^= 0x1b;
        }
        b >>= 1;
    }
    p
}

/// The S-box and its inverse, computed: inverse in GF(2^8) (x^254), then the affine map.
const SBOXES: ([u8; 256], [u8; 256]) = {
    let mut s = [0u8; 256];
    let mut inv = [0u8; 256];
    let mut x = 0usize;
    while x < 256 {
        let mut r = 1u8;
        let mut i = 0;
        while i < 254 {
            r = gmul(r, x as u8);
            i += 1;
        }
        let b = if x == 0 { 0 } else { r };
        let v = b ^ b.rotate_left(1) ^ b.rotate_left(2) ^ b.rotate_left(3) ^ b.rotate_left(4) ^ 0x63;
        s[x] = v;
        inv[v as usize] = x as u8;
        x += 1;
    }
    (s, inv)
};

/// The 11 round keys of AES-128.
fn expand_key(key: &[u8; 16]) -> [[u8; 16]; 11] {
    let sbox = &SBOXES.0;
    let mut w = [[0u8; 4]; 44];
    for i in 0..4 {
        w[i].copy_from_slice(&key[4 * i..4 * i + 4]);
    }
    let mut rcon = 1u8;
    for i in 4..44 {
        let mut t = w[i - 1];
        if i % 4 == 0 {
            t = [sbox[t[1] as usize] ^ rcon, sbox[t[2] as usize], sbox[t[3] as usize], sbox[t[0] as usize]];
            rcon = gmul(rcon, 2);
        }
        for j in 0..4 {
            w[i][j] = w[i - 4][j] ^ t[j];
        }
    }
    let mut rk = [[0u8; 16]; 11];
    for (r, k) in rk.iter_mut().enumerate() {
        for c in 0..4 {
            k[4 * c..4 * c + 4].copy_from_slice(&w[4 * r + c]);
        }
    }
    rk
}

/// One AES-128 block decrypted in place (byte `r + 4c` is row r, column c).
fn decrypt_block(rk: &[[u8; 16]; 11], s: &mut [u8; 16]) {
    let inv = &SBOXES.1;
    let add = |s: &mut [u8; 16], k: &[u8; 16]| s.iter_mut().zip(k).for_each(|(b, k)| *b ^= k);
    let inv_shift_sub = |s: &mut [u8; 16]| {
        let t = *s;
        for r in 0..4 {
            for c in 0..4 {
                s[r + 4 * ((c + r) % 4)] = inv[t[r + 4 * c] as usize];
            }
        }
    };
    add(s, &rk[10]);
    for round in (1..10).rev() {
        inv_shift_sub(s);
        add(s, &rk[round]);
        for c in 0..4 {
            let [a0, a1, a2, a3] = [s[4 * c], s[4 * c + 1], s[4 * c + 2], s[4 * c + 3]];
            s[4 * c] = gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9);
            s[4 * c + 1] = gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13);
            s[4 * c + 2] = gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11);
            s[4 * c + 3] = gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14);
        }
    }
    inv_shift_sub(s);
    add(s, &rk[0]);
}

/// AES-128-CBC decryption in place; a trailing partial block is left as is (never happens:
/// callers pass whole blocks).
fn aes128_cbc_decrypt(key: &[u8; 16], iv: [u8; 16], data: &mut [u8]) {
    let rk = expand_key(key);
    let mut prev = iv;
    for chunk in data.chunks_exact_mut(16) {
        let mut blk = [0u8; 16];
        blk.copy_from_slice(chunk);
        let ct = blk;
        decrypt_block(&rk, &mut blk);
        for (b, p) in blk.iter_mut().zip(prev) {
            *b ^= p;
        }
        chunk.copy_from_slice(&blk);
        prev = ct;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn unhex(s: &str) -> Vec<u8> {
        (0..s.len()).step_by(2).map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap()).collect()
    }

    #[test]
    fn sha3_256_known_answers() {
        assert_eq!(sha3(b"").to_vec(), unhex("a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a"));
        assert_eq!(sha3(b"abc").to_vec(), unhex("3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"));
        // 200 bytes: more than one 136-byte block.
        assert_eq!(sha3(&[0xa3; 200]).to_vec(), unhex("79f38adec5c20307a98ef76e8324afbfd46cfd81b22e3973c65fa1bd9de31787"));
    }

    #[test]
    fn aes128_fips197_c1() {
        let key: [u8; 16] = unhex("000102030405060708090a0b0c0d0e0f").try_into().unwrap();
        let mut blk: [u8; 16] = unhex("69c4e0d86a7b0430d8cdb78070b4c55a").try_into().unwrap();
        decrypt_block(&expand_key(&key), &mut blk);
        assert_eq!(blk.to_vec(), unhex("00112233445566778899aabbccddeeff"));
    }

    // Vector from dump-forge's own `derive_pfs_key` / `encrypt_entry` (ps5upload-fpkg 7.0.22):
    // content id UP0000-PPSA01234_00-TESTGAME00000000, debug passcode, key 3, a 0x0402 row,
    // plaintext (i * 7 + 1) for i in 0..50, padded to 64 bytes.
    const CONTENT_ID: &str = "UP0000-PPSA01234_00-TESTGAME00000000";
    const KEY3: &str = "be99fab1eb4d7b1ab6e7e30c5d18fedd8b5a157303e0330717341aa845dd9a08";
    const CHECK3: &str = "6fefce37aa9d4766c9cfe4ced2a8cda7daf9dc6b97bd259e29745b60c138c155";
    const ROW: &str = "0000040200000000800000000000300000001200000000320000000000000000";
    const CT: &str = "6e42784dfb16d264c5f5a2365ce03d6015ef1d6b0e33f0dd4be993409ee62b7b\
                      bffcef03843b50ba80d38afa5745ddb02a5b9cbd39ec79ebdbde47b8154163c6";

    fn plain() -> Vec<u8> {
        (0..50).map(|i| (i * 7 + 1) as u8).collect()
    }

    #[test]
    fn decrypts_dump_forge_encrypt_entry() {
        let key = derive_pfs_key(CONTENT_ID, DEBUG_PASSCODE, 3);
        assert_eq!(key.to_vec(), unhex(KEY3));
        let mut check = sha3(&key);
        check.iter_mut().zip(key).for_each(|(c, k)| *c ^= k);
        assert_eq!(check.to_vec(), unhex(CHECK3));
        let row: [u8; 32] = unhex(ROW).try_into().unwrap();
        let mut data = unhex(CT);
        decrypt_entry(&row, &key, &mut data);
        data.truncate(50);
        assert_eq!(data, plain());
    }

    struct Mem(Vec<u8>);
    impl PRead for Mem {
        fn pread(&self, off: u64, buf: &mut [u8]) -> Result<usize, crate::Error> {
            let at = (off as usize).min(self.0.len());
            let n = buf.len().min(self.0.len() - at);
            buf[..n].copy_from_slice(&self.0[at..at + n]);
            Ok(n)
        }
    }

    /// The entry-keys slot at 0x100 (seed digest, then key 3's check), the entry at 0x200.
    fn package(check: &str) -> (Mem, Container, CntEntry) {
        let mut b = vec![0u8; 0x240];
        b[0x100 + 32 + 32 * 3..0x100 + 32 + 32 * 4].copy_from_slice(&unhex(check));
        b[0x200..0x240].copy_from_slice(&unhex(CT));
        let row: [u8; 32] = unhex(ROW).try_into().unwrap();
        let keys = CntEntry { id: ENTRY_KEYS, flags1: 0, flags2: 0, at: 0x100, size: 32 + 7 * 32, row: [0; 32] };
        let e = CntEntry { id: 0x0402, flags1: FLAG, flags2: 3 << 12, at: 0x200, size: 50, row };
        let cnt = Container { content_id: CONTENT_ID.into(), entries: vec![keys, e] };
        (Mem(b), cnt, e)
    }

    #[test]
    fn plaintext_checks_the_key_then_decrypts() {
        let (io, cnt, e) = package(CHECK3);
        assert_eq!(plaintext(&io, io.0.len() as u64, &cnt, &e).unwrap(), plain());
    }

    #[test]
    fn a_failed_key_check_yields_nothing() {
        let (io, cnt, e) = package(KEY3);
        let why = plaintext(&io, io.0.len() as u64, &cnt, &e).unwrap_err();
        assert!(why.contains("key check"), "{why}");
        let (io, cnt, e) = package(CHECK3);
        let big = CntEntry { size: MAX_SIZE + 1, ..e };
        assert!(plaintext(&io, io.0.len() as u64, &cnt, &big).is_err());
    }
}
