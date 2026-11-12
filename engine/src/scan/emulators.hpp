// Émulateurs : catalogue en données, résolution des dossiers réels (fichier de réglages
// d'abord, dossiers par défaut ensuite) et brique de reconnaissance des jeux (ROM).
#pragma once
#include <map>
#include <string>
#include <vector>

#include "core/json.hpp"
#include "model/model.hpp"
#include "scan/scanner.hpp"

namespace pbc::scan {

struct RomRoot {
    std::string path;        // chemin console
    std::string platform;    // plateforme des jeux de ce dossier
    std::string source;      // « config », « défaut »
};

struct Emulator {
    std::string id, name, platform, data;
    std::vector<std::string> title_ids, config_files, rom_ext, saves, states, covers;
    std::string switch_saves;
    Json config_roots;
    std::vector<std::string> rom_roots_default, saves_patterns, states_patterns;  // tels que dans le catalogue
    // Réglages manuels de l'utilisateur (prioritaires) : dossiers de ROM, fichier de réglages, sauvegardes.
    std::vector<std::string> manual_roms;
    std::string manual_config, manual_saves;
    // Résolu sur la console :
    bool installed = false;
    std::vector<RomRoot> roots;
};

class Catalog {
public:
    bool load(const std::string& override_file_host);   // catalogue intégré + surcharge éventuelle
    // Détermine les émulateurs installés et leurs dossiers de jeux ; `usb_mounts` : /mnt/usbN présents.
    void resolve(const std::vector<std::string>& usb_mounts, const std::vector<std::string>& installed_title_ids);
    const std::vector<Emulator>& all() const { return emulators_; }
    Json to_json() const;  // pour Paramètres → Émulateurs
    // Réglages manuels : lus / écrits dans <état>/emulator-settings.json. `settings` vide = automatique.
    void load_manual(const std::string& host_file);
    bool set_manual(const std::string& id, const Json& settings, const std::string& host_file);
private:
    std::vector<Emulator> emulators_;
};

// Brique de reconnaissance : chaque dossier de jeux résolu devient une liste d'éléments ROM.
// Un même fichier vu par deux émulateurs donne un seul élément (clé = chemin du jeu).
class RomRecognizer : public Recognizer {
public:
    explicit RomRecognizer(const Catalog& c) : catalog_(c) {}
    const char* name() const override { return "roms"; }
    Verdict visit(const DirView& dir, std::vector<Item>& out) override;
    std::vector<Root> roots() const;  // à ajouter au parcours du scanner
private:
    const Catalog& catalog_;
};

// Utilitaires exposés pour les tests.
std::string normalize_title(std::string_view name);              // « Super Mario 64 (Europe) [!] » → « supermario64 »
bool switch_title_id(std::string_view name, std::string& tid);  // « … [0100ABC…][v0].nsp » → identifiant
std::string switch_base_id(const std::string& tid);             // mise à jour / DLC → jeu de base

}  // namespace pbc::scan
