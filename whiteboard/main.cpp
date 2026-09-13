#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
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
std::vector<CHyprSignalListener> lifecycleListeners;

struct Board {
  std::string monitor;
  int workspace;
  float zoom = 1.0F;
  struct Placement {
    std::string layer = "grid";
    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };
  struct Grid {
    int rows = 2;
    int columns = 4;
    int gap = 16;
    int margin = 32;
    std::string openingLayer = "grid";
  };
  Grid grid;
  std::unordered_map<std::string, Placement> clients;
};

std::unordered_map<int, Board> activeBoards;

std::filesystem::path statePath() {
  const char* stateHome = std::getenv("XDG_STATE_HOME");
  const auto root = stateHome == nullptr || std::string_view{stateHome}.empty()
                        ? std::filesystem::path{std::getenv("HOME") == nullptr ? "." : std::getenv("HOME")}
                        : std::filesystem::path{stateHome};
  return root / "hyprfield" / "whiteboard.state";
}

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

std::optional<std::reference_wrapper<Board>> findBoard(std::string_view monitor, int workspace) {
  const auto found = activeBoards.find(workspace);
  if (found == activeBoards.end() || found->second.monitor != monitor)
    return std::nullopt;
  return found->second;
}

struct MonitorGeometry {
  int x = 0;
  int y = 0;
  int width = 1920;
  int height = 1080;
};

int jsonNumber(const std::string& json, std::string_view field, int fallback) {
  const auto position = json.find("\"" + std::string{field} + "\":");
  if (position == std::string::npos)
    return fallback;
  try {
    return std::stoi(json.substr(position + field.size() + 3));
  } catch (const std::exception&) {
    return fallback;
  }
}

MonitorGeometry monitorGeometry(std::string_view monitor) {
  const auto json = HyprlandAPI::invokeHyprctlCommand("monitors", "-j");
  const auto name = json.find("\"name\":\"" + std::string{monitor} + "\"");
  if (name == std::string::npos)
    return {};
  const auto object = json.substr(name, json.find('}', name) - name);
  return {.x = jsonNumber(object, "x", 0),
          .y = jsonNumber(object, "y", 0),
          .width = jsonNumber(object, "width", 1920),
          .height = jsonNumber(object, "height", 1080)};
}

bool occupies(const Board::Placement& placement, int row, int column) {
  return row >= placement.row && row < placement.row + placement.rowSpan && column >= placement.column
         && column < placement.column + placement.columnSpan;
}

bool freeSlots(const Board& board, std::string_view except, int row, int column, int rowSpan, int columnSpan) {
  for (const auto& [identity, placement] : board.clients) {
    if (identity == except || placement.layer != "grid")
      continue;
    for (int claimedRow = row; claimedRow < row + rowSpan; ++claimedRow)
      for (int claimedColumn = column; claimedColumn < column + columnSpan; ++claimedColumn)
        if (occupies(placement, claimedRow, claimedColumn))
          return false;
  }
  return true;
}

void setGeometry(Board& board, Board::Placement& placement, std::string_view monitor) {
  const auto geometry = monitorGeometry(monitor);
  const auto usableWidth = geometry.width - board.grid.margin * 2 - board.grid.gap * (board.grid.columns - 1);
  const auto usableHeight = geometry.height - board.grid.margin * 2 - board.grid.gap * (board.grid.rows - 1);
  const auto slotWidth = usableWidth / board.grid.columns;
  const auto slotHeight = usableHeight / board.grid.rows;
  placement.x = geometry.x + board.grid.margin + placement.column * (slotWidth + board.grid.gap);
  placement.y = geometry.y + board.grid.margin + placement.row * (slotHeight + board.grid.gap);
  placement.width = slotWidth * placement.columnSpan + board.grid.gap * (placement.columnSpan - 1);
  placement.height = slotHeight * placement.rowSpan + board.grid.gap * (placement.rowSpan - 1);
}

