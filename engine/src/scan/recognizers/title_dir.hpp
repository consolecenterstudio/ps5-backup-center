// Brique : dossier d'application PS5 (homebrew ou jeu installé) reconnu à son
// sce_sys/param.json. Composants : l'application entière, et sa configuration
// si on sait où elle se trouve (table `config_dirs`, alimentée par les briques d'émulateurs).
#pragma once
#include <map>
#include <string>
#include <vector>

#include "scan/scanner.hpp"

namespace pbc::scan {

class TitleDirRecognizer : public Recognizer {
public:
    // Dossier de configuration connu par Title ID (chemin console), ex. ProsperoEden → /data/prosperoeden/config.
    void set_config_dir(const std::string& title_id, const std::string& console_dir) { config_dirs_[title_id] = console_dir; }
    // Fichiers de réglages connus (catalogue des émulateurs) : composant « Configuration ».
    void set_config_files(const std::string& title_id, const std::vector<std::string>& files) { config_files_[title_id] = files; }
    void clear_config() { config_dirs_.clear(); config_files_.clear(); }
    const char* name() const override { return "title_dir"; }
    Verdict visit(const DirView& dir, std::vector<Item>& out) override;

private:
    std::map<std::string, std::string> config_dirs_;
    std::map<std::string, std::vector<std::string>> config_files_;
};

// Lit titleId et nom (langue préférée fr, puis en, puis défaut) d'un param.json.
bool read_param(const std::string& console_path, std::string& title_id, std::string& title, int& category);
// Vrai jeu du commerce : signature « standard » (les homebrews sont « free » ou sans signature),
// même s'ils se déclarent « jeu » (applicationCategoryType 0) comme BlackBox ou Orbit.
bool is_retail(const std::string& console_path);

}  // namespace pbc::scan
