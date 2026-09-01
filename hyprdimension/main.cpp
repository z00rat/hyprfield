#include "plugin.hpp"

APICALL EXPORT std::string PLUGIN_API_VERSION() {
  return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  hyprdimension::initialize(handle);
  return {.name = "hyprdimension",
          .description = "Monitor-scoped dimensional workspaces",
          .author = "zurat",
          .version = "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
  hyprdimension::shutdown();
}
