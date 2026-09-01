#include "host_harness.hpp"

#include <dlfcn.h>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <hyprland/src/debug/log/Logger.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

hyprfield::testing::HostHarness* active_host = nullptr;

using PluginDescription = struct {
  std::string name;
  std::string description;
  std::string author;
  std::string version;
};

using PluginApiVersion = std::string (*)();
using PluginInit = PluginDescription (*)(void*);
using PluginExit = void (*)();

constexpr auto plugin_api_version_name = "pluginAPIVersion";
constexpr auto plugin_init_name = "pluginInit";
constexpr auto plugin_exit_name = "pluginExit";

}  // namespace

class CHyprColor {
 public:
  CHyprColor(float red, float green, float blue, float alpha) : r(red), g(green), b(blue), a(alpha) {}

  float r;
  float g;
  float b;
  float a;
};

extern "C" void colorConstructorStub() asm("_ZN10CHyprColorC1Effff");
void colorConstructorStub() {}

Log::CLogger::CLogger() : m_logger(), m_logsEnabled(true), m_isTrace(false) {}

namespace HyprlandAPI {

extern "C" bool addLuaFunction(void* handle,
                               const std::string& namespace_,
                               const std::string& name,
                               int (*function)(lua_State*)) {
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

  const auto version = reinterpret_cast<PluginApiVersion>(dlsym(library_, plugin_api_version_name));
  const auto init = reinterpret_cast<PluginInit>(dlsym(library_, plugin_init_name));
  if (version == nullptr || init == nullptr || version() != expected_version) {
    unload();
    return false;
  }

  active_host = this;
  const auto description = init(this);
  active_host = nullptr;
  plugin_exit_ = reinterpret_cast<PluginExit>(dlsym(library_, plugin_exit_name));
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
  active_host = this;
  found->second(state);
  active_host = nullptr;
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
