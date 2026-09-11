#include "host_harness.hpp"

#include <cstdlib>
#include <string>

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
  host.setFunction("IElementRenderer::drawSurface(WP<CSurfacePassElement>, CRegion const&)");
  host.setFunction("CInputManager::processMouseMove(Vector2D const&)");
  expect(host.loadPlugin(plugin_path, "0.1"));
  expect(host.hookCount() == 2);
  expect(host.notifications().back().text == "[whiteboard] compatibility proof ready");
  expect(host.invokeLua("whiteboard", "proof", "DP-1"));
  expect(host.notifications().back().text == "[whiteboard proof] monitor-scoped temporary artifact on DP-1");
  host.unload();
  expect(host.hookCount() == 0);
  expect(!host.loaded());

  hyprfield::testing::HostHarness unsupported;
  unsupported.setHostVersion("0.56.1");
  expect(!unsupported.loadPlugin(plugin_path, "0.1"));
  expect(unsupported.hookCount() == 0);
  expect(unsupported.notifications().back().text
         == "[whiteboard] compatibility gate: unsupported or unverified host version");

  hyprfield::testing::HostHarness failed;
  failed.setFunction("IElementRenderer::drawSurface(WP<CSurfacePassElement>, CRegion const&)");
  failed.setFunction("CInputManager::processMouseMove(Vector2D const&)");
  failed.setHookRegistration(false);
  expect(!failed.loadPlugin(plugin_path, "0.1"));
  expect(failed.hookCount() == 0);
  expect(failed.notifications().at(failed.notifications().size() - 2).text
         == "[whiteboard] compatibility gate: failed to register IElementRenderer::drawSurface");

  hyprfield::testing::HostHarness missing;
  expect(!missing.loadPlugin(plugin_path, "0.1"));
  expect(missing.hookCount() == 0);
  expect(missing.notifications().at(missing.notifications().size() - 2).text
         == "[whiteboard] compatibility gate: missing IElementRenderer::drawSurface");

  hyprfield::testing::HostHarness dirty;
  dirty.setHostVersion("0.56.2", true);
  expect(!dirty.loadPlugin(plugin_path, "0.1"));
  expect(dirty.hookCount() == 0);
}

}  // namespace

int main(int argc, char** argv) {
  expect(argc == 3);
  verify_host_lifecycle(argv[1]);
  verify_whiteboard(argv[2]);
}
