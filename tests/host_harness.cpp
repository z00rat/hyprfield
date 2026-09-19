#include "host_harness.hpp"

#include <dlfcn.h>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/ElementRenderer.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
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

namespace Event {

UP<CEventBus>& bus() {
  static auto event_bus = makeUnique<CEventBus>();
  return event_bus;
}

}  // namespace Event

CFunctionHook::CFunctionHook(HANDLE owner, void* source, void* destination)
    : m_source(source), m_destination(destination), m_owner(owner) {}
CFunctionHook::~CFunctionHook() = default;
bool CFunctionHook::hook() {
  return true;
}
bool CFunctionHook::unhook() {
  return true;
}

Vector2D CInputManager::getMouseCoordsInternal() {
  return {};
}

void Render::IHyprRenderer::damageBox(const int&, const int&, const int&, const int&) {}
void Render::IElementRenderer::drawElement(WP<IPassElement>, const CRegion&) {}
CTexPassElement::CTexPassElement(SRenderData&& data) : m_data(std::move(data)) {}
std::vector<UP<IPassElement>> IPassElement::draw() {
  return {};
}
void IPassElement::discard() {}
bool IPassElement::undiscardable() {
  return false;
}
std::optional<CBox> IPassElement::boundingBox() {
  return std::nullopt;
}
CRegion IPassElement::opaqueRegion() {
  return {};
}
bool IPassElement::disableSimplification() {
  return false;
}
bool CTexPassElement::needsLiveBlur() {
  return false;
}
bool CTexPassElement::needsPrecomputeBlur() {
  return false;
}
std::optional<CBox> CTexPassElement::boundingBox() {
  return m_data.box;
}
CRegion CTexPassElement::opaqueRegion() {
  return {};
}
void CTexPassElement::discard() {}

namespace NColorManagement {

const SPCPRimaries& getPrimaries(ePrimaries) {
  static const SPCPRimaries primaries{};
  return primaries;
}

WP<const CImageDescription> CImageDescription::from(const SImageDescription&) {
  return nullptr;
}

}  // namespace NColorManagement

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

