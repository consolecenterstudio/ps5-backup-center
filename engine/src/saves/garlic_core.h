/*
 * Cœur de restauration des sauvegardes PS5 / PS4, compilé dans le moteur (aucun processus, aucun port).
 *
 * Adapté de Garlic SaveMgr v1.15-beta12 par earthonion (https://github.com/earthonion/garlic-savemgr),
 * GPL-3.0, dont la copie figée et non modifiée est dans engine/third_party/garlic-savemgr/src/main.c.
 * On garde la logique de Garlic (montage, élévation authid, bloc PS5PARAM, re-signature du propriétaire,
 * mise à jour de savedata.db) ; seule la plomberie HTTP est remplacée par des paramètres et des retours.
 *
 * Toutes les fonctions sont sérialisées par un verrou interne. Une seule image est montée à la fois.
 * Sur Mac (build hôte), chaque fonction échoue avec « non disponible sur Mac ».
 */
#ifndef PBC_GARLIC_CORE_H
#define PBC_GARLIC_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Journal : par défaut rien ; le moteur branche le sien. */
void gc_set_log(void (*fn)(const char *line));

/* Étape 1 de l'import (POST /api/import_encrypted de Garlic) : copie l'image chiffrée (et sa clé .bin pour une
 * PS4) dans /data/save_files, la monte sur /data/mount_sd, lit le vrai param.sfo (bloc PS5PARAM pour une PS5)
 * et compare le compte de la sauvegarde à celui du compte cible (registre). L'image reste montée :
 * suivre de gc_import_finish() ou de gc_unmount(). */
typedef struct {
    char title_id[16];
    char dir_name[64];
    char main_title[128];
    uint64_t save_account_id;   /* ACCOUNT_ID du param.sfo */
    uint64_t user_account_id;   /* compte cible lu dans le registre (0 si inconnu) */
    int user_account_known;
    int match;                  /* même compte (ou compte cible inconnu, comme Garlic) */
    int exists;                 /* une image du même nom est déjà installée */
    int sfo_zeroed;             /* param.sfo à zéro : l'image reste montée, rien d'autre n'est lu */
    int ps4;
} gc_import_info;

int gc_import_encrypted(const char *image_path, const char *key_path_or_null, int ps4, const char *uid_hex,
                        gc_import_info *out, char *err, size_t errsz);

/* Étape 2 (GET /api/import_finish) : re-signe le param.sfo pour le compte cible, démonte, réécrit le bloc
 * PS5PARAM, copie l'image dans /user/home/<uid>/savedata_prospero|savedata/<TITRE>/sdimg_<dossier>,
 * l'icône dans savedata_prospero_meta, puis met à jour /system_data/.../savedata.db. */
typedef struct {
    int force;                  /* param.sfo illisible : continuer avec title_id / dir_name ci-dessous */
    const char *title_id;
    const char *dir_name;
} gc_finish_options;

typedef struct {
    char title_id[16];
    char dir_name[64];
    char main_title[128];
    uint64_t account_id;        /* compte écrit dans le param.sfo */
    int owner_result;           /* 0 complet, 1..4 : avertissement de Garlic (voir warning) */
    char warning[256];
    int sfo_zeroed;
    int db_existed;             /* l'entrée existait déjà dans savedata.db */
    int db_ok;
    int blob_written;           /* bloc PS5PARAM réécrit dans l'en-tête */
} gc_finish_info;

int gc_import_finish(const char *uid_hex, const gc_finish_options *opt, gc_finish_info *out, char *err, size_t errsz);

/* Montage d'une image installée pour la relire (GET /api/mount de Garlic) : copie dans /data/save_files puis
 * montage sur /data/mount_sd. Différence volontaire : gc_unmount() ne recopie jamais cette copie sur l'original
 * (lecture seule). */
typedef struct {
    char title_id[16];
    char main_title[128];
    char mount_point[256];
    uint64_t account_id;
    int sfo_ok;                 /* param.sfo lisible */
    int sfo_zeroed;
    int from_blob;              /* lu dans le bloc PS5PARAM */
    unsigned files;             /* fichiers et dossiers sous le point de montage */
    /* PBC : param.sfo effectif (bloc PS5PARAM ou sce_sys/param.sfo) tel quel, pour lire SUBTITLE, DETAIL, PARAMS…
     * Pointe vers un tampon interne valable jusqu'au prochain appel gc_* ; NULL / 0 si illisible. */
    const unsigned char *sfo;
    unsigned sfo_len;
} gc_mount_info;

int gc_mount_save(const char *image_path, gc_mount_info *out, char *err, size_t errsz);

/* Démonte l'image montée (s'il y en a une) et supprime les copies temporaires de /data/save_files. */
int gc_unmount(void);

/* Au démarrage du moteur : démonte les sauvegardes laissées montées par une exécution précédente (arrêt brutal,
 * plantage) et efface les copies temporaires d'import. */
void gc_startup_cleanup(void);

/* Titre du jeu en cours (sceSystemServiceGetAppIdOfRunningBigApp) : 1 si un jeu tourne, 0 sinon. */
int gc_running_title(char out[16]);

#ifdef __cplusplus
}
#endif

#endif
