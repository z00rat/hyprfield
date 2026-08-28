# Hyprfield

Hyprfield is a collection of independent plugins for the Hyprland compositor. Each plugin extends a running Hyprland host and can be loaded or managed separately.

## Language

**Hyprfield**:
The repository context containing multiple independent Hyprland plugins.
_Avoid_: Monolithic plugin, Hyprland itself

**Plugin**:
A separately loadable extension that adds behavior to a running Hyprland compositor through the Hyprland plugin API.
_Avoid_: Module, feature, patch

**Plugin collection**:
The set of independent plugins maintained in Hyprfield; plugins share repository tooling but are loaded and used individually.
_Avoid_: Super plugin, plugin bundle

**Host**:
The running Hyprland compositor process that loads, initializes, and unloads plugins.
_Avoid_: Runtime, platform, operating system

**Plugin lifecycle**:
The stages of a plugin being loaded, initialized, used by the host, and unloaded.
_Avoid_: Build lifecycle, application lifecycle

**Native Lua configuration**:
Hyprland's Lua-based configuration interface, which is the supported configuration interface for Hyprfield plugins.
_Avoid_: Lua mode, Lua config file, legacy configuration

**Lua-facing API**:
The plugin functionality exposed for use from native Lua configuration, such as a plugin function invoked by a keybind.
_Avoid_: Lua binding, command alias, dispatcher

**Example plugin**:
A deliberately minimal plugin used to verify that the repository's plugin loading and Lua integration work.
_Avoid_: Production plugin, reference implementation
