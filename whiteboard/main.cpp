#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace {

constexpr std::string_view kPluginName = "whiteboard";
constexpr std::string_view kRenderClass = "IElementRenderer";
constexpr std::string_view kRenderMethod = "drawSurface";
constexpr std::string_view kInputClass = "CInputManager";
constexpr std::string_view kInputMethod = "onMouseMoved";

HANDLE pluginHandle = nullptr;
std::vector<CFunctionHook*> hooks;

struct Board {
  std::string monitor;
  int workspace;
  float zoom = 1.0F;
  std::unordered_map<std::string, std::string> clients;
};

std::unordered_map<int, Board> activeBoards;

std::filesystem::path statePath() {
  const char* stateHome = std::getenv("XDG_STATE_HOME");
  const auto root = stateHome == nullptr || std::string_view{stateHome}.empty()
                        ? std::filesystem::path{std::getenv("HOME") == nullptr ? "." : std::getenv("HOME")}
                        : std::filesystem::path{stateHome};
  return root / "hyprfield" / "whiteboard.state";
}

void rendererProofHook(...) {}
void inputProofHook(...) {}

void report(std::string_view reason) {
  HyprlandAPI::addNotification(pluginHandle,
                               "[whiteboard] compatibility gate: " + std::string{reason},
                               CHyprColor{1.0F, 0.75F, 0.1F, 1.0F},
                               10000.0F);
}

bool supportedHost() {
  const auto version = HyprlandAPI::getHyprlandVersion(pluginHandle);
  auto tag = version.tag.empty() ? version.branch : version.tag;
  if (tag.starts_with('v'))
    tag.erase(0, 1);
  if (tag != HYPRFIELD_HYPRLAND_VERSION_PIN || version.hash != HYPRFIELD_HYPRLAND_COMMIT_PIN) {
    report("host check failed: raw-tag=" + version.tag + ", branch=" + version.branch + ", hash=" + version.hash
           + ", dirty=" + std::string{version.dirty ? "true" : "false"} + ", selected=" + tag);
    return false;
  }
  return true;
}

CFunctionHook* installHook(std::string_view className, std::string_view methodName) {
  const auto matches = HyprlandAPI::findFunctionsByName(pluginHandle, std::string{methodName});
  const auto found = std::ranges::find_if(matches, [className, methodName](const auto& match) {
    return match.address != nullptr
           && match.demangled.contains(std::string{className} + "::" + std::string{methodName} + "(");
  });
  if (found == matches.end()) {
    report("missing " + std::string{className} + "::" + std::string{methodName});
    return nullptr;
  }

  const auto destination = methodName == kRenderMethod ? reinterpret_cast<const void*>(&rendererProofHook)
                                                       : reinterpret_cast<const void*>(&inputProofHook);
  auto* hook = HyprlandAPI::createFunctionHook(pluginHandle, found->address, destination);
  if (hook == nullptr || !hook->hook()) {
    report("failed to register " + std::string{className} + "::" + std::string{methodName});
    return nullptr;
  }
  hooks.push_back(hook);
  return hook;
}

int proofLua(lua_State* state) {
  const auto* monitor = luaL_optstring(state, 1, "current");
  HyprlandAPI::addNotification(pluginHandle,
                               "[whiteboard proof] monitor-scoped temporary artifact on " + std::string{monitor},
                               CHyprColor{0.2F, 0.8F, 1.0F, 1.0F},
                               2500.0F);
  return 0;
}

int activationFailure(lua_State* state, std::string_view reason) {
  const auto message = "[whiteboard] activation failed: " + std::string{reason};
  HyprlandAPI::addNotification(pluginHandle, message, CHyprColor{1.0F, 0.35F, 0.2F, 1.0F}, 5000.0F);
  return luaL_error(state, "%s", message.c_str());
}

std::optional<std::reference_wrapper<Board>> findBoard(std::string_view monitor, int workspace) {
  const auto found = activeBoards.find(workspace);
  if (found == activeBoards.end() || found->second.monitor != monitor)
    return std::nullopt;
  return found->second;
}

bool jsonContainsMonitor(const std::string& json, std::string_view monitor) {
  return json.contains("\"name\":\"" + std::string{monitor} + "\"");
}

