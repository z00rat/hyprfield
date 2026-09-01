#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <map>
#include <optional>
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

namespace hyprdimension {

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
  bool opening_floating = false;
  std::map<std::string, Placement> windows;
  std::map<std::string, std::string> popups;
};

std::map<std::string, Board> boards;

std::optional<int> integer(std::string_view value) {
  try {
    std::size_t consumed = 0;
    const auto result = std::stoi(std::string{value}, &consumed);
    return consumed == value.size() ? std::optional{result} : std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<double> decimal(std::string_view value) {
  try {
    std::size_t consumed = 0;
    const auto result = std::stod(std::string{value}, &consumed);
    return consumed == value.size() ? std::optional{result} : std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

bool isWindowAddress(std::string_view value) {
  if (value.size() < 3 || value.substr(0, 2) != "0x") {
    return false;
  }
  return std::all_of(value.begin() + 2, value.end(), [](const char character) {
    return std::isxdigit(static_cast<unsigned char>(character)) != 0;
  });
}

bool windowExists(std::string_view address) {
  const auto clients = HyprlandAPI::invokeHyprctlCommand("clients", "-j", "json");
  for (auto position = clients.find("\"address\""); position != std::string::npos;
       position = clients.find("\"address\"", position + 1)) {
    const auto colon = clients.find(':', position);
    const auto quote = clients.find('"', colon);
    if (colon != std::string::npos && quote != std::string::npos
        && clients.compare(quote + 1, address.size(), address) == 0) {
      return true;
    }
  }
  return false;
}

void notify(const std::string& text) {
  HyprlandAPI::addNotification(pluginHandle, text, CHyprColor{0.2F, 0.6F, 1.0F, 1.0F}, 5000.0F);
}

std::filesystem::path statePath() {
  if (const auto* state_home = std::getenv("XDG_STATE_HOME"); state_home != nullptr && *state_home != '\0') {
    return std::filesystem::path{state_home} / "hyprfield" / "hyprdimension.state";
  }
  if (const auto* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::filesystem::path{home} / ".local" / "state" / "hyprfield" / "hyprdimension.state";
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
    notify("hyprdimension: assign requires <monitor> <workspace>");
    return 0;
  }
  const auto already_assigned = std::ranges::any_of(
      boards, [&fields](const auto& entry) { return entry.first != fields[0] && entry.second.workspace == fields[1]; });
  if (already_assigned) {
    notify("hyprdimension workspace already assigned");
    return 0;
  }
  boardFor(fields[0]).workspace = fields[1];
  HyprlandAPI::invokeHyprctlCommand("dispatch", "moveworkspacetomonitor " + fields[1] + " " + fields[0]);
  notify("hyprdimension assigned " + fields[1] + " to " + fields[0]);
  saveState();
  return 0;
}

int configureLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || (fields.size() != 5 && fields.size() != 6)) {
    notify("hyprdimension: configure requires <monitor> <rows> <columns> <gap> <margin> [floating]");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  const auto rows = integer(fields[1]);
  const auto columns = integer(fields[2]);
  const auto gap = integer(fields[3]);
  const auto margin = integer(fields[4]);
  if (!rows || !columns || !gap || !margin || *rows < 1 || *columns < 1 || *gap < 0 || *margin < 0) {
    notify("hyprdimension grid configuration rejected");
    return 0;
  }
  board.rows = *rows;
  board.columns = *columns;
  board.gap = *gap;
  board.margin = *margin;
  if (fields.size() == 6 && fields[5] != "grid" && fields[5] != "floating") {
    notify("hyprdimension grid configuration rejected");
    return 0;
  }
  board.opening_floating = fields.size() == 6 && fields[5] == "floating";
  notify("hyprdimension grid configured " + fields[0]);
  saveState();
  return 0;
}

int openLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 2) {
    notify("hyprdimension: open requires <monitor> <window>");
    return 0;
  }
  if (!isWindowAddress(fields[1])) {
    notify("hyprdimension: open requires a Hyprland window address");
    return 0;
  }
  if (!windowExists(fields[1])) {
    notify("hyprdimension window does not exist");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  const auto slot = static_cast<int>(board.windows.size()) % std::max(1, board.columns);
  Placement placement{.row = static_cast<int>(board.windows.size()) / std::max(1, board.columns), .column = slot};
  if (board.opening_floating) {
    placement.floating = true;
  } else if (!inBounds(board, placement) || occupied(board, fields[1], placement)) {
    notify("hyprdimension grid is full");
    return 0;
  }
  board.windows[fields[1]] = placement;
  notify("hyprdimension window opened " + fields[1]);
  saveState();
  return 0;
}

int placeLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || (fields.size() != 4 && fields.size() != 6)) {
    notify("hyprdimension: place requires <monitor> <window> <row> <column> [<row-span> <column-span>]");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  const auto row = integer(fields[2]);
  const auto column = integer(fields[3]);
  if (!row || !column) {
    notify("hyprdimension placement rejected");
    return 0;
  }
  Placement candidate{.row = *row, .column = *column};
  if (fields.size() == 6) {
    const auto row_span = integer(fields[4]);
    const auto column_span = integer(fields[5]);
    if (!row_span || !column_span) {
      notify("hyprdimension placement rejected");
      return 0;
    }
    candidate.row_span = *row_span;
    candidate.column_span = *column_span;
  }
  if (!inBounds(board, candidate)) {
    notify("hyprdimension placement rejected");
    return 0;
  }
  auto current = board.windows.find(fields[1]);
  auto occupant = std::ranges::find_if(board.windows, [&fields, &candidate](const auto& entry) {
    return entry.first != fields[1] && !entry.second.floating && overlaps(entry.second, candidate);
  });
  if (current != board.windows.end() && occupant != board.windows.end() && candidate.row_span == 1
      && candidate.column_span == 1 && current->second.row_span == 1 && current->second.column_span == 1
      && occupant->second.row_span == 1 && occupant->second.column_span == 1) {
    std::swap(current->second.row, occupant->second.row);
    std::swap(current->second.column, occupant->second.column);
  } else if (occupied(board, fields[1], candidate)) {
    notify("hyprdimension placement rejected");
    return 0;
  }
  board.windows[fields[1]] = candidate;
  notify("hyprdimension window placed " + fields[1]);
  saveState();
  return 0;
}

int floatingLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 2) {
    notify("hyprdimension: floating requires <monitor> <window>");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  auto found = board.windows.find(fields[1]);
  if (found == board.windows.end()) {
    notify("hyprdimension window not found");
    return 0;
  }
  if (found->second.floating) {
    auto grid_placement = found->second;
    grid_placement.floating = false;
    if (!inBounds(board, grid_placement) || occupied(board, fields[1], grid_placement)) {
      notify("hyprdimension grid placement rejected");
      return 0;
    }
  }
  found->second.floating = !found->second.floating;
  notify("hyprdimension window " + fields[1] + (found->second.floating ? " floating" : " grid"));
  saveState();
  return 0;
}

int zoomLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 2) {
    notify("hyprdimension: zoom requires <monitor> <value>");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  const auto value = decimal(fields[1]);
  if (!value) {
    notify("hyprdimension zoom rejected");
    return 0;
  }
  const auto requested = std::clamp(*value, 0.25, 1.0);
  board.zoom = board.zoom >= 1.0 && requested < 0.9 ? 0.5 : requested;
  notify("hyprdimension " + fields[0] + (board.zoom < 1.0 ? " management" : " normal"));
  saveState();
  return 0;
}

int cameraLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 3) {
    notify("hyprdimension: camera requires <monitor> <x> <y>");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  const auto x = integer(fields[1]);
  const auto y = integer(fields[2]);
  if (!x || !y) {
    notify("hyprdimension camera move rejected");
    return 0;
  }
  board.camera_x = *x;
  board.camera_y = *y;
  notify("hyprdimension camera moved " + fields[0]);
  saveState();
  return 0;
}

int focusLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 2) {
    notify("hyprdimension: focus requires <monitor> <window>");
    return 0;
  }
  if (!isWindowAddress(fields[1])) {
    notify("hyprdimension: focus requires a Hyprland window address");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  if (!board.windows.contains(fields[1])) {
    notify("hyprdimension window not found");
    return 0;
  }
  board.zoom = 1.0;
  HyprlandAPI::invokeHyprctlCommand("dispatch", "focuswindow address:" + fields[1]);
  notify("hyprdimension focused " + fields[1]);
  saveState();
  return 0;
}

int popupLua(lua_State* state) {
  std::vector<std::string> fields;
  if (!parse(luaL_optstring(state, 1, ""), fields) || fields.size() != 3) {
    notify("hyprdimension: popup requires <monitor> <popup> <parent>");
    return 0;
  }
  auto& board = boardFor(fields[0]);
  if (!board.windows.contains(fields[2])) {
    notify("hyprdimension popup parent not found");
    return 0;
  }
  board.popups[fields[1]] = fields[2];
  notify("hyprdimension popup attached " + fields[1]);
  saveState();
  return 0;
}

void initialize(HANDLE handle) {
  pluginHandle = handle;
  boards.clear();
  loadState();
  HyprlandAPI::addLuaFunction(handle, "hyprdimension", "assign", assignLua);
  HyprlandAPI::addLuaFunction(handle, "hyprdimension", "configure", configureLua);
  HyprlandAPI::addLuaFunction(handle, "hyprdimension", "open", openLua);
  HyprlandAPI::addLuaFunction(handle, "hyprdimension", "place", placeLua);
  HyprlandAPI::addLuaFunction(handle, "hyprdimension", "floating", floatingLua);
  HyprlandAPI::addLuaFunction(handle, "hyprdimension", "zoom", zoomLua);
  HyprlandAPI::addLuaFunction(handle, "hyprdimension", "camera", cameraLua);
  HyprlandAPI::addLuaFunction(handle, "hyprdimension", "focus", focusLua);
  HyprlandAPI::addLuaFunction(handle, "hyprdimension", "popup", popupLua);
  notify("hyprdimension plugin loaded");
}

void shutdown() {
  saveState();
  boards.clear();
  pluginHandle = nullptr;
}

}  // namespace hyprdimension
