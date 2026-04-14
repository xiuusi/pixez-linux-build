#include "single_instance_plugin.h"

#include <flutter_linux/flutter_linux.h>

void register_single_instance_plugin(FlPluginRegistrar* registrar) {
    // Single instance detection for Linux is not implemented yet
    // This is an optional feature - the app will still work without it
}