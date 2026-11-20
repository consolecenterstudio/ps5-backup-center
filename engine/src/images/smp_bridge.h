// Notre ShadowMount intégré (smp_bridge.c) : façade C pour le moteur. Console seulement ; sur le Mac, smp.cpp n'en
// appelle rien (le code de ShadowMountPlus ne se compile que pour la PS5).
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

enum { PBC_SMP_OFF = 0, PBC_SMP_STARTING = 1, PBC_SMP_RUNNING = 2, PBC_SMP_STOPPED = 3, PBC_SMP_FAILED = 4 };

// Démarre une fois (fil à part) ; 0 si démarré ou déjà en marche, -1 sinon. Une seule fois par vie du moteur : après
// pbc_smp_stop, il faut redémarrer le moteur pour le relancer (son état d'arrêt n'est pas remis à zéro).
int pbc_smp_start(void (*log)(const char*));
void pbc_smp_stop(void);  // démonte ce qu'il a monté, puis attend la fin du fil
int pbc_smp_state(void);
void pbc_smp_scan_now(void);  // un parcours tout de suite (après un rangement dans un dossier de jeux)

#ifdef __cplusplus
}
#endif
