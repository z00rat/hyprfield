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

}  // namespace

int main(int argc, char** argv) {
  expect(argc == 2);
  verify_host_lifecycle(argv[1]);
}
