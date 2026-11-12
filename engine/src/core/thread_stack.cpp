// Pile des fils du moteur : 512 Ko quand rien n'est précisé, quel que soit celui qui a lancé le moteur.
// Lancé par le chargeur du débridage (9021, Payload Manager, l'elfldr d'origine), le moteur recevait des fils de 64 Ko
// seulement, et plantait au premier tampon de 64 Ko sur la pile (ex. Updater::run_check) ; lancé par
// Arsenal, la pile par défaut était plus grande. L'édition des liens redirige pthread_create ici (--wrap=pthread_create,
// build-system/build.py) : tous les fils passent par là, std::thread compris. PS5 seulement.
#if defined(PBCK_PS5)
#include <pthread.h>

extern "C" int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*fn)(void*), void* arg);

extern "C" int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
    if (attr) return __real_pthread_create(thread, attr, fn, arg);  // taille déjà choisie par l'appelant
    pthread_attr_t a;
    if (pthread_attr_init(&a) != 0) return __real_pthread_create(thread, nullptr, fn, arg);
    pthread_attr_setstacksize(&a, 512u * 1024u);
    const int rc = __real_pthread_create(thread, &a, fn, arg);
    pthread_attr_destroy(&a);
    return rc;
}
#endif
