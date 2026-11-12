#pragma once
// Registre des identités : chaque identifiant de compte vu par l'app (comptes de la console, noms notés dans les
// manifestes de nos backups, registres trouvés sur les disques), avec son nom et ses dates. Gardé dans l'état du
// moteur ET copié dans « PBC Backup/identities.json » de chaque disque USB, pour survivre à un formatage de la console :
// rebrancher le disque suffit à retrouver les noms d'identifiants qui n'existent plus sur la console.
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "core/json.hpp"

namespace pbc::accounts {

struct Identity {
    std::string name, user_id, console;
    std::int64_t first_seen = 0, last_seen = 0;
};

class Identities {
public:
    explicit Identities(std::string host_path);
    // Ajoute ou rafraîchit un identifiant (16 chiffres hexadécimaux, « 0x » toléré) ; vrai si quelque chose a changé.
    bool note(const std::string& account_id, const std::string& name, const std::string& user_id, std::int64_t when, const std::string& console = "");
    bool merge(const Json& other);  // un registre lu sur un disque
    std::string name(const std::string& account_id) const;
    Json to_json() const;
    bool save();                    // dans l'état du moteur, si changé
    bool dirty() const { return dirty_; }
    static std::string normalize(std::string id);
private:
    std::string path_;
    mutable std::mutex mu_;
    std::map<std::string, Identity> ids_;
    bool dirty_ = false;
};

}  // namespace pbc::accounts
