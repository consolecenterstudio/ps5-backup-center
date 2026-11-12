// Journal, empreintes, identifiants, temps.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

extern "C" {
#include "self_update_sha256.h"
}

namespace pbc {

// Une ligne « PBC2 … » : sur la console vers le journal système, sur le Mac vers stderr.
void log_line(const char* line);
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

class Sha256 {
public:
    Sha256() { self_update_sha256_start(&h_); }
    void add(const void* p, std::size_t n) { self_update_sha256_add(&h_, p, n); }
    std::string hex() { unsigned char d[32]; char t[65]; self_update_sha256_finish(&h_, d); self_update_sha256_hex(d, t); return t; }
private:
    self_update_sha256 h_{};
};
std::string sha256_hex(std::string_view data);

std::string random_id(unsigned bytes = 8);  // hexadécimal
// Version du système de la console (« 3.21 », « 12.40 ») ; vide si inconnue. PBC_FIRMWARE la remplace (tests).
std::string firmware();
// Version système « 11.20 » → 1120 (comparaisons) ; 0 si illisible.
int firmware_number(const std::string& v);
std::int64_t now_unix();
std::string iso_time(std::int64_t unix_seconds);
// Nom de fichier sûr (sans '/', sans caractères de contrôle), pour les dossiers de sauvegarde.
std::string safe_name(std::string_view s, std::size_t max = 80);

// Compare deux versions « a.b.c » (« 01.000.039 ») : <0, 0, >0 — même règle que les mises à jour de l'app.
int compare_versions(const std::string& a, const std::string& b);

}  // namespace pbc
