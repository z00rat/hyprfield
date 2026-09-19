#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/state/MonitorState.hpp>
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

struct MonitorGeometry {
  int x = 0;
  int y = 0;
  int width = 1920;
  int height = 1080;
};

struct CanvasBounds {
  double minX = 0.0;
  double minY = 0.0;
  double maxX = 0.0;
  double maxY = 0.0;
};

// Window decorations and shadows are rendered outside the client rectangle.
// Keep those pixels in the offscreen canvas instead of clipping them at the
// outermost registered client.
constexpr double kCanvasRenderPadding = 128.0;

constexpr std::string_view kPluginName = "whiteboard";
constexpr std::string_view kRenderClass = "IHyprRenderer";
constexpr std::string_view kRenderMethod = "renderWorkspaceWindows";
constexpr std::string_view kInputClass = "CInputManager";
constexpr std::string_view kInputMethod = "onMouseMoved";
constexpr std::string_view kWindowClass = "CWindow";
constexpr std::string_view kWindowVisibilityMethod = "visibleOnMonitor";

HANDLE pluginHandle = nullptr;
std::vector<CFunctionHook*> hooks;
std::vector<CHyprSignalListener> lifecycleListeners;

struct Board {
  struct Geometry {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };
  std::string monitor;
  int workspace;
  PHLWORKSPACE workspaceRef;
  float zoom = 1.0F;
  Vector2D pan;
  float targetZoom = 1.0F;
  Vector2D targetPan;
  float animationStartZoom = 1.0F;
  Vector2D animationStartPan;
  std::chrono::steady_clock::time_point cameraAnimationStarted = std::chrono::steady_clock::now();
  bool cameraAnimating = false;
  MonitorGeometry geometry;
  struct Placement {
    std::string layer = "grid";
    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;
    // Stable whiteboard coordinates. Camera operations derive temporary
    // compositor geometry from this rectangle and never rewrite it.
    Geometry world;
  };
  struct Grid {
    int rows = 2;
    int columns = 4;
    int canvasMinRow = 0;
    int canvasMaxRow = 2;
    int canvasMinColumn = 0;
    int canvasMaxColumn = 4;
    int gap = 16;
    int margin = 32;
    std::string openingLayer = "grid";
  };
  Grid grid;
  std::unordered_map<std::string, Placement> clients;
};

std::unordered_map<int, Board> activeBoards;
CFunctionHook* workspaceRenderHook = nullptr;
CFunctionHook* inputHook = nullptr;
CFunctionHook* windowVisibilityHook = nullptr;
PHLMONITOR canvasCaptureMonitor;
PHLWORKSPACE canvasCaptureWorkspace;
void renderWorkspaceWindowsHook(Render::IHyprRenderer*, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp&);
void mouseMovedHook(CInputManager*, IPointer::SMotionEvent);
bool visibleOnMonitorHook(Desktop::View::CWindow*, PHLMONITOR);
void updateCamera(Board&);
bool jsonClientBelongsToBoard(const std::string&, std::string_view, std::string_view, int);

std::string invokeDispatcher(std::string_view expression);

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

  const auto destination = methodName == kRenderMethod  ? reinterpret_cast<const void*>(&renderWorkspaceWindowsHook)
                           : methodName == kInputMethod ? reinterpret_cast<const void*>(&mouseMovedHook)
                                                        : reinterpret_cast<const void*>(&visibleOnMonitorHook);
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

constexpr float kMinimumZoom = 0.25F;
constexpr float kManagementThreshold = 0.9F;

float boundedZoom(float zoom) {
  return std::clamp(zoom, kMinimumZoom, 1.0F);
}