extern "C" std::string invokeHyprctlCommand(const std::string& call, const std::string& args, const std::string&) {
  return active_host->invokeHyprctl(call, args);
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

void HostHarness::setMonitor(std::string_view name) {
  monitors_.push_back(std::string{name});
  monitor_geometry_.push_back({.name = std::string{name}});
}

void HostHarness::setMonitorGeometry(std::string_view name, int x, int y, int width, int height) {
  const auto found =
      std::ranges::find_if(monitor_geometry_, [name](const auto& monitor) { return monitor.name == name; });
  if (found == monitor_geometry_.end()) {
    monitor_geometry_.push_back({.name = std::string{name}, .x = x, .y = y, .width = width, .height = height});
    monitors_.push_back(std::string{name});
    return;
  }
  found->x = x;
  found->y = y;
  found->width = width;
  found->height = height;
}

void HostHarness::setWorkspace(int workspace, std::string_view monitor) {
  workspaces_.emplace_back(workspace, std::string{monitor});
}

void HostHarness::setClient(std::string_view identity, int workspace, std::string_view monitor) {
  clients_.push_back({.identity = std::string{identity}, .workspace = workspace, .monitor = std::string{monitor}});
}

void HostHarness::closeClient(std::string_view identity) {
  std::erase_if(clients_, [identity](const auto& client) { return client.identity == identity; });
  active_host = this;
  Event::bus()->m_events.window.close.emit(PHLWINDOW{});
  active_host = nullptr;
}

void HostHarness::removeMonitor(std::string_view name) {
  std::erase(monitors_, std::string{name});
  std::erase_if(monitor_geometry_, [name](const auto& monitor) { return monitor.name == name; });
  active_host = this;
  Event::bus()->m_events.monitor.removed.emit(PHLMONITOR{});
  active_host = nullptr;
}

void HostHarness::removeWorkspace(int workspace) {
  std::erase_if(workspaces_, [workspace](const auto& entry) { return entry.first == workspace; });
  active_host = this;
  Event::bus()->m_events.workspace.removed.emit(PHLWORKSPACEREF{});
  active_host = nullptr;
}

void HostHarness::setCommandResults(bool succeeds) {
  command_results_succeed_ = succeeds;
}

void HostHarness::registerLuaFunction(std::string_view namespace_, std::string_view name, LuaFunction function) {
  lua_functions_.emplace_back(std::string{namespace_} + "." + std::string{name}, function);
}

void HostHarness::recordNotification(std::string_view text) {
  notifications_.push_back({.text = std::string{text}});
}

bool HostHarness::invokeLua(std::string_view namespace_, std::string_view name, std::string_view argument) {
  return invokeLua(namespace_, name, std::vector<std::string>{std::string{argument}});
}

bool HostHarness::invokeLua(std::string_view namespace_,
                            std::string_view name,
                            const std::vector<std::string>& arguments) {
  const auto key = std::string{namespace_} + "." + std::string{name};
  const auto found = std::ranges::find_if(lua_functions_, [&key](const auto& entry) { return entry.first == key; });
  if (found == lua_functions_.end()) {
    return false;
  }
  lua_State* state = luaL_newstate();
  lua_pushcfunction(state, found->second);
  for (const auto& argument : arguments)
    lua_pushlstring(state, argument.data(), argument.size());
  active_host = this;
  const auto status = lua_pcall(state, static_cast<int>(arguments.size()), LUA_MULTRET, 0);
  const auto result = status == LUA_OK && (lua_gettop(state) == 0 || lua_toboolean(state, -1) != 0);
  active_host = nullptr;
  lua_close(state);
  return result;
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

const std::string& HostHarness::focusedClient() const {
  return focused_client_;
}

HostVersion HostHarness::hostVersion() const {
  return host_version_;
}

const std::vector<Function>& HostHarness::functions() const {
  return functions_;
}

const std::vector<Command>& HostHarness::commands() const {
  return commands_;
}

bool HostHarness::hookRegistrationSucceeds() const {
  return hook_registration_succeeds_;
}

std::string HostHarness::invokeHyprctl(std::string_view call, std::string_view args) {
  commands_.push_back({.call = std::string{call}, .args = std::string{args}});
  if (!command_results_succeed_ && call == "dispatch")
    return "error";
  if (call == "dispatch") {
    const auto separator = args.find(' ');
    if (separator != std::string_view::npos && args.substr(0, separator) == "moveworkspacetomonitor") {
      const auto workspace_separator = args.find(' ', separator + 1);
      if (workspace_separator != std::string_view::npos) {
        const auto workspace = std::stoi(std::string{args.substr(separator + 1, workspace_separator - separator - 1)});
        const auto monitor = std::string{args.substr(workspace_separator + 1)};
        for (auto& entry : workspaces_) {
          if (entry.first == workspace)
            entry.second = monitor;
        }
      }
    }
    if (args.starts_with("movetoworkspacesilent ")) {
      const auto separator = args.find(',');
      if (separator != std::string_view::npos) {
        constexpr std::string_view prefix = "movetoworkspacesilent ";
        const auto workspace = std::stoi(std::string{args.substr(prefix.size(), separator - prefix.size())});
        const auto address = std::string{args.substr(separator + 1)};
        for (auto& client : clients_)
          if (client.identity == address)
            client.workspace = workspace;
      }
    }
    if (args.starts_with("focuswindow "))
      focused_client_ = std::string{args.substr(std::string_view{"focuswindow "}.size())};
    if (args.starts_with("hl.focus({window=\"")) {
      constexpr std::string_view prefix = "hl.focus({window=\"";
      const auto end = args.find('"', prefix.size());
      if (end != std::string_view::npos)
        focused_client_ = std::string{args.substr(prefix.size(), end - prefix.size())};
    }
    if (args.starts_with("togglefloating ")) {
      const auto address = std::string{args.substr(std::string_view{"togglefloating "}.size())};
      for (auto& client : clients_)
        if (client.identity == address)
          client.floating = !client.floating;
    }
    return "ok";
  }
  if (call == "workspaces") {
    std::string result = "[";
    for (size_t index = 0; index < workspaces_.size(); ++index) {
      if (index != 0)
        result += ',';
      result += "{\"id\": " + std::to_string(workspaces_[index].first) + ", \"monitor\": \"" + workspaces_[index].second
                + "\"}";
    }
    return result + ']';
  }
  if (call == "monitors") {
    std::string result = "[";
    for (size_t index = 0; index < monitors_.size(); ++index) {
      if (index != 0)
        result += ',';
      const auto geometry = std::ranges::find_if(
          monitor_geometry_, [this, index](const auto& monitor) { return monitor.name == monitors_[index]; });
      const auto x = geometry == monitor_geometry_.end() ? 0 : geometry->x;
      const auto y = geometry == monitor_geometry_.end() ? 0 : geometry->y;
      const auto width = geometry == monitor_geometry_.end() ? 1920 : geometry->width;
      const auto height = geometry == monitor_geometry_.end() ? 1080 : geometry->height;
      result += "{\"name\": \"" + monitors_[index] + "\", \"x\": " + std::to_string(x) + ", \"y\": " + std::to_string(y)
                + ", \"width\": " + std::to_string(width) + ", \"height\": " + std::to_string(height) + "}";
    }
    return result + ']';
  }
  if (call == "clients") {
    std::string result = "[";
    for (size_t index = 0; index < clients_.size(); ++index) {
      if (index != 0)
        result += ',';
      result += "{\"address\": \"" + clients_[index].identity + "\", \"monitor\": \"" + clients_[index].monitor
                + "\", \"workspace\": {\"id\": " + std::to_string(clients_[index].workspace)
                + "}, \"floating\": " + (clients_[index].floating ? "true" : "false") + "}";
    }
    return result + ']';
  }
  static_cast<void>(args);
  return "error";
}

void HostHarness::incrementActiveHook() {
  ++active_hooks_;
}

void HostHarness::decrementActiveHook() {
  if (active_hooks_ > 0)
    --active_hooks_;
}

}  // namespace hyprfield::testing
