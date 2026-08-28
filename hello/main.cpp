#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <string>
#include <utility>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace {

constexpr auto DISPATCHER_NAME = "hello:hello";
HANDLE pluginHandle = nullptr;

void showHello(HANDLE handle, std::string name) {
  if (name.empty()) {
    name = "world";
  }

  HyprlandAPI::addNotification(handle, "Hello, " + name + "!", CHyprColor{0.2F, 0.8F, 0.4F, 1.0F}, 5000.0F);
}

int sayHelloLua(lua_State* state) {
  const auto* name = luaL_optstring(state, 1, "world");
  showHello(pluginHandle, name);
  return 0;
}

}  // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() {
  return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  pluginHandle = handle;

  HyprlandAPI::addLuaFunction(handle, "hello", "say", sayHelloLua);

  HyprlandAPI::addDispatcherV2(handle, DISPATCHER_NAME, [handle](std::string name) {
    showHello(handle, std::move(name));
    return SDispatchResult{};
  });

  HyprlandAPI::addNotification(handle, "hello plugin loaded", CHyprColor{0.2F, 0.8F, 0.4F, 1.0F}, 5000.0F);

  return {
      .name = "hello",
      .description = "A minimal Hyprland plugin example",
      .author = "zurat",
      .version = "0.1.0",
  };
}

APICALL EXPORT void PLUGIN_EXIT() {
  HyprlandAPI::removeDispatcher(pluginHandle, DISPATCHER_NAME);
}