Vector2D boundedPan(const Board& board, const MonitorGeometry& geometry) {
  const auto zoom = boundedZoom(board.zoom);
  const auto contentWidth = geometry.width - board.grid.margin * 2 - board.grid.gap * (board.grid.columns - 1);
  const auto columnEdge = [contentWidth, &board](int column) {
    return std::floor(column * contentWidth / static_cast<double>(board.grid.columns)) + column * board.grid.gap;
  };
  const auto contentHeight = geometry.height - board.grid.margin * 2 - board.grid.gap * (board.grid.rows - 1);
  const auto rowEdge = [contentHeight, &board](int row) {
    return std::floor(row * contentHeight / static_cast<double>(board.grid.rows)) + row * board.grid.gap;
  };
  const auto canvasLeft = board.grid.margin + columnEdge(board.grid.canvasMinColumn);
  const auto canvasRight = board.grid.margin + columnEdge(board.grid.canvasMaxColumn) - board.grid.gap;
  const auto canvasTop = board.grid.margin + rowEdge(board.grid.canvasMinRow);
  const auto canvasBottom = board.grid.margin + rowEdge(board.grid.canvasMaxRow) - board.grid.gap;
  const auto center = Vector2D{geometry.width / 2.0, geometry.height / 2.0};
  const auto leftExtent = std::abs(center.x + (canvasLeft - center.x) * zoom);
  const auto rightExtent = std::abs(geometry.width - (center.x + (canvasRight - center.x) * zoom));
  const auto topExtent = std::abs(center.y + (canvasTop - center.y) * zoom);
  const auto bottomExtent = std::abs(geometry.height - (center.y + (canvasBottom - center.y) * zoom));
  const auto maximum = Vector2D{std::max(leftExtent, rightExtent), std::max(topExtent, bottomExtent)};
  return {std::clamp(board.pan.x, -maximum.x, maximum.x), std::clamp(board.pan.y, -maximum.y, maximum.y)};
}

Board::Geometry projectGeometry(const Board& board, const Board::Geometry& world) {
  const auto zoom = boundedZoom(board.zoom);
  const auto monitorCenter =
      Vector2D{board.geometry.x + board.geometry.width / 2.0, board.geometry.y + board.geometry.height / 2.0};
  const auto worldCenter = Vector2D{world.x + world.width / 2.0, world.y + world.height / 2.0};
  const auto screenCenter = monitorCenter + (worldCenter - monitorCenter) * zoom + board.pan;
  const auto width = static_cast<int>(std::lround(world.width * zoom));
  const auto height = static_cast<int>(std::lround(world.height * zoom));
  return {.x = static_cast<int>(std::lround(screenCenter.x - width / 2.0)),
          .y = static_cast<int>(std::lround(screenCenter.y - height / 2.0)),
          .width = width,
          .height = height};
}

MonitorGeometry monitorGeometry(std::string_view monitor);

void damageBoard(const Board& board) {
  if (g_pHyprRenderer == nullptr)
    return;
  g_pHyprRenderer->damageBox(board.geometry.x, board.geometry.y, board.geometry.width, board.geometry.height);
}

CanvasBounds canvasBounds(const Board& board) {
  CanvasBounds bounds{.minX = static_cast<double>(board.geometry.x),
                      .minY = static_cast<double>(board.geometry.y),
                      .maxX = static_cast<double>(board.geometry.x + board.geometry.width),
                      .maxY = static_cast<double>(board.geometry.y + board.geometry.height)};
  for (const auto& [_, placement] : board.clients) {
    bounds.minX = std::min(bounds.minX, static_cast<double>(placement.world.x));
    bounds.minY = std::min(bounds.minY, static_cast<double>(placement.world.y));
    bounds.maxX = std::max(bounds.maxX, static_cast<double>(placement.world.x + placement.world.width));
    bounds.maxY = std::max(bounds.maxY, static_cast<double>(placement.world.y + placement.world.height));
  }
  bounds.minX -= kCanvasRenderPadding;
  bounds.minY -= kCanvasRenderPadding;
  bounds.maxX += kCanvasRenderPadding;
  bounds.maxY += kCanvasRenderPadding;
  return bounds;
}