bool jsonContainsWorkspace(const std::string& json, int workspace, std::string_view monitor) {
  const auto id = "\"id\":" + std::to_string(workspace);
  auto objectStart = json.find(id);
  while (objectStart != std::string::npos) {
    const auto idEnd = objectStart + id.size();
    if (idEnd < json.size() && json[idEnd] >= '0' && json[idEnd] <= '9') {
      objectStart = json.find(id, idEnd);
      continue;
    }
    const auto objectEnd = json.find('}', objectStart);
    if (objectEnd == std::string::npos)
      return false;
    const auto monitorField = json.find("\"monitor\":\"" + std::string{monitor} + "\"", objectStart);
    if (monitorField != std::string::npos && monitorField < objectEnd)
      return true;
    objectStart = json.find(id, objectEnd + 1);
  }
  return false;
}

bool jsonContainsWorkspace(const std::string& json, int workspace) {
  return json.contains("\"id\":" + std::to_string(workspace));
}

bool jsonContainsClient(const std::string& json, std::string_view identity) {
  return json.contains("\"address\":\"" + std::string{identity} + "\"");
}

bool jsonClientBelongsToBoard(const std::string& json,
                              std::string_view identity,
                              std::string_view monitor,
                              int workspace) {
  const auto address = json.find("\"address\":\"" + std::string{identity} + "\"");
  if (address == std::string::npos)
    return false;
  const auto objectStart = json.rfind('{', address);
  const auto objectEnd = json.find('}', address);
  if (objectStart == std::string::npos || objectEnd == std::string::npos)
    return false;
  const auto object = json.substr(objectStart, objectEnd - objectStart);
  return object.contains("\"monitor\":\"" + std::string{monitor} + "\"")
         && object.contains("\"workspace\":{\"id\":" + std::to_string(workspace));
}

int activateLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int isWorkspace = 0;
  const auto workspace = lua_tointegerx(state, 2, &isWorkspace);
  if (monitorValue == nullptr || monitorLength == 0 || !isWorkspace || workspace <= 0) {
    return activationFailure(state, "monitor and positive workspace are required");
  }
  const std::string monitor{monitorValue, monitorLength};
  const auto existing = activeBoards.find(static_cast<int>(workspace));
  if (existing != activeBoards.end() && existing->second.monitor != monitor) {
    return activationFailure(
        state, "workspace " + std::to_string(workspace) + " is already assigned to " + existing->second.monitor);
  }

  if (!jsonContainsMonitor(HyprlandAPI::invokeHyprctlCommand("monitors", "-j"), monitor)) {
    return activationFailure(state, "monitor " + monitor + " was not found");
  }
  const auto workspaces = HyprlandAPI::invokeHyprctlCommand("workspaces", "-j");
  if (!jsonContainsWorkspace(workspaces, static_cast<int>(workspace))) {
    return activationFailure(state, "workspace " + std::to_string(workspace) + " was not found");
  }
  if (!jsonContainsWorkspace(workspaces, static_cast<int>(workspace), monitor)
      && !jsonContainsWorkspace(workspaces, static_cast<int>(workspace), "")) {
    return activationFailure(state, "workspace " + std::to_string(workspace) + " is assigned to another monitor");
  }
  const auto dispatch = HyprlandAPI::invokeHyprctlCommand(
      "dispatch", "moveworkspacetomonitor " + std::to_string(workspace) + " " + monitor);
  if (!dispatch.starts_with("ok")) {
    return activationFailure(state, "compositor command failed");
  }
  if (!jsonContainsWorkspace(HyprlandAPI::invokeHyprctlCommand("workspaces", "-j"), workspace, monitor)) {
    return activationFailure(state, "workspace verification failed");
  }
  activeBoards.try_emplace(static_cast<int>(workspace),
                           Board{.monitor = monitor, .workspace = static_cast<int>(workspace)});
  HyprlandAPI::addNotification(pluginHandle,
                               "[whiteboard] workspace " + std::to_string(workspace) + " active on " + monitor,
                               CHyprColor{0.2F, 0.8F, 1.0F, 1.0F},
                               5000.0F);
  lua_pushboolean(state, true);
  return 1;
}

int activeLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int isWorkspace = 0;
  const auto workspace = lua_tointegerx(state, 2, &isWorkspace);
  const auto active =
      monitorValue != nullptr && monitorLength > 0 && isWorkspace && workspace > 0
      && activeBoards.contains(static_cast<int>(workspace))
      && activeBoards.at(static_cast<int>(workspace)).monitor == std::string{monitorValue, monitorLength};
  lua_pushboolean(state, active);
  return 1;
}

int registerClientLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int isWorkspace = 0;
  const auto workspace = lua_tointegerx(state, 2, &isWorkspace);
  size_t identityLength = 0;
  const auto* identityValue = lua_tolstring(state, 3, &identityLength);
  if (monitorValue == nullptr || monitorLength == 0 || !isWorkspace || workspace <= 0 || identityValue == nullptr
      || identityLength == 0)
    return activationFailure(state, "monitor, positive workspace, and client identity are required");

  const std::string monitor{monitorValue, monitorLength};
  const std::string identity{identityValue, identityLength};
  const auto clients = HyprlandAPI::invokeHyprctlCommand("clients", "-j");
  if (!jsonContainsClient(clients, identity))
    return activationFailure(state, "client identity was not found");
  if (!jsonClientBelongsToBoard(clients, identity, monitor, static_cast<int>(workspace)))
    return activationFailure(state, "client is not on the requested board");
  const auto board = findBoard(monitor, static_cast<int>(workspace));
  if (!board)
    return activationFailure(state, "client belongs to an inactive board");
  const auto alreadyRegistered = std::ranges::any_of(
      activeBoards, [&identity](const auto& entry) { return entry.second.clients.contains(identity); });
  if (alreadyRegistered)
    return activationFailure(state, "client identity is already registered");
  board->get().clients.emplace(identity, monitor);
  lua_pushboolean(state, true);
  return 1;
}

int clientActiveLua(lua_State* state) {
  size_t identityLength = 0;
  const auto* identityValue = lua_tolstring(state, 1, &identityLength);
  bool active = identityValue != nullptr && identityLength != 0;
  const std::string identity{identityValue == nullptr ? "" : identityValue, identityLength};
  if (active) {
    active = std::ranges::any_of(activeBoards,
                                 [&identity](const auto& entry) { return entry.second.clients.contains(identity); });
  }
  lua_pushboolean(state, active);
  return 1;
}

int closeClientLua(lua_State* state) {
  size_t identityLength = 0;
  const auto* identityValue = lua_tolstring(state, 1, &identityLength);
  if (identityValue == nullptr || identityLength == 0)
    return activationFailure(state, "client identity is required");
  const std::string identity{identityValue, identityLength};
  for (auto& [_, board] : activeBoards) {
    if (board.clients.erase(identity) != 0) {
      lua_pushboolean(state, true);
      return 1;
    }
  }
  return activationFailure(state, "client identity was not registered");
}

int setZoomLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int isWorkspace = 0;
  const auto workspace = lua_tointegerx(state, 2, &isWorkspace);
  int isZoom = 0;
  const auto zoom = static_cast<float>(lua_tonumberx(state, 3, &isZoom));
  if (monitorValue == nullptr || monitorLength == 0 || !isWorkspace || workspace <= 0 || !isZoom || zoom <= 0.0F)
    return activationFailure(state, "monitor, positive workspace, and positive zoom are required");
  const auto board = findBoard(std::string_view{monitorValue, monitorLength}, static_cast<int>(workspace));
  if (!board)
    return activationFailure(state, "board is not active");
  board->get().zoom = std::clamp(zoom, 0.5F, 1.0F);
  lua_pushboolean(state, true);
  return 1;
}

int normalLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int isWorkspace = 0;
  const auto workspace = lua_tointegerx(state, 2, &isWorkspace);
  const auto board = monitorValue == nullptr || !isWorkspace
                         ? std::optional<std::reference_wrapper<Board>>{}
                         : findBoard(std::string_view{monitorValue, monitorLength}, static_cast<int>(workspace));
  lua_pushboolean(state, board && board->get().zoom == 1.0F);
  return 1;
}

int managementLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int isWorkspace = 0;
  const auto workspace = lua_tointegerx(state, 2, &isWorkspace);
  const auto board = monitorValue == nullptr || !isWorkspace
                         ? std::optional<std::reference_wrapper<Board>>{}
                         : findBoard(std::string_view{monitorValue, monitorLength}, static_cast<int>(workspace));
  lua_pushboolean(state, board && board->get().zoom < 0.9F);
  return 1;
}

void persist() {
  std::error_code error;
  std::filesystem::create_directories(statePath().parent_path(), error);
  std::ofstream state{statePath()};
  if (!state)
    return;
  for (const auto& [_, board] : activeBoards) {
    state << board.monitor << '\t' << board.workspace << '\t' << board.zoom << '\n';
    for (const auto& [identity, _] : board.clients)
      state << "client\t" << board.workspace << '\t' << identity << '\n';
  }
}

