// Lancer un jeu depuis son .pkg (docs/PLAN-lancement-kraken.md) : les fonctions C de pbc-kraken (engine/kraken), code de
// Kraken Mount v0.3.0 dans notre bibliothèque Rust. Sur le Mac, seules setup et build existent.
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
void pbc_kraken_setup(const char* root, void (*log)(const char* line));
int pbc_kraken_build(const char* pkg, char* tid, size_t tid_len, char* err, size_t err_len);
#if defined(PBCK_PS5)
int pbc_kraken_kstuff(void);
int pbc_kraken_process(const char* name);
int pbc_kraken_is_ours(const char* tid);
int pbc_kraken_register(const char* tid, char* err, size_t err_len);
void pbc_kraken_watch(const char* tids);
void pbc_kraken_watch_loop(void);  // bloquante : dans un fil du moteur (std::thread de Rust échoue sur la PS5)
void pbc_kraken_running(char* out, size_t len);
#endif
#ifdef __cplusplus
}
#endif