bool dispatchGeometry(std::string_view identity, const Board::Placement& placement) {
  const auto address = identity.starts_with("address:") ? std::string{identity} : "address:" + std::string{identity};
  const auto clients = HyprlandAPI::invokeHyprctlCommand("clients", "-j");
  const auto client = clients.find("\"address\":\"" + std::string{identity} + "\"");
  const auto objectEnd = client == std::string::npos ? std::string::npos : clients.find("\"address\":\"", client + 1);
  const auto object =
      client == std::string::npos
          ? std::string{}
          : clients.substr(client, objectEnd == std::string::npos ? std::string::npos : objectEnd - client);
  if (!object.contains("\"floating\":true")) {
    const auto floating = HyprlandAPI::invokeHyprctlCommand("dispatch", "togglefloating " + address);
    if (!floating.starts_with("ok"))
      return false;
  }
  const auto move = HyprlandAPI::invokeHyprctlCommand(
      "dispatch",
      "movewindowpixel exact " + std::to_string(placement.x) + " " + std::to_string(placement.y) + "," + address);
  if (!move.starts_with("ok"))
    return false;
  const auto resize = HyprlandAPI::invokeHyprctlCommand("dispatch",
                                                        "resizewindowpixel exact " + std::to_string(placement.width)
                                                            + " " + std::to_string(placement.height) + "," + address);
  return resize.starts_with("ok");
}

