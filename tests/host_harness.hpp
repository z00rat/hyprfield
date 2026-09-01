#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct lua_State;

namespace hyprfield::testing {

struct Notification {
  std::string text;
};

struct Command {
  std::string name;
  std::string arguments;
  std::string format;
};

class HostHarness {
 public:
  using LuaFunction = int (*)(lua_State*);

  bool loadPlugin(const std::string& path, std::string_view expected_version);
  void unload();
  void registerLuaFunction(std::string_view namespace_, std::string_view name, LuaFunction function);
  void recordNotification(std::string_view text);
  std::string invokeHyprctlCommand(const std::string& name, const std::string& arguments, const std::string& format);
  bool invokeLua(std::string_view namespace_, std::string_view name, std::string_view argument);

  [[nodiscard]] bool loaded() const;
  [[nodiscard]] const std::string& pluginName() const;
  [[nodiscard]] const std::vector<Notification>& notifications() const;
  [[nodiscard]] const std::vector<Command>& commands() const;

 private:
  std::string plugin_name_;
  void* library_ = nullptr;
  void (*plugin_exit_)() = nullptr;
  std::vector<Notification> notifications_;
  std::vector<Command> commands_;
  std::vector<std::pair<std::string, LuaFunction>> lua_functions_;
};

}  // namespace hyprfield::testing
