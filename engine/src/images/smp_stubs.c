// Notre ShadowMount intégré (docs/PLAN-integration-shadowmount.md) : les modules de ShadowMountPlus qu'on ne reprend
// pas, remplacés par des fonctions qui ne font rien. Ainsi : aucun crochet posé dans SceShellCore (l'inscription passe
// par l'appel direct de sceAppInstUtilAppInstallTitleDir, sm_install.c), aucune interface web ni API (aucun port),
// pas de serveur de paquets, pas de mise à jour d'ampr-emu depuis Internet, pas de ventilateur, pas de lecture du
// journal du noyau, pas de raccourci dans le menu de la PS5.
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "sm_limits.h"
#include "sm_types.h"

// sm_ampr_updater.c
void sm_ampr_updater_on_config_reload(const runtime_config_t *old_cfg, const runtime_config_t *new_cfg) { (void)old_cfg; (void)new_cfg; }
void sm_ampr_updater_on_sleep_change(bool active) { (void)active; }
bool sm_ampr_updater_start(void) { return false; }
void sm_ampr_updater_stop(void) {}

// sm_api_service.c
void sm_api_service_on_sleep_change(bool active) { (void)active; }
bool sm_api_service_reconfigure(void) { return false; }
bool sm_api_service_start(void) { return false; }
void sm_api_service_stop(void) {}

// sm_fan.c
void sm_fan_apply_game_target(const char *title_id) { (void)title_id; }

// sm_mdbg.c
void sm_mdbg_game_handoff(pid_t old_pid, pid_t new_pid) { (void)old_pid; (void)new_pid; }
void sm_mdbg_game_on_exec(pid_t pid, const char *title_id, uint64_t exec_time_us) { (void)pid; (void)title_id; (void)exec_time_us; }
void sm_mdbg_game_on_exit(pid_t pid) { (void)pid; }
void sm_mdbg_game_shutdown(void) {}
void sm_mdbg_init(void) {}
uint64_t sm_mdbg_next_wake_us(void) { return 0; }
void sm_mdbg_poll(bool process_active) { (void)process_active; }
void sm_mdbg_shutdown(void) {}

// sm_shell_icon.c
void sm_shell_icon_install_if_missing(void) {}

// sm_shellcore_flags.c, sm_shellcore_hooks.c, sm_shellcore_service.c : pas de crochets → comportement d'origine du
// système (« stock behavior » de ShadowMountPlus quand ses crochets ne sont pas disponibles).
bool sm_shellcore_background_work_blocked(void) { return false; }
void sm_shellcore_end_external_mutation(void) {}
bool sm_shellcore_try_begin_external_mutation(void) { return true; }
bool sm_shellcore_ensure_title_runtime(const char *title_id) { (void)title_id; return true; }
bool sm_shellcore_flags_start(void) { return false; }
void sm_shellcore_flags_stop(void) {}
bool sm_shellcore_hooks_refresh(void) { return false; }
bool sm_shellcore_hooks_start(void) { return false; }
void sm_shellcore_hooks_stop(void) {}
bool sm_shellcore_install_bridge_enabled(void) { return false; }  // → appel direct de sceAppInstUtilAppInstallTitleDir
bool sm_shellcore_install_title_dir(const char *title_id, const char *install_dir, int *result_out) {
    (void)title_id; (void)install_dir;
    if (result_out) *result_out = -1;
    return false;
}
bool sm_shellcore_service_bind_prepared_app(const char *title_id, uint32_t app_id, pid_t pid) { (void)title_id; (void)app_id; (void)pid; return false; }
bool sm_shellcore_service_has_prepared_mount(void) { return false; }
uint64_t sm_shellcore_service_next_exit_cleanup_us(uint64_t now_us) { (void)now_us; return 0; }
bool sm_shellcore_service_note_game_exit(pid_t pid, uint64_t exit_time_us, char title_id_out[MAX_TITLE_ID]) {
    (void)pid; (void)exit_time_us;
    if (title_id_out) title_id_out[0] = '\0';
    return false;
}
void sm_shellcore_service_on_sleep_change(bool active) { (void)active; }
int sm_shellcore_service_release_exited_titles(void) { return 0; }
bool sm_shellcore_service_start(void) { return false; }
void sm_shellcore_service_stop(void) {}
bool sm_shellcore_service_title_is_prepared(const char *title_id) { (void)title_id; return false; }
bool sm_shellcore_service_title_sandbox_exists(const char *title_id) { (void)title_id; return false; }
bool sm_shellcore_source_is_protected(const char *path) { (void)path; return false; }
bool sm_shellcore_title_is_protected(const char *title_id) { (void)title_id; return false; }