int placementFailure(lua_State* state, std::string_view reason) {
  return activationFailure(state, reason);
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

void pruneLostBoards() {
  const auto monitors = HyprlandAPI::invokeHyprctlCommand("monitors", "-j");
  const auto workspaces = HyprlandAPI::invokeHyprctlCommand("workspaces", "-j");
  for (auto iterator = activeBoards.begin(); iterator != activeBoards.end();) {
    if (jsonContainsMonitor(monitors, iterator->second.monitor)
        && jsonContainsWorkspace(workspaces, iterator->second.workspace, iterator->second.monitor))
      ++iterator;
    else
      iterator = activeBoards.erase(iterator);
  }
}

bool jsonContainsClient(const std::string& json, std::string_view identity) {
  return json.contains("\"address\":\"" + std::string{identity} + "\"");
}

void pruneStaleClients() {
  const auto clients = HyprlandAPI::invokeHyprctlCommand("clients", "-j");
  for (auto& [_, board] : activeBoards)
    for (auto iterator = board.clients.begin(); iterator != board.clients.end();) {
      if (jsonContainsClient(clients, iterator->first))
        ++iterator;
      else
        iterator = board.clients.erase(iterator);
    }
}

void removeClosedClient(PHLWINDOW window) {
  if (!window) {
    pruneStaleClients();
    return;
  }
  const auto identity = std::format("0x{:x}", reinterpret_cast<uintptr_t>(window.get()));
  for (auto& [_, board] : activeBoards)
    board.clients.erase(identity);
}

void installLifecycleListeners() {
  lifecycleListeners.push_back(Event::bus()->m_events.window.close.listen(removeClosedClient));
  lifecycleListeners.push_back(Event::bus()->m_events.monitor.removed.listen([](PHLMONITOR) { pruneLostBoards(); }));
  lifecycleListeners.push_back(
      Event::bus()->m_events.workspace.removed.listen([](PHLWORKSPACEREF) { pruneLostBoards(); }));
}

bool jsonClientBelongsToBoard(const std::string& json,
                              std::string_view identity,
                              std::string_view monitor,
                              int workspace) {
  const auto address = json.find("\"address\":\"" + std::string{identity} + "\"");
  if (address == std::string::npos)
    return false;
  const auto objectStart = json.rfind('{', address);
  const auto objectEnd = json.find('}', address);
  if (objectStart == std::string::npos || objectEnd == std::string::npos)
    return false;
  const auto object = json.substr(objectStart, objectEnd - objectStart);
  return object.contains("\"monitor\":\"" + std::string{monitor} + "\"")
         && object.contains("\"workspace\":{\"id\":" + std::to_string(workspace));
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
  if (existing != activeBoards.end() && existing->second.monitor != monitor) {
    return activationFailure(
        state, "workspace " + std::to_string(workspace) + " is already assigned to " + existing->second.monitor);
  }

  if (!jsonContainsMonitor(HyprlandAPI::invokeHyprctlCommand("monitors", "-j"), monitor)) {
    return activationFailure(state, "monitor " + monitor + " was not found");
  }
  const auto workspaces = HyprlandAPI::invokeHyprctlCommand("workspaces", "-j");
  if (!jsonContainsWorkspace(workspaces, static_cast<int>(workspace))) {
    return activationFailure(state, "workspace " + std::to_string(workspace) + " was not found");
  }
  if (!jsonContainsWorkspace(workspaces, static_cast<int>(workspace), monitor)
      && !jsonContainsWorkspace(workspaces, static_cast<int>(workspace), "")) {
    return activationFailure(state, "workspace " + std::to_string(workspace) + " is assigned to another monitor");
  }
  const auto dispatch = HyprlandAPI::invokeHyprctlCommand(
      "dispatch", "moveworkspacetomonitor " + std::to_string(workspace) + " " + monitor);
  if (!dispatch.starts_with("ok")) {
    return activationFailure(state, "compositor command failed");
  }
  if (!jsonContainsWorkspace(HyprlandAPI::invokeHyprctlCommand("workspaces", "-j"), workspace, monitor)) {
    return activationFailure(state, "workspace verification failed");
  }
  activeBoards.try_emplace(static_cast<int>(workspace),
                           Board{.monitor = monitor, .workspace = static_cast<int>(workspace)});
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
  const auto active =
      monitorValue != nullptr && monitorLength > 0 && isWorkspace && workspace > 0
      && activeBoards.contains(static_cast<int>(workspace))
      && activeBoards.at(static_cast<int>(workspace)).monitor == std::string{monitorValue, monitorLength};
  lua_pushboolean(state, active);
  return 1;
}

int registerClientLua(lua_State* state) {
  pruneLostBoards();
  pruneStaleClients();
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int isWorkspace = 0;
  const auto workspace = lua_tointegerx(state, 2, &isWorkspace);
  size_t identityLength = 0;
  const auto* identityValue = lua_tolstring(state, 3, &identityLength);
  if (monitorValue == nullptr || monitorLength == 0 || !isWorkspace || workspace <= 0 || identityValue == nullptr
      || identityLength == 0)
    return activationFailure(state, "monitor, positive workspace, and client identity are required");

  const std::string monitor{monitorValue, monitorLength};
  const std::string identity{identityValue, identityLength};
  const auto clients = HyprlandAPI::invokeHyprctlCommand("clients", "-j");
  if (!jsonContainsClient(clients, identity))
    return activationFailure(state, "client identity was not found");
  const auto board = findBoard(monitor, static_cast<int>(workspace));
  if (!board)
    return activationFailure(state, "client belongs to an inactive board");
  const auto alreadyRegistered = std::ranges::any_of(
      activeBoards, [&identity](const auto& entry) { return entry.second.clients.contains(identity); });
  if (alreadyRegistered)
    return activationFailure(state, "client identity is already registered");
  const auto clientOnBoard = jsonClientBelongsToBoard(clients, identity, monitor, static_cast<int>(workspace));
  if (!clientOnBoard) {
    const auto dispatch = HyprlandAPI::invokeHyprctlCommand(
        "dispatch", "movetoworkspacesilent " + std::to_string(workspace) + "," + identity);
    if (!dispatch.starts_with("ok"))
      return activationFailure(state, "failed to move client to board");
    if (!jsonClientBelongsToBoard(
            HyprlandAPI::invokeHyprctlCommand("clients", "-j"), identity, monitor, static_cast<int>(workspace)))
      return activationFailure(state, "client workspace verification failed");
  }
  auto& record = board->get().clients[identity];
  record.layer = board->get().grid.openingLayer;
  if (record.layer == "grid") {
    bool placed = false;
    for (int row = 0; row < board->get().grid.rows && !placed; ++row)
      for (int column = 0; column < board->get().grid.columns && !placed; ++column)
        if (freeSlots(board->get(), identity, row, column, 1, 1)) {
          record.row = row;
          record.column = column;
          setGeometry(board->get(), record, monitor);
          if (!dispatchGeometry(identity, record)) {
            board->get().clients.erase(identity);
            return activationFailure(state, "client geometry dispatch failed");
          }
          placed = true;
        }
    if (!placed) {
      board->get().clients.erase(identity);
      return activationFailure(state, "no free grid slot");
    }
  }
  lua_pushboolean(state, true);
  return 1;
}

int clientActiveLua(lua_State* state) {
  pruneLostBoards();
  pruneStaleClients();
  size_t identityLength = 0;
  const auto* identityValue = lua_tolstring(state, 1, &identityLength);
  bool active = identityValue != nullptr && identityLength != 0;
  const std::string identity{identityValue == nullptr ? "" : identityValue, identityLength};
  if (active) {
    active = std::ranges::any_of(activeBoards,
                                 [&identity](const auto& entry) { return entry.second.clients.contains(identity); });
  }
  lua_pushboolean(state, active);
  return 1;
}

int closeClientLua(lua_State* state) {
  size_t identityLength = 0;
  const auto* identityValue = lua_tolstring(state, 1, &identityLength);
  if (identityValue == nullptr || identityLength == 0)
    return activationFailure(state, "client identity is required");
  const std::string identity{identityValue, identityLength};
  for (auto& [_, board] : activeBoards) {
    if (board.clients.erase(identity) != 0) {
      lua_pushboolean(state, true);
      return 1;
    }
  }
  return activationFailure(state, "client identity was not registered");
}

int configureGridLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int workspaceOk = 0;
  const auto workspace = lua_tointegerx(state, 2, &workspaceOk);
  int rowsOk = 0;
  const auto rows = lua_tointegerx(state, 3, &rowsOk);
  int columnsOk = 0;
  const auto columns = lua_tointegerx(state, 4, &columnsOk);
  int gapOk = 0;
  const auto gap = lua_tointegerx(state, 5, &gapOk);
  int marginOk = 0;
  const auto margin = lua_tointegerx(state, 6, &marginOk);
  const auto layer = luaL_optstring(state, 7, "grid");
  if (monitorValue == nullptr || monitorLength == 0 || !workspaceOk || workspace <= 0 || !rowsOk || rows <= 0
      || !columnsOk || columns <= 0 || !gapOk || gap < 0 || !marginOk || margin < 0
      || (std::string_view{layer} != "grid" && std::string_view{layer} != "floating"))
    return placementFailure(state, "invalid grid configuration");
  const auto board = findBoard(std::string_view{monitorValue, monitorLength}, static_cast<int>(workspace));
  if (!board)
    return placementFailure(state, "board is not active");
  if (margin * 2 + gap * (columns - 1) >= monitorGeometry(monitorValue).width
      || margin * 2 + gap * (rows - 1) >= monitorGeometry(monitorValue).height)
    return placementFailure(state, "grid configuration does not fit monitor");
  for (const auto& [_, placement] : board->get().clients)
    if (placement.layer == "grid"
        && (placement.row + placement.rowSpan > rows || placement.column + placement.columnSpan > columns))
      return placementFailure(state, "grid configuration would invalidate placement");
  auto& grid = board->get().grid;
  const auto previousGrid = grid;
  const auto previousPlacements = board->get().clients;
  grid = {.rows = static_cast<int>(rows),
          .columns = static_cast<int>(columns),
          .gap = static_cast<int>(gap),
          .margin = static_cast<int>(margin),
          .openingLayer = layer};
  for (auto& [identity, placement] : board->get().clients)
    if (placement.layer == "grid") {
      setGeometry(board->get(), placement, std::string_view{monitorValue, monitorLength});
      if (!dispatchGeometry(identity, placement)) {
        grid = previousGrid;
        board->get().clients = previousPlacements;
        return placementFailure(state, "client geometry dispatch failed");
      }
    }
  lua_pushboolean(state, true);
  return 1;
}

int placeGridLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int workspaceOk = 0;
  const auto workspace = lua_tointegerx(state, 2, &workspaceOk);
  size_t identityLength = 0;
  const auto* identityValue = lua_tolstring(state, 3, &identityLength);
  int rowOk = 0;
  const auto row = lua_tointegerx(state, 4, &rowOk);
  int columnOk = 0;
  const auto column = lua_tointegerx(state, 5, &columnOk);
  int rowSpanOk = 0;
  const auto rowSpan = lua_tointegerx(state, 6, &rowSpanOk);
  int columnSpanOk = 0;
  const auto columnSpan = lua_tointegerx(state, 7, &columnSpanOk);
  if (monitorValue == nullptr || monitorLength == 0 || !workspaceOk || workspace <= 0 || identityValue == nullptr
      || identityLength == 0 || !rowOk || !columnOk || !rowSpanOk || !columnSpanOk || row < 0 || column < 0
      || rowSpan <= 0 || columnSpan <= 0)
    return placementFailure(state, "invalid grid placement");
  const std::string monitor{monitorValue, monitorLength};
  const std::string identity{identityValue, identityLength};
  const auto board = findBoard(monitor, static_cast<int>(workspace));
  if (!board)
    return placementFailure(state, "board is not active");
  auto found = board->get().clients.find(identity);
  if (found == board->get().clients.end())
    return placementFailure(state, "client identity was not registered");
  if (row + rowSpan > board->get().grid.rows || column + columnSpan > board->get().grid.columns)
    return placementFailure(state, "grid placement is out of bounds");
  auto& target = found->second;
  if (rowSpan == 1 && columnSpan == 1) {
    for (auto& [otherIdentity, other] : board->get().clients) {
      if (otherIdentity == identity || other.layer != "grid"
          || !occupies(other, static_cast<int>(row), static_cast<int>(column)))
        continue;
      if (other.rowSpan != 1 || other.columnSpan != 1)
        return placementFailure(state, "grid slot is occupied by a span");
      const auto previousTarget = target;
      const auto previousOther = other;
      std::swap(target.row, other.row);
      std::swap(target.column, other.column);
      target.rowSpan = 1;
      target.columnSpan = 1;
      target.layer = "grid";
      setGeometry(board->get(), target, monitor);
      setGeometry(board->get(), other, monitor);
      if (!dispatchGeometry(identity, target) || !dispatchGeometry(otherIdentity, other)) {
        target = previousTarget;
        other = previousOther;
        dispatchGeometry(identity, previousTarget);
        dispatchGeometry(otherIdentity, previousOther);
        return placementFailure(state, "client geometry dispatch failed");
      }
      lua_pushboolean(state, true);
      return 1;
    }
  } else if (!freeSlots(board->get(),
                        identity,
                        static_cast<int>(row),
                        static_cast<int>(column),
                        static_cast<int>(rowSpan),
                        static_cast<int>(columnSpan))) {
    return placementFailure(state, "grid placement is occupied");
  }
  const auto previousTarget = target;
  target.layer = "grid";
  target.row = static_cast<int>(row);
  target.column = static_cast<int>(column);
  target.rowSpan = static_cast<int>(rowSpan);
  target.columnSpan = static_cast<int>(columnSpan);
  setGeometry(board->get(), target, monitor);
  if (!dispatchGeometry(identity, target)) {
    target = previousTarget;
    return placementFailure(state, "client geometry dispatch failed");
  }
  lua_pushboolean(state, true);
  return 1;
}

int setLayerLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int workspaceOk = 0;
  const auto workspace = lua_tointegerx(state, 2, &workspaceOk);
  size_t identityLength = 0;
  const auto* identityValue = lua_tolstring(state, 3, &identityLength);
  const auto layer = luaL_optstring(state, 4, "floating");
  if (monitorValue == nullptr || monitorLength == 0 || !workspaceOk || workspace <= 0 || identityValue == nullptr
      || identityLength == 0 || (std::string_view{layer} != "grid" && std::string_view{layer} != "floating"))
    return placementFailure(state, "invalid layer");
  const auto board = findBoard(std::string_view{monitorValue, monitorLength}, static_cast<int>(workspace));
  if (!board)
    return placementFailure(state, "board is not active");
  const std::string identity{identityValue, identityLength};
  auto found = board->get().clients.find(identity);
  if (found == board->get().clients.end())
    return placementFailure(state, "client identity was not registered");
  const auto previousPlacement = found->second;
  if (std::string_view{layer} == "grid") {
    if (found->second.layer != "grid"
        && !freeSlots(board->get(),
                      identity,
                      found->second.row,
                      found->second.column,
                      found->second.rowSpan,
                      found->second.columnSpan))
      return placementFailure(state, "grid placement is occupied");
    if (found->second.row + found->second.rowSpan > board->get().grid.rows
        || found->second.column + found->second.columnSpan > board->get().grid.columns)
      return placementFailure(state, "grid placement is out of bounds");
    found->second.layer = "grid";
    setGeometry(board->get(), found->second, std::string_view{monitorValue, monitorLength});
    if (!dispatchGeometry(identity, found->second)) {
      found->second = previousPlacement;
      return placementFailure(state, "client geometry dispatch failed");
    }
  } else {
    found->second.layer = "floating";
  }
  lua_pushboolean(state, true);
  return 1;
}

int placeFloatingLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int workspaceOk = 0;
  const auto workspace = lua_tointegerx(state, 2, &workspaceOk);
  size_t identityLength = 0;
  const auto* identityValue = lua_tolstring(state, 3, &identityLength);
  int valuesOk[4] = {};
  const auto x = lua_tointegerx(state, 4, &valuesOk[0]);
  const auto y = lua_tointegerx(state, 5, &valuesOk[1]);
  const auto width = lua_tointegerx(state, 6, &valuesOk[2]);
  const auto height = lua_tointegerx(state, 7, &valuesOk[3]);
  if (monitorValue == nullptr || monitorLength == 0 || !workspaceOk || workspace <= 0 || identityValue == nullptr
      || identityLength == 0 || !valuesOk[0] || !valuesOk[1] || !valuesOk[2] || !valuesOk[3] || width <= 0
      || height <= 0)
    return placementFailure(state, "invalid floating placement");
  const auto board = findBoard(std::string_view{monitorValue, monitorLength}, static_cast<int>(workspace));
  if (!board)
    return placementFailure(state, "board is not active");
  const std::string identity{identityValue, identityLength};
  auto found = board->get().clients.find(identity);
  if (found == board->get().clients.end())
    return placementFailure(state, "client identity was not registered");
  const auto previousPlacement = found->second;
  found->second = {.layer = "floating",
                   .x = static_cast<int>(x),
                   .y = static_cast<int>(y),
                   .width = static_cast<int>(width),
                   .height = static_cast<int>(height)};
  if (!dispatchGeometry(identity, found->second)) {
    found->second = previousPlacement;
    return placementFailure(state, "client geometry dispatch failed");
  }
  lua_pushboolean(state, true);
  return 1;
}

int focusClientLua(lua_State* state) {
  size_t identityLength = 0;
  const auto* identityValue = lua_tolstring(state, 1, &identityLength);
  if (identityValue == nullptr || identityLength == 0)
    return placementFailure(state, "client identity is required");
  const std::string identity{identityValue, identityLength};
  const auto registered = std::ranges::any_of(
      activeBoards, [&identity](const auto& entry) { return entry.second.clients.contains(identity); });
  if (!registered)
    return placementFailure(state, "client identity was not registered");
  const auto address = identity.starts_with("address:") ? identity : "address:" + identity;
  const auto dispatch = HyprlandAPI::invokeHyprctlCommand("dispatch", "focuswindow " + std::string{address});
  if (!dispatch.starts_with("ok"))
    return placementFailure(state, "failed to focus client");
  lua_pushboolean(state, true);
  return 1;
}

int setZoomLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int isWorkspace = 0;
  const auto workspace = lua_tointegerx(state, 2, &isWorkspace);
  int isZoom = 0;
  const auto zoom = static_cast<float>(lua_tonumberx(state, 3, &isZoom));
  if (monitorValue == nullptr || monitorLength == 0 || !isWorkspace || workspace <= 0 || !isZoom || zoom <= 0.0F)
    return activationFailure(state, "monitor, positive workspace, and positive zoom are required");
  const auto board = findBoard(std::string_view{monitorValue, monitorLength}, static_cast<int>(workspace));
  if (!board)
    return activationFailure(state, "board is not active");
  board->get().zoom = std::clamp(zoom, 0.5F, 1.0F);
  lua_pushboolean(state, true);
  return 1;
}

int normalLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int isWorkspace = 0;
  const auto workspace = lua_tointegerx(state, 2, &isWorkspace);
  const auto board = monitorValue == nullptr || !isWorkspace
                         ? std::optional<std::reference_wrapper<Board>>{}
                         : findBoard(std::string_view{monitorValue, monitorLength}, static_cast<int>(workspace));
  lua_pushboolean(state, board && board->get().zoom == 1.0F);
  return 1;
}

int managementLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int isWorkspace = 0;
  const auto workspace = lua_tointegerx(state, 2, &isWorkspace);
  const auto board = monitorValue == nullptr || !isWorkspace
                         ? std::optional<std::reference_wrapper<Board>>{}
                         : findBoard(std::string_view{monitorValue, monitorLength}, static_cast<int>(workspace));
  lua_pushboolean(state, board && board->get().zoom < 0.9F);
  return 1;
}

void persist() {
  std::error_code error;
  std::filesystem::create_directories(statePath().parent_path(), error);
  std::ofstream state{statePath()};
  if (!state)
    return;
  for (const auto& [_, board] : activeBoards) {
    state << board.monitor << '\t' << board.workspace << '\t' << board.zoom << '\n';
    state << "grid\t" << board.workspace << '\t' << board.grid.rows << '\t' << board.grid.columns << '\t'
          << board.grid.gap << '\t' << board.grid.margin << '\t' << board.grid.openingLayer << '\n';
    for (const auto& [identity, placement] : board.clients)
      state << "client\t" << board.workspace << '\t' << identity << '\t' << placement.layer << '\t' << placement.row
            << '\t' << placement.column << '\t' << placement.rowSpan << '\t' << placement.columnSpan << '\n';
  }
}