void mouseMovedHook(CInputManager* input, IPointer::SMotionEvent event) {
  if (input != nullptr) {
    const auto cursor = input->getMouseCoordsInternal();
    for (const auto& [_, board] : activeBoards) {
      const auto& geometry = board.geometry;
      if (cursor.x < geometry.x || cursor.y < geometry.y || cursor.x >= geometry.x + geometry.width
          || cursor.y >= geometry.y + geometry.height)
        continue;
      break;
    }
  }
  if (inputHook != nullptr && inputHook->m_original != nullptr)
    reinterpret_cast<void (*)(CInputManager*, IPointer::SMotionEvent)>(inputHook->m_original)(input, event);
}

bool visibleOnMonitorHook(Desktop::View::CWindow* window, PHLMONITOR monitor) {
  if (window != nullptr && monitor != nullptr && canvasCaptureMonitor == monitor && canvasCaptureWorkspace != nullptr
      && window->m_workspace == canvasCaptureWorkspace)
    return true;

  if (windowVisibilityHook != nullptr && windowVisibilityHook->m_original != nullptr)
    return reinterpret_cast<bool (*)(Desktop::View::CWindow*, PHLMONITOR)>(windowVisibilityHook->m_original)(window,
                                                                                                             monitor);
  return false;
}

void renderPassIntoFramebuffer(Render::IHyprRenderer* renderer,
                               Render::CRenderPass& pass,
                               SP<Render::IFramebuffer> framebuffer,
                               PHLMONITOR monitor,
                               const CanvasBounds& bounds) {
  auto& renderData = renderer->m_renderData;
  const auto oldDamage = renderData.damage.copy();
  const auto oldFinalDamage = renderData.finalDamage.copy();
  const auto oldWindow = renderData.currentWindow;
  const auto oldSurface = renderData.surface;
  const auto oldClipBox = renderData.clipBox;
  const auto oldRenderModif = renderData.renderModif;
  const auto oldProjectionType = renderData.projectionType;
  const auto oldFbSize = renderData.fbSize;
  const auto oldTransformDamage = renderData.transformDamage;
  const auto oldPrimarySurfaceUVTopLeft = renderData.primarySurfaceUVTopLeft;
  const auto oldPrimarySurfaceUVBottomRight = renderData.primarySurfaceUVBottomRight;
  const auto scale = monitor->m_scale;
  const auto framebufferSize =
      Vector2D{std::ceil((bounds.maxX - bounds.minX) * scale), std::ceil((bounds.maxY - bounds.minY) * scale)};
  const CRegion canvasDamage{(bounds.minX - monitor->m_position.x) * scale,
                             (bounds.minY - monitor->m_position.y) * scale,
                             framebufferSize.x,
                             framebufferSize.y};

  {
    auto guard = renderer->bindTempFB(framebuffer);
    renderData.fbSize = framebufferSize;
    renderer->setProjectionType(Render::RPT_EXPORT);
    renderer->setViewport(0, 0, sc<int>(framebufferSize.x), sc<int>(framebufferSize.y));
    renderData.currentWindow.reset();
    renderData.surface.reset();
    renderData.clipBox = {};
    renderData.renderModif = {};
    renderData.renderModif.modifs.emplace_back(
        Render::SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE,
        Vector2D{(monitor->m_position.x - bounds.minX) * scale, (monitor->m_position.y - bounds.minY) * scale});
    renderData.transformDamage = false;
    renderData.primarySurfaceUVTopLeft = Vector2D(-1, -1);
    renderData.primarySurfaceUVBottomRight = Vector2D(-1, -1);
    renderData.damage = canvasDamage;
    renderData.finalDamage = canvasDamage;

    renderer->draw(CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
    pass.render(canvasDamage);
  }

  renderData.damage = oldDamage;
  renderData.finalDamage = oldFinalDamage;
  renderData.currentWindow = oldWindow;
  renderData.surface = oldSurface;
  renderData.clipBox = oldClipBox;
  renderData.renderModif = oldRenderModif;
  renderData.fbSize = oldFbSize;
  renderData.transformDamage = oldTransformDamage;
  renderer->setProjectionType(oldProjectionType);
  renderer->setViewport(0, 0, sc<int>(monitor->m_pixelSize.x), sc<int>(monitor->m_pixelSize.y));
  renderData.primarySurfaceUVTopLeft = oldPrimarySurfaceUVTopLeft;
  renderData.primarySurfaceUVBottomRight = oldPrimarySurfaceUVBottomRight;
}

CBox canvasOutputBox(const Board& board, PHLMONITOR monitor, const CanvasBounds& bounds) {
  const auto zoom = boundedZoom(board.zoom);
  const auto monitorCenter =
      Vector2D{board.geometry.x + board.geometry.width / 2.0, board.geometry.y + board.geometry.height / 2.0};
  const auto canvasCenter = Vector2D{(bounds.minX + bounds.maxX) / 2.0, (bounds.minY + bounds.maxY) / 2.0};
  const auto logicalPosition = monitorCenter + (canvasCenter - monitorCenter) * zoom + board.pan;
  const auto logicalSize = Vector2D{(bounds.maxX - bounds.minX) * zoom, (bounds.maxY - bounds.minY) * zoom};
  return {(logicalPosition - Vector2D{monitor->m_position.x, monitor->m_position.y}) * monitor->m_scale
              - logicalSize * (monitor->m_scale / 2.0),
          logicalSize * monitor->m_scale};
}

void renderWorkspaceWindowsHook(Render::IHyprRenderer* renderer,
                                PHLMONITOR monitor,
                                PHLWORKSPACE workspace,
                                const Time::steady_tp& time) {
  if (renderer == nullptr || monitor == nullptr || workspace == nullptr || monitor->m_activeWorkspace != workspace
      || workspaceRenderHook == nullptr || workspaceRenderHook->m_original == nullptr) {
    if (workspaceRenderHook != nullptr && workspaceRenderHook->m_original != nullptr)
      reinterpret_cast<void (*)(Render::IHyprRenderer*, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp&)>(
          workspaceRenderHook->m_original)(renderer, monitor, workspace, time);
    return;
  }

  const auto board = std::ranges::find_if(activeBoards, [monitor, workspace](const auto& entry) {
    return entry.second.monitor == monitor->m_name
           && (entry.second.workspaceRef == nullptr || entry.second.workspaceRef == workspace);
  });
  if (board == activeBoards.end()) {
    reinterpret_cast<void (*)(Render::IHyprRenderer*, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp&)>(
        workspaceRenderHook->m_original)(renderer, monitor, workspace, time);
    return;
  }

  board->second.workspaceRef = workspace;
  updateCamera(board->second);
  const auto transformed = board->second.zoom != 1.0F || board->second.pan.x != 0.0 || board->second.pan.y != 0.0;
  if (!transformed) {
    reinterpret_cast<void (*)(Render::IHyprRenderer*, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp&)>(
        workspaceRenderHook->m_original)(renderer, monitor, workspace, time);
    return;
  }

  const auto bounds = canvasBounds(board->second);
  const auto framebufferSize = Vector2D{std::ceil((bounds.maxX - bounds.minX) * monitor->m_scale),
                                        std::ceil((bounds.maxY - bounds.minY) * monitor->m_scale)};
  const auto framebuffer = renderer->createFB("whiteboard canvas");
  if (!framebuffer || !framebuffer->alloc(sc<int>(framebufferSize.x), sc<int>(framebufferSize.y))) {
    reinterpret_cast<void (*)(Render::IHyprRenderer*, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp&)>(
        workspaceRenderHook->m_original)(renderer, monitor, workspace, time);
    return;
  }

  Render::CRenderPass windowPass;
  {
    auto redirect = renderer->redirectPass(&windowPass);
    canvasCaptureMonitor = monitor;
    canvasCaptureWorkspace = workspace;
    reinterpret_cast<void (*)(Render::IHyprRenderer*, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp&)>(
        workspaceRenderHook->m_original)(renderer, monitor, workspace, time);
    canvasCaptureWorkspace.reset();
    canvasCaptureMonitor.reset();
  }

  renderPassIntoFramebuffer(renderer, windowPass, framebuffer, monitor, bounds);
  renderer->currentPass().add(makeUnique<CTexPassElement>(CTexPassElement::SRenderData{
      .tex = framebuffer->getTexture(),
      .box = canvasOutputBox(board->second, monitor, bounds),
      .a = 1.0F,
  }));
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

double jsonNumber(const std::string& json, std::string_view field, double fallback) {
  const auto value = jsonFieldValue(json, field);
  if (!value)
    return fallback;
  try {
    return std::stod(json.substr(*value));
  } catch (const std::exception&) {
    return fallback;
  }
}

MonitorGeometry monitorGeometry(std::string_view monitor) {
  if (State::monitorState() != nullptr) {
    for (const auto& candidate : State::monitorState()->monitors()) {
      if (candidate == nullptr || candidate->m_name != monitor)
        continue;
      return {.x = static_cast<int>(std::lround(candidate->m_position.x)),
              .y = static_cast<int>(std::lround(candidate->m_position.y)),
              .width = static_cast<int>(std::lround(candidate->m_size.x)),
              .height = static_cast<int>(std::lround(candidate->m_size.y))};
    }
  }
  const auto json = HyprlandAPI::invokeHyprctlCommand("monitors", "", "j");
  const auto name = jsonFieldValue(json, "name");
  if (!name || !jsonStringFieldEquals(json, "name", monitor))
    return {};
  const auto nextMonitor = json.find("\"name\"", *name + 1);
  const auto object = json.substr(*name, nextMonitor == std::string::npos ? std::string::npos : nextMonitor - *name);
  const auto scale = std::max(1.0, jsonNumber(object, "scale", 1.0));
  debugLog("monitor geometry monitor=" + std::string{monitor} + " scale=" + std::to_string(scale)
           + " raw=" + std::to_string(jsonNumber(object, "width", 0.0)) + "x"
           + std::to_string(jsonNumber(object, "height", 0.0)));
  return {.x = static_cast<int>(std::lround(jsonNumber(object, "x", 0.0))),
          .y = static_cast<int>(std::lround(jsonNumber(object, "y", 0.0))),
          .width = static_cast<int>(std::lround(jsonNumber(object, "width", 1920.0) / scale)),
          .height = static_cast<int>(std::lround(jsonNumber(object, "height", 1080.0) / scale))};
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

void recomputeCanvasBounds(Board& board) {
  board.grid.canvasMinRow = 0;
  board.grid.canvasMaxRow = board.grid.rows;
  board.grid.canvasMinColumn = 0;
  board.grid.canvasMaxColumn = board.grid.columns;
  for (const auto& [_, placement] : board.clients) {
    if (placement.layer != "grid")
      continue;
    board.grid.canvasMinRow = std::min(board.grid.canvasMinRow, placement.row);
    board.grid.canvasMaxRow = std::max(board.grid.canvasMaxRow, placement.row + placement.rowSpan);
    board.grid.canvasMinColumn = std::min(board.grid.canvasMinColumn, placement.column);
    board.grid.canvasMaxColumn = std::max(board.grid.canvasMaxColumn, placement.column + placement.columnSpan);
  }
}

void setGeometry(Board& board, Board::Placement& placement, std::string_view) {
  const auto& geometry = board.geometry;
  const auto contentWidth = geometry.width - board.grid.margin * 2 - board.grid.gap * (board.grid.columns - 1);
  const auto contentHeight = geometry.height - board.grid.margin * 2 - board.grid.gap * (board.grid.rows - 1);
  const auto columnEdge = [contentWidth, &board](int column) {
    return std::floor(column * contentWidth / static_cast<double>(board.grid.columns)) + column * board.grid.gap;
  };
  const auto rowEdge = [contentHeight, &board](int row) {
    return std::floor(row * contentHeight / static_cast<double>(board.grid.rows)) + row * board.grid.gap;
  };
  const auto left = columnEdge(placement.column);
  const auto right = columnEdge(placement.column + placement.columnSpan) - board.grid.gap;
  const auto top = rowEdge(placement.row);
  const auto bottom = rowEdge(placement.row + placement.rowSpan) - board.grid.gap;
  placement.world.x = geometry.x + board.grid.margin + left;
  placement.world.y = geometry.y + board.grid.margin + top;
  placement.world.width = right - left;
  placement.world.height = bottom - top;
  debugLog("grid monitorBox=" + std::to_string(geometry.x) + "," + std::to_string(geometry.y) + " "
           + std::to_string(geometry.width) + "x" + std::to_string(geometry.height)
           + " cells=" + std::to_string(board.grid.rows) + "x" + std::to_string(board.grid.columns)
           + " gap=" + std::to_string(board.grid.gap) + " margin=" + std::to_string(board.grid.margin)
           + " placement=" + std::to_string(placement.row) + "," + std::to_string(placement.column) + "+"
           + std::to_string(placement.rowSpan) + "x" + std::to_string(placement.columnSpan)
           + " box=" + std::to_string(placement.world.x) + "," + std::to_string(placement.world.y) + " "
           + std::to_string(placement.world.width) + "x" + std::to_string(placement.world.height));
}

bool dispatchScreenGeometry(std::string_view identity, const Board::Geometry& geometry) {
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
  if (!object.contains("\"floating\":true") && !object.contains("\"floating\": true")) {
    const auto floating = invokeDispatcher("hl.dsp.window.float({action=\"enable\",window=\"" + address + "\"})");
    if (!floating.starts_with("ok"))
      return false;
  }
  const auto resize =
      invokeDispatcher("hl.dsp.window.resize({x=" + std::to_string(geometry.width)
                       + ",y=" + std::to_string(geometry.height) + ",relative=false,window=\"" + address + "\"})");
  if (!resize.starts_with("ok")) {
    debugLog("grid resize failed identity=" + std::string{identity} + " response=" + resize);
    return false;
  }
  const auto move = invokeDispatcher("hl.dsp.window.move({x=" + std::to_string(geometry.x) + ",y="
                                     + std::to_string(geometry.y) + ",relative=false,window=\"" + address + "\"})");
  if (!move.starts_with("ok")) {
    debugLog("grid move failed identity=" + std::string{identity} + " response=" + move);
    return false;
  }
  return true;
}

bool dispatchGeometry(std::string_view identity, const Board::Placement& placement) {
  return dispatchScreenGeometry(identity, placement.world);
}

bool applyCamera(Board& board, Vector2D pan, float zoom) {
  board.zoom = boundedZoom(zoom);
  board.pan = pan;
  board.pan = boundedPan(board, board.geometry);
  return true;
}

bool applyPan(Board& board, Vector2D pan) {
  return applyCamera(board, pan, board.zoom);
}

void requestCamera(Board& board, Vector2D targetPan, float targetZoom) {
  const auto currentPan = board.pan;
  const auto currentZoom = board.zoom;
  board.pan = targetPan;
  board.zoom = boundedZoom(targetZoom);
  targetPan = boundedPan(board, board.geometry);
  board.pan = currentPan;
  board.zoom = currentZoom;
  board.targetPan = targetPan;
  board.targetZoom = boundedZoom(targetZoom);
  board.animationStartPan = currentPan;
  board.animationStartZoom = currentZoom;
  board.cameraAnimationStarted = std::chrono::steady_clock::now();
  board.cameraAnimating = true;
}

void updateCamera(Board& board) {
  if (!board.cameraAnimating)
    return;
  constexpr auto duration = std::chrono::milliseconds{220};
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()
                                                                             - board.cameraAnimationStarted);
  const auto linear = std::clamp(elapsed.count() / static_cast<float>(duration.count()), 0.0F, 1.0F);
  const auto eased = linear * linear * (3.0F - 2.0F * linear);
  applyCamera(board,
              board.animationStartPan + (board.targetPan - board.animationStartPan) * eased,
              board.animationStartZoom + (board.targetZoom - board.animationStartZoom) * eased);
  if (linear >= 1.0F)
    board.cameraAnimating = false;
  else if (g_pHyprRenderer != nullptr)
    g_pHyprRenderer->damageBox(board.geometry.x, board.geometry.y, board.geometry.width, board.geometry.height);
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
  const auto requestedLayer = luaL_optstring(state, 4, board->get().grid.openingLayer.c_str());
  if (std::string_view{requestedLayer} != "grid" && std::string_view{requestedLayer} != "floating")
    return activationFailure(state, "client layer must be grid or floating");
  auto& record = board->get().clients[identity];
  record.layer = requestedLayer;
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
  auto& grid = board->get().grid;
  const auto previousGrid = grid;
  const auto previousPlacements = board->get().clients;
  grid = {.rows = static_cast<int>(rows),
          .columns = static_cast<int>(columns),
          .gap = static_cast<int>(gap),
          .margin = static_cast<int>(margin),
          .openingLayer = layer};
  recomputeCanvasBounds(board->get());
  for (auto& [identity, placement] : board->get().clients)
    if (placement.layer == "grid") {
      setGeometry(board->get(), placement, std::string_view{monitorValue, monitorLength});
      if (!dispatchGeometry(identity, placement)) {
        grid = previousGrid;
        board->get().clients = previousPlacements;
        for (const auto& [previousIdentity, previousPlacement] : previousPlacements)
          dispatchGeometry(previousIdentity, previousPlacement);
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
      || identityLength == 0 || !rowOk || !columnOk || !rowSpanOk || !columnSpanOk || rowSpan <= 0 || columnSpan <= 0)
    return placementFailure(state, "invalid grid placement");
  const std::string monitor{monitorValue, monitorLength};
  const std::string identity{identityValue, identityLength};
  const auto board = findBoard(monitor, static_cast<int>(workspace));
  if (!board)
    return placementFailure(state, "board is not active");
  auto found = board->get().clients.find(identity);
  if (found == board->get().clients.end())
    return placementFailure(state, "client identity was not registered");
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
  recomputeCanvasBounds(board->get());
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
    recomputeCanvasBounds(board->get());
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
                   .world = {.x = static_cast<int>(x),
                             .y = static_cast<int>(y),
                             .width = static_cast<int>(width),
                             .height = static_cast<int>(height)}};
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
  if (zoom < kMinimumZoom || zoom > 1.0F)
    return activationFailure(state, "zoom must be between 0.25 and 1.0");
  requestCamera(board->get(), board->get().pan, zoom);
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
  if (zoom < kMinimumZoom || zoom > 1.0F)
    return activationFailure(state, "zoom must be between 0.25 and 1.0");
  requestCamera(board->get(), Vector2D{panX, panY}, zoom);
  debugLog("camera monitor=" + std::string{monitorValue, monitorLength} + " workspace=" + std::to_string(workspace)
           + " zoom=" + std::to_string(board->get().zoom) + " pan=" + std::to_string(board->get().pan.x) + ","
           + std::to_string(board->get().pan.y));
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
  const auto effectiveZoom = board && board->get().cameraAnimating ? board->get().targetZoom
                             : board                               ? board->get().zoom
                                                                   : 1.0F;
  lua_pushboolean(state, effectiveZoom < kManagementThreshold);
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
  debugLog("plugin init handle=" + std::to_string(reinterpret_cast<uintptr_t>(handle)));
  if (!supportedHost()) {
    pluginHandle = nullptr;
    return {};
  }

  workspaceRenderHook = installHook(kRenderClass, kRenderMethod);
  inputHook = installHook(kInputClass, kInputMethod);
  windowVisibilityHook = installHook(kWindowClass, kWindowVisibilityMethod);
  if (workspaceRenderHook == nullptr || inputHook == nullptr || windowVisibilityHook == nullptr) {
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
      || !HyprlandAPI::addLuaFunction(pluginHandle, "whiteboard", "management", managementLua)) {
    report("failed to register workspace model API");
    PLUGIN_EXIT();
    return {};
  }
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
  for (auto* hook : hooks)
    HyprlandAPI::removeFunctionHook(pluginHandle, hook);
  hooks.clear();
  workspaceRenderHook = nullptr;
  inputHook = nullptr;
  windowVisibilityHook = nullptr;
  canvasCaptureWorkspace.reset();
  canvasCaptureMonitor.reset();
  lifecycleListeners.clear();
  activeBoards.clear();
  pluginHandle = nullptr;
}
