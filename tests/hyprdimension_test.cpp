#include <cstdlib>
#include <filesystem>
#include <string>

#include "host_harness.hpp"

namespace {

void expect(bool condition) {
  if (!condition) {
    std::abort();
  }
}

void invoke(hyprfield::testing::HostHarness& host, const char* name, const char* argument) {
  expect(host.invokeLua("hyprdimension", name, argument));
}

void verify_whiteboard(const std::string& plugin_path) {
  std::filesystem::remove_all("/tmp/hyprfield-hyprdimension-test");
  hyprfield::testing::HostHarness host;
  expect(host.loadPlugin(plugin_path, "0.1"));
  expect(host.pluginName() == "hyprdimension");
  invoke(host, "assign", "DP-1 9");
  invoke(host, "configure", "DP-1 2 4 12 24");
  invoke(host, "open", "DP-1 terminal");
  invoke(host, "place", "DP-1 terminal 0 0 1 1");
  invoke(host, "open", "DP-1 browser");
  invoke(host, "place", "DP-1 browser 0 1 1 1");
  invoke(host, "place", "DP-1 browser 0 0 1 1");
  invoke(host, "floating", "DP-1 browser");
  invoke(host, "zoom", "DP-1 0.8");
  invoke(host, "focus", "DP-1 browser");
  invoke(host, "camera", "DP-1 100 -40");
  invoke(host, "popup", "DP-1 menu browser");
  expect(host.notifications().size() == 13);
  expect(host.notifications()[1].text == "hyprdimension assigned 9 to DP-1");
  expect(host.notifications()[7].text == "hyprdimension window placed browser");
  expect(host.notifications()[9].text == "hyprdimension DP-1 management");
  expect(host.notifications().back().text == "hyprdimension popup attached menu");
  host.unload();
  expect(!host.loaded());

  hyprfield::testing::HostHarness restored;
  expect(restored.loadPlugin(plugin_path, "0.1"));
  invoke(restored, "focus", "DP-1 browser");
  expect(restored.notifications().back().text == "hyprdimension focused browser");
  restored.unload();
}

}  // namespace

int main(int argc, char** argv) {
  expect(argc == 2);
  verify_whiteboard(argv[1]);
}