void restore() {
  std::ifstream state{statePath()};
  if (!state)
    return;
  std::string line;
  while (std::getline(state, line)) {
    const auto first = line.find('\t');
    const auto second = line.find('\t', first + 1);
    if (first == std::string::npos || second == std::string::npos)
      continue;
    if (line.starts_with("client\t")) {
      try {
        const auto workspace = std::stoi(line.substr(first + 1, second - first - 1));
        const auto board = activeBoards.find(workspace);
        const auto fields = line.substr(second + 1);
        const auto fieldEnd = fields.find('\t');
        const auto identity = fields.substr(0, fieldEnd);
        if (board != activeBoards.end() && !identity.empty()
            && jsonClientBelongsToBoard(
                HyprlandAPI::invokeHyprctlCommand("clients", "-j"), identity, board->second.monitor, workspace)) {
          Board::Placement placement;
          if (fieldEnd != std::string::npos) {
            const auto values = fields.substr(fieldEnd + 1);
            const auto next = [&values](size_t from) { return values.find('\t', from); };
            const auto layerEnd = next(0);
            placement.layer = values.substr(0, layerEnd);
            size_t cursor = layerEnd == std::string::npos ? values.size() : layerEnd + 1;
            const auto read = [&values, &cursor, &next]() {
              const auto end = next(cursor);
              const auto value =
                  std::stoi(values.substr(cursor, end == std::string::npos ? values.size() - cursor : end - cursor));
              cursor = end == std::string::npos ? values.size() : end + 1;
              return value;
            };
            if (layerEnd != std::string::npos)
              placement.row = read(), placement.column = read(), placement.rowSpan = read(),
              placement.columnSpan = read();
          }
          board->second.clients.emplace(identity, placement);
          if (placement.layer == "grid")
            setGeometry(board->second, board->second.clients.at(identity), board->second.monitor);
        }
      } catch (const std::exception&) {
        report("ignored invalid client persistence record");
      }
      continue;
    }
    if (line.starts_with("grid\t")) {
      try {
        const auto workspace = std::stoi(line.substr(first + 1, second - first - 1));
        const auto board = activeBoards.find(workspace);
        if (board != activeBoards.end()) {
          const auto values = line.substr(second + 1);
          std::vector<std::string> fields;
          size_t cursor = 0;
          while (cursor <= values.size()) {
            const auto end = values.find('\t', cursor);
            fields.push_back(values.substr(cursor, end == std::string::npos ? values.size() - cursor : end - cursor));
            if (end == std::string::npos)
              break;
            cursor = end + 1;
          }
          if (fields.size() == 5 && std::stoi(fields[0]) > 0 && std::stoi(fields[1]) > 0 && std::stoi(fields[2]) >= 0
              && std::stoi(fields[3]) >= 0 && (fields[4] == "grid" || fields[4] == "floating"))
            board->second.grid = {.rows = std::stoi(fields[0]),
                                  .columns = std::stoi(fields[1]),
                                  .gap = std::stoi(fields[2]),
                                  .margin = std::stoi(fields[3]),
                                  .openingLayer = fields[4]};
        }
      } catch (const std::exception&) {
        report("ignored invalid grid persistence record");
      }
      continue;
    }
    try {
      const auto workspace = std::stoi(line.substr(first + 1, second - first - 1));
      const auto zoom = std::stof(line.substr(second + 1));
      const auto monitor = line.substr(0, first);
      if (jsonContainsMonitor(HyprlandAPI::invokeHyprctlCommand("monitors", "-j"), monitor)
          && jsonContainsWorkspace(HyprlandAPI::invokeHyprctlCommand("workspaces", "-j"), workspace, monitor))
        activeBoards.emplace(workspace,
                             Board{.monitor = monitor, .workspace = workspace, .zoom = std::clamp(zoom, 0.5F, 1.0F)});
    } catch (const std::exception&) {
      report("ignored invalid board persistence record");
    }
  }
}

int saveLua(lua_State* state) {
  static_cast<void>(state);
  persist();
  lua_pushboolean(state, true);
  return 1;
}

int deactivateLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int isWorkspace = 0;
  const auto workspace = lua_tointegerx(state, 2, &isWorkspace);
  if (monitorValue == nullptr || monitorLength == 0 || !isWorkspace || workspace <= 0)
    return activationFailure(state, "monitor and positive workspace are required");
  const auto board = findBoard(std::string_view{monitorValue, monitorLength}, static_cast<int>(workspace));
  if (!board)
    return activationFailure(state, "board is not active");
  activeBoards.erase(static_cast<int>(workspace));
  lua_pushboolean(state, true);
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
  if (!HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "registerClient", registerClientLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "clientActive", clientActiveLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "closeClient", closeClientLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "configureGrid", configureGridLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "placeGrid", placeGridLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "setLayer", setLayerLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "placeFloating", placeFloatingLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "focusClient", focusClientLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "deactivate", deactivateLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "setZoom", setZoomLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "normal", normalLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "management", managementLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "save", saveLua)) {
    report("failed to register workspace model API");
    PLUGIN_EXIT();
    return {};
  }
  restore();
  installLifecycleListeners();
  HyprlandAPI::addNotification(
      pluginHandle, "[whiteboard] compatibility proof ready", CHyprColor{0.2F, 1.0F, 0.4F, 1.0F}, 5000.0F);
  return {.name = "whiteboard",
          .description = "Monitor-assigned Whiteboard workspace model",
          .author = "zurat",
          .version = "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
  for (auto* hook : hooks)
    HyprlandAPI::removeFunctionHook(pluginHandle, hook);
  hooks.clear();
  lifecycleListeners.clear();
  activeBoards.clear();
  pluginHandle = nullptr;
}
