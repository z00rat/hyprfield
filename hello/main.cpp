#include <hyprland/src/plugins/PluginAPI.hpp>
#include <string>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace {

HANDLE pluginHandle = nullptr;

int sayHelloLua(lua_State* state) {
  const auto* name = luaL_optstring(state, 1, "world");
  HyprlandAPI::addNotification(
      pluginHandle, "Hello, " + std::string{name} + "!", CHyprColor{0.2F, 0.8F, 0.4F, 1.0F}, 5000.0F);
  return 0;
}

}  // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() {
  return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  pluginHandle = handle;

  HyprlandAPI::addLuaFunction(handle, "hello", "say", sayHelloLua);

  HyprlandAPI::addNotification(handle, "hello plugin loaded", CHyprColor{0.2F, 0.8F, 0.4F, 1.0F}, 5000.0F);

  return {
      .name = "hello",
      .description = "A minimal Hyprland plugin example",
      .author = "zurat",
      .version = "0.1.0",
  };
}
