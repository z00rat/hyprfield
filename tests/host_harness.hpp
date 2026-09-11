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
  std::string hash = "efb50993780079460b0cbed1363e2166a2de1d9f";
  std::string tag;
  std::string branch;
  bool dirty = false;
};

struct Function {
  std::string demangled;
};

struct Command {
  std::string call;
  std::string args;
};

class HostHarness {
 public:
  using LuaFunction = int (*)(lua_State*);

  bool loadPlugin(const std::string& path, std::string_view expected_version);
  void setHostVersion(std::string_view tag, bool dirty = false);
  void setFunction(std::string_view demangled);
  void setHookRegistration(bool succeeds);
  void setMonitor(std::string_view name);
  void setWorkspace(int workspace, std::string_view monitor);
  void setClient(std::string_view identity);
  void setCommandResults(bool succeeds);
  void unload();
  void registerLuaFunction(std::string_view namespace_, std::string_view name, LuaFunction function);
  void recordNotification(std::string_view text);
  bool invokeLua(std::string_view namespace_, std::string_view name, std::string_view argument);
  bool invokeLua(std::string_view namespace_, std::string_view name, const std::vector<std::string>& arguments);

  [[nodiscard]] bool loaded() const;
  [[nodiscard]] const std::string& pluginName() const;
  [[nodiscard]] const std::vector<Notification>& notifications() const;
  [[nodiscard]] size_t hookCount() const;

  [[nodiscard]] HostVersion hostVersion() const;
  [[nodiscard]] const std::vector<Function>& functions() const;
  [[nodiscard]] const std::vector<Command>& commands() const;
  [[nodiscard]] bool hookRegistrationSucceeds() const;
  [[nodiscard]] std::string invokeHyprctl(std::string_view call, std::string_view args);
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
  std::vector<Command> commands_;
  bool hook_registration_succeeds_ = true;
  bool command_results_succeed_ = true;
  size_t active_hooks_ = 0;
  std::vector<std::string> monitors_;
  std::vector<std::pair<int, std::string>> workspaces_;
  std::vector<std::string> clients_;
};

}  // namespace hyprfield::testing
