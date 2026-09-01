#include "host_harness.hpp"

#include <dlfcn.h>
#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

hyprfield::testing::HostHarness* active_host = nullptr;

}  // namespace

namespace HyprlandAPI {

extern "C" bool addLuaFunction(void* handle,
                               const std::string& namespace_,
                               const std::string& name,
                               PLUGIN_LUA_FN function) {
  static_cast<void>(handle);
  active_host->registerLuaFunction(namespace_, name, function);
  return true;
}

extern "C" bool addNotification(void* handle, const std::string& text, const CHyprColor& color, const float time_ms) {
  static_cast<void>(handle);
  static_cast<void>(color);
  static_cast<void>(time_ms);
  active_host->recordNotification(text);
  return true;
}

}  // namespace HyprlandAPI

namespace hyprfield::testing {

bool HostHarness::loadPlugin(const std::string& path, std::string_view expected_version) {
  library_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (library_ == nullptr) {
    return false;
  }

  const auto version = reinterpret_cast<PPLUGIN_API_VERSION_FUNC>(dlsym(library_, PLUGIN_API_VERSION_FUNC_STR));
  const auto init = reinterpret_cast<PPLUGIN_INIT_FUNC>(dlsym(library_, PLUGIN_INIT_FUNC_STR));
  if (version == nullptr || init == nullptr || version() != expected_version) {
    unload();
    return false;
  }

  active_host = this;
  const auto description = init(this);
  active_host = nullptr;
  plugin_exit_ = reinterpret_cast<PPLUGIN_EXIT_FUNC>(dlsym(library_, PLUGIN_EXIT_FUNC_STR));
  plugin_name_ = description.name;
  return true;
}

void HostHarness::unload() {
  if (plugin_exit_ != nullptr) {
    active_host = this;
    plugin_exit_();
    active_host = nullptr;
    plugin_exit_ = nullptr;
  }
  plugin_name_.clear();
  lua_functions_.clear();
  if (library_ != nullptr) {
    dlclose(library_);
    library_ = nullptr;
  }
}

void HostHarness::registerLuaFunction(std::string_view namespace_, std::string_view name, LuaFunction function) {
  lua_functions_.emplace_back(std::string{namespace_} + "." + std::string{name}, function);
}

void HostHarness::recordNotification(std::string_view text) {
  notifications_.push_back({.text = std::string{text}});
}

bool HostHarness::invokeLua(std::string_view namespace_, std::string_view name, std::string_view argument) {
  const auto key = std::string{namespace_} + "." + std::string{name};
  const auto found = std::ranges::find_if(lua_functions_, [&key](const auto& entry) { return entry.first == key; });
  if (found == lua_functions_.end()) {
    return false;
  }
  lua_State* state = luaL_newstate();
  lua_pushlstring(state, argument.data(), argument.size());
  found->second(state);
  lua_close(state);
  return true;
}

bool HostHarness::loaded() const {
  return library_ != nullptr && !plugin_name_.empty();
}

const std::string& HostHarness::pluginName() const {
  return plugin_name_;
}

const std::vector<Notification>& HostHarness::notifications() const {
  return notifications_;
}

}  // namespace hyprfield::testing
