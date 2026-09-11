#include <algorithm>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <string>
#include <string_view>
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
std::unordered_map<int, std::string> activeBoards;

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
  if (existing != activeBoards.end() && existing->second != monitor) {
    return activationFailure(state,
                             "workspace " + std::to_string(workspace) + " is already assigned to " + existing->second);
  }

  if (!jsonContainsMonitor(HyprlandAPI::invokeHyprctlCommand("monitors", "-j"), monitor)) {
    return activationFailure(state, "monitor " + monitor + " was not found");
  }
  if (!jsonContainsWorkspace(HyprlandAPI::invokeHyprctlCommand("workspaces", "-j"), static_cast<int>(workspace))) {
    return activationFailure(state, "workspace " + std::to_string(workspace) + " was not found");
  }
  const auto dispatch = HyprlandAPI::invokeHyprctlCommand(
      "dispatch", "moveworkspacetomonitor " + std::to_string(workspace) + " " + monitor);
  if (!dispatch.starts_with("ok")) {
    return activationFailure(state, "compositor command failed");
  }
  if (!jsonContainsWorkspace(HyprlandAPI::invokeHyprctlCommand("workspaces", "-j"), workspace, monitor)) {
    return activationFailure(state, "workspace verification failed");
  }
  activeBoards[static_cast<int>(workspace)] = monitor;
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
  const auto active = monitorValue != nullptr && monitorLength > 0 && isWorkspace && workspace > 0
                      && activeBoards.contains(static_cast<int>(workspace))
                      && activeBoards.at(static_cast<int>(workspace)) == std::string{monitorValue, monitorLength};
  lua_pushboolean(state, active);
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
  HyprlandAPI::addNotification(
      pluginHandle, "[whiteboard] compatibility proof ready", CHyprColor{0.2F, 1.0F, 0.4F, 1.0F}, 5000.0F);
  return {.name = "whiteboard",
          .description = "Whiteboard compatibility proof seam",
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
