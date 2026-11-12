#include "core/appdb.hpp"

#include <cstdlib>

#include "core/fs.hpp"
#include "core/util.hpp"
#include "sqlite3.h"

namespace pbc::appdb {

std::map<std::string, Info> read(const std::string& console_path) {
    std::map<std::string, Info> out;
    const std::string path = fs::host(console_path);
    if (!fs::exists(path)) return out;
    // immutable=1 : aucun verrou, aucune écriture, même si le système l'utilise en même temps.
    const std::string uri = "file:" + path + "?immutable=1";
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(uri.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr);
    if (rc != SQLITE_OK) { log("appdb open failed rc=%d", rc); sqlite3_close(db); return out; }
    sqlite3_stmt* st = nullptr;
    rc = sqlite3_prepare_v2(db, "SELECT titleId, key, val FROM tbl_appinfo WHERE key IN ('TITLE','CATEGORY_TYPE')", -1, &st, nullptr);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const auto* tid = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
            const auto* key = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
            const auto* val = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
            if (!tid || !key || !val) continue;
            Info& i = out[tid];
            if (std::string(key) == "TITLE") i.title = val;
            else i.category = std::strtol(val, nullptr, 10);
        }
    } else log("appdb query failed rc=%d", rc);
    sqlite3_finalize(st);
    sqlite3_close(db);
    log("appdb titles=%zu", out.size());
    return out;
}

}  // namespace pbc::appdb
