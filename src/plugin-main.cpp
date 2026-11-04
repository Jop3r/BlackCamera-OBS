#include "ascii_filter.h"
#include "log.h"

#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char* obs_module_name(void) { return "BlackCamera"; }
MODULE_EXPORT const char* obs_module_description(void) { return obs_module_text("Description"); }

namespace {

// blackcamera_effect logs through logging::write; send it to the OBS log
void obsLogSink(char level, const char* message) {
    const int obsLevel = level == 'E' ? LOG_ERROR : level == 'W' ? LOG_WARNING : LOG_INFO;
    blog(obsLevel, "[" PLUGIN_NAME "] %s", message);
}

} // namespace

bool obs_module_load(void) {
    logging::setSink(obsLogSink);
    registerAsciiFilter();
    blog(LOG_INFO, "[" PLUGIN_NAME "] loaded (version " PLUGIN_VERSION ")");
    return true;
}

void obs_module_unload(void) {
    logging::setSink(nullptr);
}
