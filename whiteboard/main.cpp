#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace {

HANDLE pluginHandle = nullptr;

struct Placement {
  int row = 0;
  int column = 0;
  int row_span = 1;
  int column_span = 1;
  bool floating = false;
};

struct Board {
  std::string workspace;
  int rows = 2;
  int columns = 4;
  int gap = 16;
  int margin = 32;
  double zoom = 1.0;
  int camera_x = 0;
  int camera_y = 0;
  std::map<std::string, Placement> windows;
  std::map<std::string, std::string> popups;
};

std::map<std::string, Board> boards;

void notify(const std::string& text) {
  HyprlandAPI::addNotification(pluginHandle, text, CHyprColor{0.2F, 0.6F, 1.0F, 1.0F}, 5000.0F);
}

std::filesystem::path statePath() {
  if (const auto* state_home = std::getenv("XDG_STATE_HOME"); state_home != nullptr && *state_home != '\0') {
    return std::filesystem::path{state_home} / "hyprfield" / "whiteboard.state";
  }
  if (const auto* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::filesystem::path{home} / ".local" / "state" / "hyprfield" / "whiteboard.state";
  }
  return {};
}

void loadState() {
  const auto path = statePath();
  if (path.empty()) {
    return;
  }
  std::ifstream input{path};
  std::string kind;
  while (input >> kind) {
    if (kind == "board") {
      std::string monitor;
      Board board;
      input >> monitor >> board.workspace >> board.rows >> board.columns >> board.gap >> board.margin >> board.zoom
          >> board.camera_x >> board.camera_y;
      boards[monitor] = std::move(board);
    } else if (kind == "window") {
      std::string monitor;
      std::string id;
      Placement placement;
      input >> monitor >> id >> placement.row >> placement.column >> placement.row_span >> placement.column_span
          >> placement.floating;
      boards[monitor].windows[id] = placement;
    } else if (kind == "popup") {
      std::string monitor;
      std::string id;
      std::string parent;
      input >> monitor >> id >> parent;
      boards[monitor].popups[id] = parent;
    }
  }
}

void saveState() {
  const auto path = statePath();
  if (path.empty()) {
    return;
  }
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream output{path};
  if (!output) {
    return;
  }
  for (const auto& [monitor, board] : boards) {
    output << "board " << monitor << ' ' << board.workspace << ' ' << board.rows << ' ' << board.columns << ' '
           << board.gap << ' ' << board.margin << ' ' << board.zoom << ' ' << board.camera_x << ' ' << board.camera_y
           << '\n';
    for (const auto& [id, placement] : board.windows) {
      output << "window " << monitor << ' ' << id << ' ' << placement.row << ' ' << placement.column << ' '
             << placement.row_span << ' ' << placement.column_span << ' ' << placement.floating << '\n';
    }
    for (const auto& [id, parent] : board.popups) {
      output << "popup " << monitor << ' ' << id << ' ' << parent << '\n';
    }
  }
}

bool parse(std::string_view argument, std::vector<std::string>& fields) {
  std::istringstream input{std::string{argument}};
  std::string field;
  while (input >> field) {
    fields.push_back(std::move(field));
  }
  return !fields.empty();
}

bool inBounds(const Board& board, const Placement& placement) {
  return placement.row >= 0 && placement.column >= 0 && placement.row_span > 0 && placement.column_span > 0
         && placement.row + placement.row_span <= board.rows
         && placement.column + placement.column_span <= board.columns;
}

bool overlaps(const Placement& left, const Placement& right) {
  return left.row < right.row + right.row_span && right.row < left.row + left.row_span
         && left.column < right.column + right.column_span && right.column < left.column + left.column_span;
}

bool occupied(const Board& board, const std::string& id, const Placement& candidate) {
  return std::ranges::any_of(board.windows, [&id, &candidate](const auto& entry) {
    return entry.first != id && !entry.second.floating && overlaps(entry.second, candidate);
  });
}

Board& boardFor(const std::string& monitor) {
  return boards[monitor];
}

int assignLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 2) {
    notify("whiteboard: assign requires <monitor> <workspace>");
    return 0;
  }
  boardFor(fields[0]).workspace = fields[1];
  notify("whiteboard assigned " + fields[1] + " to " + fields[0]);
  saveState();
  return 0;
}

int configureLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 5) {
    notify("whiteboard: configure requires <monitor> <rows> <columns> <gap> <margin>");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  board.rows = std::stoi(fields[1]);
  board.columns = std::stoi(fields[2]);
  board.gap = std::stoi(fields[3]);
  board.margin = std::stoi(fields[4]);
  notify("whiteboard grid configured " + fields[0]);
  saveState();
  return 0;
}

int openLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 2) {
    notify("whiteboard: open requires <monitor> <window>");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  const auto slot = static_cast<int>(board.windows.size()) % std::max(1, board.columns);
  Placement placement{.row = static_cast<int>(board.windows.size()) / std::max(1, board.columns), .column = slot};
  if (!inBounds(board, placement) || occupied(board, fields[1], placement)) {
    placement.floating = true;
  }
  board.windows[fields[1]] = placement;
  notify("whiteboard window opened " + fields[1]);
  saveState();
  return 0;
}

int placeLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || (fields.size() != 4 && fields.size() != 6)) {
    notify("whiteboard: place requires <monitor> <window> <row> <column> [<row-span> <column-span>]");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  Placement candidate{.row = std::stoi(fields[2]), .column = std::stoi(fields[3])};
  if (fields.size() == 6) {
    candidate.row_span = std::stoi(fields[4]);
    candidate.column_span = std::stoi(fields[5]);
  }
  if (!inBounds(board, candidate) || occupied(board, fields[1], candidate)) {
    notify("whiteboard placement rejected");
    return 0;
  }
  board.windows[fields[1]] = candidate;
  notify("whiteboard window placed " + fields[1]);
  saveState();
  return 0;
}

int floatingLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 2) {
    notify("whiteboard: floating requires <monitor> <window>");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  auto found = board.windows.find(fields[1]);
  if (found == board.windows.end()) {
    notify("whiteboard window not found");
    return 0;
  }
  found->second.floating = !found->second.floating;
  notify("whiteboard window " + fields[1] + (found->second.floating ? " floating" : " grid"));
  saveState();
  return 0;
}

int zoomLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 2) {
    notify("whiteboard: zoom requires <monitor> <value>");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  const auto requested = std::clamp(std::stod(fields[1]), 0.25, 1.0);
  board.zoom = requested < 0.9 ? 0.5 : requested;
  notify("whiteboard " + fields[0] + (board.zoom < 1.0 ? " management" : " normal"));
  saveState();
  return 0;
}

int cameraLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 3) {
    notify("whiteboard: camera requires <monitor> <x> <y>");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  board.camera_x = std::stoi(fields[1]);
  board.camera_y = std::stoi(fields[2]);
  notify("whiteboard camera moved " + fields[0]);
  saveState();
  return 0;
}

int focusLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 2) {
    notify("whiteboard: focus requires <monitor> <window>");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  if (!board.windows.contains(fields[1])) {
    notify("whiteboard window not found");
    return 0;
  }
  board.zoom = 1.0;
  notify("whiteboard focused " + fields[1]);
  saveState();
  return 0;
}

int popupLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 3) {
    notify("whiteboard: popup requires <monitor> <popup> <parent>");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  if (!board.windows.contains(fields[2])) {
    notify("whiteboard popup parent not found");
    return 0;
  }
  board.popups[fields[1]] = fields[2];
  notify("whiteboard popup attached " + fields[1]);
  saveState();
  return 0;
}

}  // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() {
  return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  pluginHandle = handle;
  boards.clear();
  loadState();
  HyprlandAPI::addLuaFunction(handle, "whiteboard", "assign", assignLua);
  HyprlandAPI::addLuaFunction(handle, "whiteboard", "configure", configureLua);
  HyprlandAPI::addLuaFunction(handle, "whiteboard", "open", openLua);
  HyprlandAPI::addLuaFunction(handle, "whiteboard", "place", placeLua);
  HyprlandAPI::addLuaFunction(handle, "whiteboard", "floating", floatingLua);
  HyprlandAPI::addLuaFunction(handle, "whiteboard", "zoom", zoomLua);
  HyprlandAPI::addLuaFunction(handle, "whiteboard", "camera", cameraLua);
  HyprlandAPI::addLuaFunction(handle, "whiteboard", "focus", focusLua);
  HyprlandAPI::addLuaFunction(handle, "whiteboard", "popup", popupLua);
  notify("whiteboard plugin loaded");
  return {.name = "whiteboard",
          .description = "Monitor-scoped whiteboard workspaces",
          .author = "zurat",
          .version = "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
  saveState();
  boards.clear();
  pluginHandle = nullptr;
}
