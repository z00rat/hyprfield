#include "host_harness.hpp"

#include <dlfcn.h>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

CHyprColor::CHyprColor(float red, float green, float blue, float alpha) : r(red), g(green), b(blue), a(alpha) {}
Log::CLogger::CLogger() = default;

Hyprutils::CLI::CLogger::CLogger() {}

namespace {

hyprfield::testing::HostHarness* active_host = nullptr;

}  // namespace

CFunctionHook::CFunctionHook(HANDLE owner, void* source, void* destination)
    : m_source(source), m_destination(destination), m_owner(owner) {}
CFunctionHook::~CFunctionHook() = default;
bool CFunctionHook::hook() {
  return true;
}
bool CFunctionHook::unhook() {
  return true;
}

namespace HyprlandAPI {

extern "C" SVersionInfo getHyprlandVersion(void* handle) {
  const auto version = static_cast<hyprfield::testing::HostHarness*>(handle)->hostVersion();
  return {.hash = version.hash, .tag = version.tag, .dirty = version.dirty, .branch = version.branch};
}

extern "C" std::vector<SFunctionMatch> findFunctionsByName(void* handle, const std::string&) {
  std::vector<SFunctionMatch> result;
  for (const auto& function : static_cast<hyprfield::testing::HostHarness*>(handle)->functions())
    result.push_back({.address = handle, .demangled = function.demangled});
  return result;
}

extern "C" CFunctionHook* createFunctionHook(void* handle, const void*, const void*) {
  auto* host = static_cast<hyprfield::testing::HostHarness*>(handle);
  if (!host->hookRegistrationSucceeds())
    return nullptr;
  host->incrementActiveHook();
  return new CFunctionHook(handle, nullptr, nullptr);
}

extern "C" bool removeFunctionHook(void* handle, CFunctionHook* hook) {
  auto* host = static_cast<hyprfield::testing::HostHarness*>(handle);
  delete hook;
  host->decrementActiveHook();
  return true;
}

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
  if (description.name.empty()) {
    unload();
    return false;
  }
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

void HostHarness::setHostVersion(std::string_view tag, bool dirty) {
  host_version_.tag = tag;
  host_version_.dirty = dirty;
}

void HostHarness::setFunction(std::string_view demangled) {
  functions_.push_back({.demangled = std::string{demangled}});
}

void HostHarness::setHookRegistration(bool succeeds) {
  hook_registration_succeeds_ = succeeds;
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

size_t HostHarness::hookCount() const {
  return active_hooks_;
}

HostVersion HostHarness::hostVersion() const {
  return host_version_;
}

const std::vector<Function>& HostHarness::functions() const {
  return functions_;
}

bool HostHarness::hookRegistrationSucceeds() const {
  return hook_registration_succeeds_;
}

void HostHarness::incrementActiveHook() {
  ++active_hooks_;
}

void HostHarness::decrementActiveHook() {
  if (active_hooks_ > 0)
    --active_hooks_;
}

}  // namespace hyprfield::testing
