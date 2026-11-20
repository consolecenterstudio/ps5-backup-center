/* One parameterized statement on an existing SQLite database (app.db's snd0info, register.rs
 * set_snd0info). Port of ShadowMountPlus sm_appdb.c: read-write open (never create), a busy
 * timeout since the system holds app.db open, ?1 bound as text. */
#include <stdio.h>
#include "sqlite3.h"

/* Returns the rows changed, or -1 with sqlite's message in err. */
int km_sqlite_exec1(const char *path, const char *sql, const char *arg, char *err, size_t errlen) {
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;
    int ret = -1;
    if (errlen)
        err[0] = '\0';
    int rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_busy_timeout(db, 3000);
        rc = sqlite3_prepare_v2(db, sql, -1, &st, NULL);
    }
    if (rc == SQLITE_OK)
        rc = sqlite3_bind_text(st, 1, arg, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(st);
        if (rc == SQLITE_DONE)
            ret = sqlite3_changes(db);
    }
    if (ret < 0 && errlen)
        snprintf(err, errlen, "%s (%d)", db ? sqlite3_errmsg(db) : "out of memory", rc);
    sqlite3_finalize(st);
    sqlite3_close(db);
    return ret;
}