void restore() {
  std::ifstream state{statePath()};
  if (!state)
    return;
  std::string line;
  while (std::getline(state, line)) {
    const auto first = line.find('\t');
    const auto second = line.find('\t', first + 1);
    if (first == std::string::npos || second == std::string::npos)
      continue;
    if (line.starts_with("client\t")) {
      try {
        const auto workspace = std::stoi(line.substr(first + 1, second - first - 1));
        const auto board = activeBoards.find(workspace);
        if (board != activeBoards.end() && !line.substr(second + 1).empty()
            && jsonClientBelongsToBoard(HyprlandAPI::invokeHyprctlCommand("clients", "-j"),
                                        line.substr(second + 1),
                                        board->second.monitor,
                                        workspace))
          board->second.clients.emplace(line.substr(second + 1), board->second.monitor);
      } catch (const std::exception&) {
        report("ignored invalid client persistence record");
      }
      continue;
    }
    try {
      const auto workspace = std::stoi(line.substr(first + 1, second - first - 1));
      const auto zoom = std::stof(line.substr(second + 1));
      const auto monitor = line.substr(0, first);
      if (jsonContainsMonitor(HyprlandAPI::invokeHyprctlCommand("monitors", "-j"), monitor)
          && jsonContainsWorkspace(HyprlandAPI::invokeHyprctlCommand("workspaces", "-j"), workspace, monitor))
        activeBoards.emplace(workspace,
                             Board{.monitor = monitor, .workspace = workspace, .zoom = std::clamp(zoom, 0.5F, 1.0F)});
    } catch (const std::exception&) {
      report("ignored invalid board persistence record");
    }
  }
}

int saveLua(lua_State* state) {
  static_cast<void>(state);
  persist();
  lua_pushboolean(state, true);
  return 1;
}

int deactivateLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int isWorkspace = 0;
  const auto workspace = lua_tointegerx(state, 2, &isWorkspace);
  if (monitorValue == nullptr || monitorLength == 0 || !isWorkspace || workspace <= 0)
    return activationFailure(state, "monitor and positive workspace are required");
  const auto board = findBoard(std::string_view{monitorValue, monitorLength}, static_cast<int>(workspace));
  if (!board)
    return activationFailure(state, "board is not active");
  activeBoards.erase(static_cast<int>(workspace));
  lua_pushboolean(state, true);
  return 1;
}

}  // namespace

APICALL EXPORT void PLUGIN_EXIT();

APICALL EXPORT std::string PLUGIN_API_VERSION() {
  return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  if (!hooks.empty())
    PLUGIN_EXIT();
  pluginHandle = handle;
  activeBoards.clear();
  if (!supportedHost()) {
    pluginHandle = nullptr;
    return {};
  }

  if (installHook(kRenderClass, kRenderMethod) == nullptr || installHook(kInputClass, kInputMethod) == nullptr) {
    for (auto* hook : hooks)
      HyprlandAPI::removeFunctionHook(pluginHandle, hook);
    hooks.clear();
    report("registration aborted; no compatibility seam installed");
    pluginHandle = nullptr;
    return {};
  }

  if (!HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "proof", proofLua)) {
    report("failed to register proof API");
    PLUGIN_EXIT();
    return {};
  }
  if (!HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "activate", activateLua)) {
    report("failed to register activation API");
    PLUGIN_EXIT();
    return {};
  }
  if (!HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "active", activeLua)) {
    report("failed to register active-state API");
    PLUGIN_EXIT();
    return {};
  }
  if (!HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "registerClient", registerClientLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "clientActive", clientActiveLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "closeClient", closeClientLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "deactivate", deactivateLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "setZoom", setZoomLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "normal", normalLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "management", managementLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "save", saveLua)) {
    report("failed to register workspace model API");
    PLUGIN_EXIT();
    return {};
  }
  restore();
  HyprlandAPI::addNotification(
      pluginHandle, "[whiteboard] compatibility proof ready", CHyprColor{0.2F, 1.0F, 0.4F, 1.0F}, 5000.0F);
  return {.name = "whiteboard",
          .description = "Monitor-assigned Whiteboard workspace model",
          .author = "zurat",
          .version = "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
  for (auto* hook : hooks)
    HyprlandAPI::removeFunctionHook(pluginHandle, hook);
  hooks.clear();
  activeBoards.clear();
  pluginHandle = nullptr;
}
