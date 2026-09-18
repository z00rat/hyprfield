#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/SurfacePassElement.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace Render {
class IElementRenderer;
}

namespace {

struct MonitorGeometry {
  int x = 0;
  int y = 0;
  int width = 1920;
  int height = 1080;
};

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
  Vector2D pan;
  MonitorGeometry geometry;
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
CFunctionHook* rendererHook = nullptr;
CFunctionHook* inputHook = nullptr;
void drawSurfaceHook(Render::IElementRenderer*, WP<CSurfacePassElement>, const CRegion&);
void mouseMovedHook(CInputManager*, IPointer::SMotionEvent);
void persist();
bool jsonClientBelongsToBoard(const std::string&, std::string_view, std::string_view, int);

std::string invokeDispatcher(std::string_view expression);

std::filesystem::path statePath() {
  const char* stateHome = std::getenv("XDG_STATE_HOME");
  const auto root = stateHome == nullptr || std::string_view{stateHome}.empty()
                        ? std::filesystem::path{std::getenv("HOME") == nullptr ? "." : std::getenv("HOME")}
                        : std::filesystem::path{stateHome};
  return root / "hyprfield" / "whiteboard.state";
}

void debugLog(std::string_view message) {
  std::error_code error;
  const auto path = std::filesystem::path{"/tmp/hyprfield-whiteboard-debug.log"};
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream log{path, std::ios::app};
  if (log)
    log << "[DEBUG-WB] " << message << '\n';
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

  const auto destination = methodName == kRenderMethod ? reinterpret_cast<const void*>(&drawSurfaceHook)
                                                       : reinterpret_cast<const void*>(&mouseMovedHook);
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
  debugLog("failure " + message);
  HyprlandAPI::addNotification(pluginHandle, message, CHyprColor{1.0F, 0.35F, 0.2F, 1.0F}, 5000.0F);
  return luaL_error(state, "%s", message.c_str());
}

std::optional<std::reference_wrapper<Board>> findBoard(std::string_view monitor, int workspace) {
  const auto found = activeBoards.find(workspace);
  if (found == activeBoards.end() || found->second.monitor != monitor)
    return std::nullopt;
  return found->second;
}

using DrawSurface = void (*)(Render::IElementRenderer*, WP<CSurfacePassElement>, const CRegion&);

constexpr float kMinimumZoom = 0.5F;
constexpr float kMaximumZoom = 1.0F;
constexpr float kManagementZoom = 0.9F;

float boundedZoom(float zoom) {
  return std::clamp(zoom, kMinimumZoom, kMaximumZoom);
}

Vector2D boundedPan(const Board& board, const MonitorGeometry& geometry) {
  const auto zoom = boundedZoom(board.zoom);
  const auto maximum = Vector2D{geometry.width * (1.0F - zoom) / 2.0F, geometry.height * (1.0F - zoom) / 2.0F};
  return {std::clamp(board.pan.x, -maximum.x, maximum.x), std::clamp(board.pan.y, -maximum.y, maximum.y)};
}

Vector2D transformPoint(const Vector2D& point, const MonitorGeometry& geometry, const Board& board) {
  const auto zoom = boundedZoom(board.zoom);
  const auto pan = boundedPan(board, geometry);
  const auto center = Vector2D{geometry.width / 2.0, geometry.height / 2.0};
  return center + (point - center) * zoom + pan;
}

MonitorGeometry monitorGeometry(std::string_view monitor);

void damageBoard(const Board& board) {
  if (g_pHyprRenderer == nullptr)
    return;
  g_pHyprRenderer->damageBox(board.geometry.x, board.geometry.y, board.geometry.width, board.geometry.height);
  persist();
}

void mouseMovedHook(CInputManager* input, IPointer::SMotionEvent event) {
  if (input != nullptr) {
    const auto cursor = input->getMouseCoordsInternal();
    for (const auto& [_, board] : activeBoards) {
      if (board.zoom >= kManagementZoom)
        continue;
      const auto& geometry = board.geometry;
      if (cursor.x < geometry.x || cursor.y < geometry.y || cursor.x >= geometry.x + geometry.width
          || cursor.y >= geometry.y + geometry.height)
        continue;
      const auto inverseZoom = 1.0 / boundedZoom(board.zoom);
      event.delta *= inverseZoom;
      event.unaccel *= inverseZoom;
      break;
    }
  }
  if (inputHook != nullptr && inputHook->m_original != nullptr)
    reinterpret_cast<void (*)(CInputManager*, IPointer::SMotionEvent)>(inputHook->m_original)(input, event);
}

void drawSurfaceHook(Render::IElementRenderer* renderer, WP<CSurfacePassElement> weakElement, const CRegion& damage) {
  auto* element = weakElement.get();
  if (element == nullptr || element->m_data.pWindow == nullptr || element->m_data.pMonitor == nullptr) {
    if (rendererHook != nullptr && rendererHook->m_original != nullptr)
      reinterpret_cast<DrawSurface>(rendererHook->m_original)(renderer, weakElement, damage);
    return;
  }

  const auto monitor = element->m_data.pMonitor.get();
  const auto window = element->m_data.pWindow.get();
  if (monitor == nullptr || window == nullptr) {
    if (rendererHook != nullptr && rendererHook->m_original != nullptr)
      reinterpret_cast<DrawSurface>(rendererHook->m_original)(renderer, weakElement, damage);
    return;
  }
  const auto identity = std::format("0x{:x}", reinterpret_cast<uintptr_t>(window));
  const auto board = std::ranges::find_if(
      activeBoards, [&identity](const auto& entry) { return entry.second.clients.contains(identity); });
  if (board == activeBoards.end() || board->second.monitor != monitor->m_name) {
    if (rendererHook != nullptr && rendererHook->m_original != nullptr)
      reinterpret_cast<DrawSurface>(rendererHook->m_original)(renderer, weakElement, damage);
    return;
  }

  const auto& geometry = board->second.geometry;
  const auto original = element->m_data;
  const auto transformed = transformPoint(original.pos, geometry, board->second);
  element->m_data.pos = transformed;
  element->m_data.localPos = transformPoint(original.localPos, geometry, board->second);
  element->m_data.w = original.w * boundedZoom(board->second.zoom);
  element->m_data.h = original.h * boundedZoom(board->second.zoom);
  if (element->m_data.clipBox.w > 0.0 && element->m_data.clipBox.h > 0.0) {
    const auto clipPosition = transformPoint({original.clipBox.x, original.clipBox.y}, geometry, board->second);
    element->m_data.clipBox.x = clipPosition.x;
    element->m_data.clipBox.y = clipPosition.y;
    element->m_data.clipBox.w = original.clipBox.w * boundedZoom(board->second.zoom);
    element->m_data.clipBox.h = original.clipBox.h * boundedZoom(board->second.zoom);
  }
  if (rendererHook != nullptr && rendererHook->m_original != nullptr)
    reinterpret_cast<DrawSurface>(rendererHook->m_original)(renderer, weakElement, damage);
  element->m_data = original;
}

std::optional<size_t> jsonFieldValue(const std::string& json,
                                     std::string_view field,
                                     size_t from = 0,
                                     size_t end = std::string::npos) {
  const auto key = "\"" + std::string{field} + "\"";
  const auto limit = end == std::string::npos ? json.size() : std::min(end, json.size());
  auto position = json.find(key, from);
  while (position != std::string::npos && position < limit) {
    const auto colon = json.find(':', position + key.size());
    if (colon == std::string::npos || colon >= limit)
      return std::nullopt;
    position = colon + 1;
    while (position < limit && std::isspace(static_cast<unsigned char>(json[position])))
      ++position;
    return position < limit ? std::optional<size_t>{position} : std::nullopt;
  }
  return std::nullopt;
}

std::optional<size_t> jsonStringFieldPosition(const std::string& json,
                                              std::string_view field,
                                              std::string_view expected,
                                              size_t from = 0,
                                              size_t end = std::string::npos) {
  for (auto value = jsonFieldValue(json, field, from, end); value;) {
    if (*value < json.size() && json[*value] == '"' && json.compare(*value + 1, expected.size(), expected) == 0
        && *value + expected.size() + 1 < json.size() && json[*value + expected.size() + 1] == '"')
      return value;
    value = jsonFieldValue(json, field, *value + 1, end);
  }
  return std::nullopt;
}

bool jsonStringFieldEquals(const std::string& json,
                           std::string_view field,
                           std::string_view expected,
                           size_t from = 0,
                           size_t end = std::string::npos) {
  return jsonStringFieldPosition(json, field, expected, from, end).has_value();
}

std::optional<int> jsonIntegerField(const std::string& json,
                                    std::string_view field,
                                    size_t from = 0,
                                    size_t end = std::string::npos) {
  const auto value = jsonFieldValue(json, field, from, end);
  if (!value)
    return std::nullopt;
  try {
    return std::stoi(json.substr(*value));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

int jsonNumber(const std::string& json, std::string_view field, int fallback) {
  return jsonIntegerField(json, field).value_or(fallback);
}

MonitorGeometry monitorGeometry(std::string_view monitor) {
  const auto json = HyprlandAPI::invokeHyprctlCommand("monitors", "", "j");
  const auto name = jsonFieldValue(json, "name");
  if (!name || !jsonStringFieldEquals(json, "name", monitor))
    return {};
  const auto object = json.substr(*name, json.find('}', *name) - *name);
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
  const auto clients = HyprlandAPI::invokeHyprctlCommand("clients", "", "j");
  const auto clientValue = jsonStringFieldPosition(clients, "address", identity);
  const auto client = clientValue ? *clientValue : std::string::npos;
  const auto nextClient =
      client == std::string::npos ? std::string::npos : jsonFieldValue(clients, "address", client + 1);
  const auto objectEnd = nextClient ? *nextClient : std::string::npos;
  const auto object =
      client == std::string::npos
          ? std::string{}
          : clients.substr(client, objectEnd == std::string::npos ? std::string::npos : objectEnd - client);
  if (!object.contains("\"floating\":true")) {
    const auto floating = invokeDispatcher("hl.dsp.window.float({window=\"" + address + "\"})");
    if (!floating.starts_with("ok"))
      return false;
  }
  const auto move = invokeDispatcher("hl.dsp.window.move({x=" + std::to_string(placement.x)
                                     + ",y=" + std::to_string(placement.y) + ",window=\"" + address + "\"})");
  if (!move.starts_with("ok"))
    return false;
  const auto resize = invokeDispatcher("hl.dsp.window.resize({x=" + std::to_string(placement.width)
                                       + ",y=" + std::to_string(placement.height) + ",window=\"" + address + "\"})");
  return resize.starts_with("ok");
}

int placementFailure(lua_State* state, std::string_view reason) {
  return activationFailure(state, reason);
}

bool jsonContainsMonitor(const std::string& json, std::string_view monitor) {
  return jsonStringFieldEquals(json, "name", monitor);
}

bool jsonContainsWorkspace(const std::string& json, int workspace, std::string_view monitor) {
  auto objectStart = jsonFieldValue(json, "id");
  while (objectStart) {
    std::optional<int> idValue;
    try {
      idValue = std::stoi(json.substr(*objectStart));
    } catch (const std::exception&) {
      idValue = std::nullopt;
    }
    const auto idEnd = json.find(',', *objectStart);
    if (!idValue || *idValue != workspace) {
      objectStart = jsonFieldValue(json, "id", idEnd == std::string::npos ? json.size() : idEnd + 1);
      continue;
    }
    const auto objectEnd = json.find('}', *objectStart);
    if (objectEnd == std::string::npos)
      return false;
    if (jsonStringFieldEquals(json, "monitor", monitor, *objectStart, objectEnd))
      return true;
    objectStart = jsonFieldValue(json, "id", objectEnd + 1);
  }
  return false;
}

bool jsonContainsWorkspace(const std::string& json, int workspace) {
  for (auto position = jsonFieldValue(json, "id"); position;) {
    try {
      if (std::stoi(json.substr(*position)) == workspace)
        return true;
    } catch (const std::exception&) {
    }
    const auto comma = json.find(',', *position);
    position = jsonFieldValue(json, "id", comma == std::string::npos ? json.size() : comma + 1);
  }
  return false;
}

bool jsonContainsClient(const std::string& json, std::string_view identity) {
  return jsonStringFieldEquals(json, "address", identity);
}

std::string invokeDispatcher(std::string_view expression) {
  return HyprlandAPI::invokeHyprctlCommand("dispatch", std::string{expression});
}

void pruneStaleClients() {
  const auto clients = HyprlandAPI::invokeHyprctlCommand("clients", "", "j");
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
}

bool jsonClientBelongsToBoard(const std::string& json,
                              std::string_view identity,
                              std::string_view monitor,
                              int workspace) {
  const auto address = jsonStringFieldPosition(json, "address", identity);
  if (!address || !jsonStringFieldEquals(json, "address", identity))
    return false;
  const auto objectStart = json.rfind('{', *address);
  const auto nextAddress = jsonFieldValue(json, "address", *address + 1);
  if (objectStart == std::string::npos)
    return false;
  const auto object =
      json.substr(objectStart, nextAddress == std::nullopt ? std::string::npos : *nextAddress - objectStart);
  // The clients JSON uses a numeric monitor id on current Hyprland builds;
  // the board's workspace-to-monitor assignment is the authoritative check.
  static_cast<void>(monitor);
  return jsonIntegerField(object, "id").value_or(0) == workspace;
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
  debugLog("activate begin monitor=" + monitor + " workspace=" + std::to_string(workspace)
           + " boardCount=" + std::to_string(activeBoards.size()));
  const auto existing = activeBoards.find(static_cast<int>(workspace));
  if (existing != activeBoards.end() && existing->second.monitor != monitor) {
    return activationFailure(
        state, "workspace " + std::to_string(workspace) + " is already assigned to " + existing->second.monitor);
  }

  const auto monitorData = HyprlandAPI::invokeHyprctlCommand("monitors", "", "j");
  if (!jsonContainsMonitor(monitorData, monitor)) {
    auto preview = monitorData.substr(0, 160);
    std::ranges::replace(preview, '\n', ' ');
    return activationFailure(state, "monitor " + monitor + " was not found; query=" + preview);
  }
  const auto workspaces = HyprlandAPI::invokeHyprctlCommand("workspaces", "", "j");
  if (!jsonContainsWorkspace(workspaces, static_cast<int>(workspace))) {
    return activationFailure(state, "workspace " + std::to_string(workspace) + " was not found");
  }
  if (!jsonContainsWorkspace(workspaces, static_cast<int>(workspace), monitor)
      && !jsonContainsWorkspace(workspaces, static_cast<int>(workspace), "")) {
    return activationFailure(state, "workspace " + std::to_string(workspace) + " is assigned to another monitor");
  }
  const auto dispatch = invokeDispatcher("hl.dsp.workspace.move({workspace=\"" + std::to_string(workspace)
                                         + "\",monitor=\"" + monitor + "\"})");
  if (!dispatch.starts_with("ok")) {
    auto response = dispatch.substr(0, 160);
    std::ranges::replace(response, '\n', ' ');
    return activationFailure(state, "compositor command failed: " + response);
  }
  if (!jsonContainsWorkspace(HyprlandAPI::invokeHyprctlCommand("workspaces", "", "j"), workspace, monitor)) {
    return activationFailure(state, "workspace verification failed");
  }
  auto [board, inserted] = activeBoards.try_emplace(
      static_cast<int>(workspace), Board{.monitor = monitor, .workspace = static_cast<int>(workspace)});
  static_cast<void>(inserted);
  board->second.geometry = monitorGeometry(monitor);
  debugLog("activated board workspace=" + std::to_string(workspace) + " monitor=" + monitor
           + " boardCount=" + std::to_string(activeBoards.size()));
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
  debugLog("register begin monitor=" + monitor + " workspace=" + std::to_string(workspace) + " identity=" + identity
           + " boardCount=" + std::to_string(activeBoards.size()));
  std::string activeBoardsDescription;
  for (const auto& [activeWorkspace, activeBoard] : activeBoards)
    activeBoardsDescription += " [" + std::to_string(activeWorkspace) + "," + activeBoard.monitor + "]";
  debugLog("register client identity=" + identity + " requested=[" + monitor + "," + std::to_string(workspace)
           + "] activeBoards=" + activeBoardsDescription);
  const auto clients = HyprlandAPI::invokeHyprctlCommand("clients", "", "j");
  if (!jsonContainsClient(clients, identity)) {
    auto preview = clients.substr(0, 240);
    std::ranges::replace(preview, '\n', ' ');
    return activationFailure(state, "client identity was not found; query=" + preview);
  }
  auto board = findBoard(monitor, static_cast<int>(workspace));
  if (!board) {
    const auto monitors = HyprlandAPI::invokeHyprctlCommand("monitors", "", "j");
    const auto workspaces = HyprlandAPI::invokeHyprctlCommand("workspaces", "", "j");
    if (jsonContainsMonitor(monitors, monitor)
        && jsonContainsWorkspace(workspaces, static_cast<int>(workspace), monitor)) {
      auto [restored, inserted] = activeBoards.emplace(
          static_cast<int>(workspace), Board{.monitor = monitor, .workspace = static_cast<int>(workspace)});
      static_cast<void>(inserted);
      restored->second.geometry = monitorGeometry(monitor);
      board = findBoard(monitor, static_cast<int>(workspace));
      debugLog("rehydrated board workspace=" + std::to_string(workspace) + " monitor=" + monitor);
    }
  }
  if (!board) {
    std::string activeBoardsDescription;
    for (const auto& [activeWorkspace, activeBoard] : activeBoards)
      activeBoardsDescription += " [" + std::to_string(activeWorkspace) + "," + activeBoard.monitor + "]";
    return activationFailure(state,
                             "client belongs to an inactive board; requested [" + monitor + ","
                                 + std::to_string(workspace) + "], active boards:" + activeBoardsDescription);
  }
  const auto alreadyRegistered = std::ranges::any_of(
      activeBoards, [&identity](const auto& entry) { return entry.second.clients.contains(identity); });
  if (alreadyRegistered)
    return activationFailure(state, "client identity is already registered");
  const auto clientOnBoard = jsonClientBelongsToBoard(clients, identity, monitor, static_cast<int>(workspace));
  if (!clientOnBoard) {
    const auto dispatch = invokeDispatcher("hl.dsp.window.move({workspace=\"" + std::to_string(workspace)
                                           + "\",follow=false,window=\"" + identity + "\"})");
    if (!dispatch.starts_with("ok"))
      return activationFailure(state, "failed to move client to board");
    if (!jsonClientBelongsToBoard(
            HyprlandAPI::invokeHyprctlCommand("clients", "", "j"), identity, monitor, static_cast<int>(workspace)))
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
  const auto dispatch = invokeDispatcher("hl.focus({window=\"" + std::string{address} + "\"})");
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
  board->get().zoom = boundedZoom(zoom);
  board->get().pan = boundedPan(board->get(), board->get().geometry);
  damageBoard(board->get());
  lua_pushboolean(state, true);
  return 1;
}

int setCameraLua(lua_State* state) {
  size_t monitorLength = 0;
  const auto* monitorValue = lua_tolstring(state, 1, &monitorLength);
  int workspaceOk = 0;
  const auto workspace = lua_tointegerx(state, 2, &workspaceOk);
  int valuesOk[3] = {};
  const auto zoom = static_cast<float>(lua_tonumberx(state, 3, &valuesOk[0]));
  const auto panX = static_cast<float>(lua_tonumberx(state, 4, &valuesOk[1]));
  const auto panY = static_cast<float>(lua_tonumberx(state, 5, &valuesOk[2]));
  if (monitorValue == nullptr || monitorLength == 0 || !workspaceOk || workspace <= 0 || !valuesOk[0] || !valuesOk[1]
      || !valuesOk[2])
    return activationFailure(state, "monitor, positive workspace, zoom, and pan values are required");
  const auto board = findBoard(std::string_view{monitorValue, monitorLength}, static_cast<int>(workspace));
  if (!board)
    return activationFailure(state, "board is not active");
  board->get().zoom = boundedZoom(zoom);
  board->get().pan = Vector2D{panX, panY};
  board->get().pan = boundedPan(board->get(), board->get().geometry);
  damageBoard(board->get());
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
    state << board.monitor << '\t' << board.workspace << '\t' << board.zoom << '\t' << board.pan.x << '\t'
          << board.pan.y << '\n';
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
                HyprlandAPI::invokeHyprctlCommand("clients", "", "j"), identity, board->second.monitor, workspace)) {
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
      const auto values = line.substr(second + 1);
      std::stringstream fields{values};
      std::string zoomValue;
      std::string panXValue;
      std::string panYValue;
      std::getline(fields, zoomValue, '\t');
      std::getline(fields, panXValue, '\t');
      std::getline(fields, panYValue, '\t');
      const auto zoom = std::stof(zoomValue);
      const auto monitor = line.substr(0, first);
      if (jsonContainsMonitor(HyprlandAPI::invokeHyprctlCommand("monitors", "", "j"), monitor)
          && jsonContainsWorkspace(HyprlandAPI::invokeHyprctlCommand("workspaces", "", "j"), workspace, monitor)) {
        auto [board, inserted] =
            activeBoards.emplace(workspace,
                                 Board{.monitor = monitor,
                                       .workspace = workspace,
                                       .zoom = boundedZoom(zoom),
                                       .pan = Vector2D{panXValue.empty() ? 0.0 : std::stof(panXValue),
                                                       panYValue.empty() ? 0.0 : std::stof(panYValue)}});
        static_cast<void>(inserted);
        board->second.geometry = monitorGeometry(monitor);
      }
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
  persist();
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
  debugLog("plugin init handle=" + std::to_string(reinterpret_cast<uintptr_t>(handle)));
  if (!supportedHost()) {
    pluginHandle = nullptr;
    return {};
  }

  rendererHook = installHook(kRenderClass, kRenderMethod);
  inputHook = installHook(kInputClass, kInputMethod);
  if (rendererHook == nullptr || inputHook == nullptr) {
    for (auto* hook : hooks)
      HyprlandAPI::removeFunctionHook(pluginHandle, hook);
    hooks.clear();
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
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "setCamera", setCameraLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "normal", normalLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "management", managementLua)
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "save", saveLua)) {
    report("failed to register workspace model API");
    PLUGIN_EXIT();
    return {};
  }
  restore();
  installLifecycleListeners();
  HyprlandAPI::addNotification(pluginHandle,
                               "[whiteboard] workspace model ready; camera renderer active",
                               CHyprColor{0.2F, 1.0F, 0.4F, 1.0F},
                               5000.0F);
  return {.name = "whiteboard",
          .description = "Monitor-assigned Whiteboard workspace model",
          .author = "zurat",
          .version = "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
  debugLog("plugin exit boardCount=" + std::to_string(activeBoards.size()));
  persist();
  for (auto* hook : hooks)
    HyprlandAPI::removeFunctionHook(pluginHandle, hook);
  hooks.clear();
  rendererHook = nullptr;
  inputHook = nullptr;
  lifecycleListeners.clear();
  activeBoards.clear();
  pluginHandle = nullptr;
}
