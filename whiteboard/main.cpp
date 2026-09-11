#include <algorithm>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <string>
#include <string_view>
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
constexpr std::string_view kInputMethod = "processMouseMove";

HANDLE pluginHandle = nullptr;
std::vector<CFunctionHook*> hooks;

void rendererProofHook() {}
void inputProofHook() {}

void report(std::string_view reason) {
  HyprlandAPI::addNotification(pluginHandle,
                               "[whiteboard] compatibility gate: " + std::string{reason},
                               CHyprColor{1.0F, 0.75F, 0.1F, 1.0F},
                               10000.0F);
}

bool supportedHost() {
  const auto version = HyprlandAPI::getHyprlandVersion(pluginHandle);
  auto tag = version.tag;
  if (tag.starts_with('v'))
    tag.erase(0, 1);
  return tag == HYPRFIELD_HYPRLAND_VERSION_PIN && !version.dirty;
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

}  // namespace

APICALL EXPORT void PLUGIN_EXIT();

APICALL EXPORT std::string PLUGIN_API_VERSION() {
  return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  if (!hooks.empty())
    PLUGIN_EXIT();
  pluginHandle = handle;
  if (!supportedHost()) {
    report("unsupported or unverified host version");
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
  pluginHandle = nullptr;
}
