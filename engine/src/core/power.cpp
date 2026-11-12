#include "core/power.hpp"

#include <thread>
#include <unistd.h>

#include "core/util.hpp"

#if defined(PBCK_PS5)
extern "C" {
int sceSystemStateMgrReboot(void);
int sceSystemStateMgrEnterStandby(void);
int sceSystemStateMgrTurnOff(void);
}
#endif

namespace pbc::power {

bool request(const std::string& action, int delay_ms, std::string& error) {
    if (action != "reboot" && action != "rest" && action != "off") { error = "action inconnue (reboot, rest, off)"; return false; }
    log("power: %s dans %d ms", action.c_str(), delay_ms);
    std::thread([action, delay_ms] {
        ::usleep(static_cast<useconds_t>(delay_ms) * 1000);
#if defined(PBCK_PS5)
        int rc = -1;
        if (action == "reboot") rc = sceSystemStateMgrReboot();
        else if (action == "rest") rc = sceSystemStateMgrEnterStandby();
        else rc = sceSystemStateMgrTurnOff();
        log("power: %s rc=0x%x", action.c_str(), static_cast<unsigned>(rc));
#else
        log("power: %s (Mac : rien)", action.c_str());
#endif
    }).detach();
    return true;
}

}  // namespace pbc::power
