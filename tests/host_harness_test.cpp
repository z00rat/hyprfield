#include "host_harness.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

void expect(bool condition) {
  if (!condition) {
    std::abort();
  }
}

void verify_host_lifecycle(const std::string& plugin_path) {
  hyprfield::testing::HostHarness host;
  expect(host.loadPlugin(plugin_path, "0.1"));
  expect(host.loaded());
  expect(host.pluginName() == "hello");
  expect(host.notifications().size() == 1);
  expect(host.notifications().front().text == "hello plugin loaded");
  expect(host.invokeLua("hello", "say", "world"));
  expect(host.notifications().size() == 2);
  expect(host.notifications().back().text == "Hello, world!");
  expect(!host.invokeLua("hello", "missing", "world"));

  host.unload();
  expect(!host.loaded());
  expect(!host.invokeLua("hello", "say", "after unload"));
}

void verify_whiteboard(const std::string& plugin_path) {
  hyprfield::testing::HostHarness host;
  host.setFunction("IHyprRenderer::renderWorkspaceWindows(PHLMONITOR, PHLWORKSPACE, Time::steady_tp const&)");
  host.setFunction("CInputManager::onMouseMoved(IPointer::SMotionEvent)");
  expect(host.loadPlugin(plugin_path, "0.1"));
  expect(host.hookCount() == 2);
  expect(host.notifications().back().text == "[whiteboard] workspace model ready; camera renderer active");
  expect(host.invokeLua("whiteboard", "proof", "DP-1"));
  expect(host.notifications().back().text == "[whiteboard proof] monitor-scoped temporary artifact on DP-1");
  host.unload();
  expect(host.hookCount() == 0);
  expect(!host.loaded());

  hyprfield::testing::HostHarness unsupported;
  unsupported.setHostVersion("0.56.1");
  expect(!unsupported.loadPlugin(plugin_path, "0.1"));
  expect(unsupported.hookCount() == 0);
  expect(
      unsupported.notifications().back().text
      == "[whiteboard] compatibility gate: host check failed: raw-tag=0.56.1, branch=, hash=efb50993780079460b0cbed1363e2166a2de1d9f, dirty=false, selected=0.56.1");

  hyprfield::testing::HostHarness failed_renderer;
  failed_renderer.setFunction(
      "IHyprRenderer::renderWorkspaceWindows(PHLMONITOR, PHLWORKSPACE, Time::steady_tp const&)");
  failed_renderer.setFunction("CInputManager::onMouseMoved(IPointer::SMotionEvent)");
  failed_renderer.setHookRegistration(false);
  expect(!failed_renderer.loadPlugin(plugin_path, "0.1"));
  expect(failed_renderer.hookCount() == 0);
  expect(failed_renderer.notifications().front().text
         == "[whiteboard] compatibility gate: failed to register IHyprRenderer::renderWorkspaceWindows");

  hyprfield::testing::HostHarness dirty;
  dirty.setHostVersion("0.56.1", true);
  expect(!dirty.loadPlugin(plugin_path, "0.1"));
  expect(dirty.hookCount() == 0);
  expect(dirty.notifications().back().text
      == "[whiteboard] compatibility gate: host check failed: raw-tag=0.56.1, branch=, hash=efb50993780079460b0cbed1363e2166a2de1d9f, dirty=true, selected=0.56.1");

  hyprfield::testing::HostHarness activation;
  activation.setMonitor("DP-1");
  activation.setMonitor("DP-2");
  activation.setWorkspace(42, "DP-1");
  activation.setFunction("IHyprRenderer::renderWorkspaceWindows(PHLMONITOR, PHLWORKSPACE, Time::steady_tp const&)");
  activation.setFunction("CInputManager::onMouseMoved(IPointer::SMotionEvent)");
  expect(activation.loadPlugin(plugin_path, "0.1"));
  expect(activation.invokeLua("whiteboard", "activate", std::vector<std::string>{"DP-1", "42"}));
  expect(activation.notifications().back().text == "[whiteboard] workspace 42 active on DP-1");
  expect(activation.invokeLua("whiteboard", "active", std::vector<std::string>{"DP-1", "42"}));
  expect(activation.commands().size() == 5);
  expect(activation.commands().at(1).args == "");
  expect(activation.commands().at(2).args == "hl.dsp.workspace.move({workspace=\"42\",monitor=\"DP-1\"})");
  expect(!activation.invokeLua("whiteboard", "activate", std::vector<std::string>{"DP-2", "42"}));
  expect(activation.notifications().back().text
         == "[whiteboard] activation failed: workspace 42 is already assigned to DP-1");
  activation.unload();

  hyprfield::testing::HostHarness missing_monitor;
  missing_monitor.setWorkspace(42, "DP-1");
  missing_monitor.setFunction(
      "IHyprRenderer::renderWorkspaceWindows(PHLMONITOR, PHLWORKSPACE, Time::steady_tp const&)");
  missing_monitor.setFunction("CInputManager::onMouseMoved(IPointer::SMotionEvent)");
  expect(missing_monitor.loadPlugin(plugin_path, "0.1"));
  expect(!missing_monitor.invokeLua("whiteboard", "activate", std::vector<std::string>{"DP-9", "42"}));
  expect(missing_monitor.notifications().back().text.starts_with(
      "[whiteboard] activation failed: monitor DP-9 was not found"));

  hyprfield::testing::HostHarness failed_command;
  failed_command.setMonitor("DP-1");
  failed_command.setWorkspace(42, "DP-1");
  failed_command.setFunction("IHyprRenderer::renderWorkspaceWindows(PHLMONITOR, PHLWORKSPACE, Time::steady_tp const&)");
  failed_command.setFunction("CInputManager::onMouseMoved(IPointer::SMotionEvent)");
  failed_command.setCommandResults(false);
  expect(failed_command.loadPlugin(plugin_path, "0.1"));
  expect(!failed_command.invokeLua("whiteboard", "activate", std::vector<std::string>{"DP-1", "42"}));
  expect(failed_command.notifications().back().text.starts_with(
      "[whiteboard] activation failed: compositor command failed"));

  hyprfield::testing::HostHarness invalid_workspace;
  invalid_workspace.setMonitor("DP-1");
  invalid_workspace.setFunction(
      "IHyprRenderer::renderWorkspaceWindows(PHLMONITOR, PHLWORKSPACE, Time::steady_tp const&)");
  invalid_workspace.setFunction("CInputManager::onMouseMoved(IPointer::SMotionEvent)");
  expect(invalid_workspace.loadPlugin(plugin_path, "0.1"));
  expect(!invalid_workspace.invokeLua("whiteboard", "activate", std::vector<std::string>{"DP-1", "99"}));
  expect(invalid_workspace.notifications().back().text == "[whiteboard] activation failed: workspace 99 was not found");

  hyprfield::testing::HostHarness model;
  model.setMonitor("DP-1");
  model.setWorkspace(42, "DP-1");
  model.setClient("address:one", 42, "DP-1");
  model.setClient("address:two", 42, "DP-1");
  model.setFunction("IHyprRenderer::renderWorkspaceWindows(PHLMONITOR, PHLWORKSPACE, Time::steady_tp const&)");
  model.setFunction("CInputManager::onMouseMoved(IPointer::SMotionEvent)");
  expect(model.loadPlugin(plugin_path, "0.1"));
  expect(model.invokeLua("whiteboard", "activate", std::vector<std::string>{"DP-1", "42"}));
  expect(model.invokeLua("whiteboard", "normal", std::vector<std::string>{"DP-1", "42"}));
  expect(model.invokeLua("whiteboard", "registerClient", std::vector<std::string>{"DP-1", "42", "address:one"}));
  expect(model.invokeLua("whiteboard", "registerClient", std::vector<std::string>{"DP-1", "42", "address:two"}));
  expect(model.invokeLua("whiteboard", "clientActive", "address:one"));
  expect(!model.invokeLua("whiteboard", "registerClient", std::vector<std::string>{"DP-1", "42", "address:one"}));
  expect(model.invokeLua(
      "whiteboard", "configureGrid", std::vector<std::string>{"DP-1", "42", "2", "4", "10", "20", "grid"}));
  expect(model.invokeLua(
      "whiteboard", "placeGrid", std::vector<std::string>{"DP-1", "42", "address:one", "1", "3", "1", "1"}));
  expect(model.invokeLua(
      "whiteboard", "placeGrid", std::vector<std::string>{"DP-1", "42", "address:one", "2", "0", "1", "1"}));
  expect(model.invokeLua("whiteboard", "setLayer", std::vector<std::string>{"DP-1", "42", "address:one", "floating"}));
  expect(model.invokeLua("whiteboard", "setLayer", std::vector<std::string>{"DP-1", "42", "address:one", "grid"}));
  expect(model.invokeLua(
      "whiteboard", "placeFloating", std::vector<std::string>{"DP-1", "42", "address:one", "120", "80", "640", "480"}));
  expect(model.invokeLua("whiteboard", "focusClient", "address:one"));
  expect(model.focusedClient() == "address:one");
  model.closeClient("address:one");
  expect(!model.invokeLua("whiteboard", "clientActive", "address:one"));
  model.setClient("address:one", 42, "DP-1");
  expect(model.invokeLua("whiteboard", "registerClient", std::vector<std::string>{"DP-1", "42", "address:one"}));
  expect(!model.invokeLua(
      "whiteboard", "configureGrid", std::vector<std::string>{"DP-1", "42", "0", "4", "10", "20", "grid"}));
  expect(model.invokeLua("whiteboard", "setZoom", std::vector<std::string>{"DP-1", "42", "1.0"}));
  expect(model.invokeLua("whiteboard", "setZoom", std::vector<std::string>{"DP-1", "42", "0.5"}));
  expect(!model.invokeLua("whiteboard", "setZoom", std::vector<std::string>{"DP-1", "42", "0.1"}));
  expect(model.invokeLua("whiteboard", "setZoom", std::vector<std::string>{"DP-1", "42", "1.0"}));
  expect(model.invokeLua("whiteboard", "normal", std::vector<std::string>{"DP-1", "42"}));
  expect(!model.invokeLua("whiteboard", "management", std::vector<std::string>{"DP-1", "42"}));
  expect(model.invokeLua("whiteboard", "setCamera", std::vector<std::string>{"DP-1", "42", "1.0", "100", "-100"}));
  expect(!model.invokeLua("whiteboard", "management", std::vector<std::string>{"DP-1", "42"}));
  expect(model.invokeLua("whiteboard", "closeClient", "address:one"));
  expect(!model.invokeLua("whiteboard", "clientActive", "address:one"));
  expect(model.invokeLua("whiteboard", "registerClient", std::vector<std::string>{"DP-1", "42", "address:one"}));
  expect(model.invokeLua("whiteboard", "deactivate", std::vector<std::string>{"DP-1", "42"}));
  expect(!model.invokeLua("whiteboard", "active", std::vector<std::string>{"DP-1", "42"}));
  expect(!model.invokeLua("whiteboard", "clientActive", "address:one"));
  model.unload();

  hyprfield::testing::HostHarness lost;
  lost.setMonitor("DP-1");
  lost.setWorkspace(42, "DP-1");
  lost.setFunction("IHyprRenderer::renderWorkspaceWindows(PHLMONITOR, PHLWORKSPACE, Time::steady_tp const&)");
  lost.setFunction("CInputManager::onMouseMoved(IPointer::SMotionEvent)");
  expect(lost.loadPlugin(plugin_path, "0.1"));
  expect(lost.invokeLua("whiteboard", "activate", std::vector<std::string>{"DP-1", "42"}));
  lost.removeMonitor("DP-1");
  expect(lost.invokeLua("whiteboard", "active", std::vector<std::string>{"DP-1", "42"}));
  expect(lost.invokeLua("whiteboard", "deactivate", std::vector<std::string>{"DP-1", "42"}));
  expect(!lost.invokeLua("whiteboard", "active", std::vector<std::string>{"DP-1", "42"}));
  lost.unload();

  hyprfield::testing::HostHarness persisted;
  persisted.setMonitor("DP-1");
  persisted.setWorkspace(42, "DP-1");
  persisted.setClient("address:persisted", 42, "DP-1");
  persisted.setFunction("IHyprRenderer::renderWorkspaceWindows(PHLMONITOR, PHLWORKSPACE, Time::steady_tp const&)");
  persisted.setFunction("CInputManager::onMouseMoved(IPointer::SMotionEvent)");
  expect(persisted.loadPlugin(plugin_path, "0.1"));
  expect(persisted.invokeLua("whiteboard", "activate", std::vector<std::string>{"DP-1", "42"}));
  expect(
      persisted.invokeLua("whiteboard", "registerClient", std::vector<std::string>{"DP-1", "42", "address:persisted"}));
  expect(persisted.invokeLua("whiteboard", "save", std::vector<std::string>{}));
  persisted.unload();

  hyprfield::testing::HostHarness restored;
  restored.setMonitor("DP-1");
  restored.setWorkspace(42, "DP-1");
  restored.setClient("address:persisted", 42, "DP-1");
  restored.setFunction("IHyprRenderer::renderWorkspaceWindows(PHLMONITOR, PHLWORKSPACE, Time::steady_tp const&)");
  restored.setFunction("CInputManager::onMouseMoved(IPointer::SMotionEvent)");
  expect(restored.loadPlugin(plugin_path, "0.1"));
  expect(restored.invokeLua("whiteboard", "active", std::vector<std::string>{"DP-1", "42"}));
  // Client registrations are runtime handles and are intentionally discarded
  // when the plugin unloads; board/camera state remains persisted.
  expect(!restored.invokeLua("whiteboard", "clientActive", "address:persisted"));
  restored.unload();
}

}  // namespace

int main(int argc, char** argv) {
  expect(argc == 3);
  const std::filesystem::path state = "/tmp/hyprfield-whiteboard-test-state";
  std::filesystem::remove_all(state);
  setenv("XDG_STATE_HOME", state.c_str(), 1);
  verify_host_lifecycle(argv[1]);
  verify_whiteboard(argv[2]);
  std::filesystem::remove_all(state);
}
