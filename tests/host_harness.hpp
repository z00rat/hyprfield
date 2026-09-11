#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct lua_State;

namespace hyprfield::testing {

struct Notification {
  std::string text;
};

struct HostVersion {
  std::string tag;
  bool dirty = false;
};

struct Function {
  std::string demangled;
};

class HostHarness {
 public:
  using LuaFunction = int (*)(lua_State*);

  bool loadPlugin(const std::string& path, std::string_view expected_version);
  void setHostVersion(std::string_view tag, bool dirty = false);
  void setFunction(std::string_view demangled);
  void setHookRegistration(bool succeeds);
  void unload();
  void registerLuaFunction(std::string_view namespace_, std::string_view name, LuaFunction function);
  void recordNotification(std::string_view text);
  bool invokeLua(std::string_view namespace_, std::string_view name, std::string_view argument);

  [[nodiscard]] bool loaded() const;
  [[nodiscard]] const std::string& pluginName() const;
  [[nodiscard]] const std::vector<Notification>& notifications() const;
  [[nodiscard]] size_t hookCount() const;

  [[nodiscard]] HostVersion hostVersion() const;
  [[nodiscard]] const std::vector<Function>& functions() const;
  [[nodiscard]] bool hookRegistrationSucceeds() const;
  void incrementActiveHook();
  void decrementActiveHook();

 private:
  std::string plugin_name_;
  void* library_ = nullptr;
  void (*plugin_exit_)() = nullptr;
  std::vector<Notification> notifications_;
  std::vector<std::pair<std::string, LuaFunction>> lua_functions_;
  HostVersion host_version_{.tag = "0.56.2"};
  std::vector<Function> functions_;
  bool hook_registration_succeeds_ = true;
  size_t active_hooks_ = 0;
};

}  // namespace hyprfield::testing
