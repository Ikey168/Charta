#pragma once

#include "core/Benchmark.h"
#include "core/DebugConsole.h"
#include "core/InputMap.h"
#include "core/LogSystem.h"
#include "core/PerfStats.h"
#include "core/Picking.h"
#include "core/Settings.h"
#include "core/ecs/ECS.h"
#include "core/ecs/components/Components.h"
#include "ui/EcsInspector.h"
#include "ui/EntityInspector.h"
#include "game/VerticalSlice.h"
#include "ui/HudFramework.h"
#include "ui/MenuSystem.h"
#include "ui/SceneHierarchy.h"
#include "ui/UiScaling.h"

#include <string>
#include <vector>

struct GLFWwindow;

/**
 * @file DebugUI.h
 * @brief Dear ImGui integration: the foundation for in-engine editor/debug UI.
 *
 * Milestone 9 (issue #144). Wraps ImGui setup, per-frame rendering, and teardown
 * behind a small interface so the engine's main loop only needs a few lines.
 * Docking is enabled (ImGuiConfigFlags_DockingEnable) so the panels added by the
 * follow-on issues (#53-#63: FPS overlay, console, entity inspector, scene
 * hierarchy, input remapping, HUD) can be docked into a layout.
 *
 * The overlay is toggleable and starts hidden; when hidden, render() returns
 * immediately so there is no per-frame ImGui cost.
 */
namespace IKore {

class DebugUI {
public:
    /**
     * @brief Initialize ImGui and its GLFW + OpenGL3 backends.
     * @param window       The GLFW window owning the current GL context.
     * @param glslVersion  GLSL #version string for the GL3 backend (e.g. "#version 330").
     * @return true on success.
     *
     * Call after the GL context is current and the loader is initialized.
     */
    bool initialize(GLFWwindow* window, const char* glslVersion);

    /// Tear down the ImGui backends and context. Safe to call if not initialized.
    void shutdown();

    /**
     * @brief Build and draw the debug UI for this frame.
     * @param deltaTimeSeconds Frame delta, shown in the stats panel.
     *
     * No-op (returns immediately) when the overlay is hidden or uninitialized,
     * so a hidden overlay costs nothing.
     */
    void render(float deltaTimeSeconds);

    /**
     * @brief Record this frame's timing into the perf stats (issue #53).
     *
     * Called every frame regardless of visibility - a cheap deque push - so the
     * FPS/frame-time graph has history the moment the overlay is opened, while a
     * hidden overlay still costs nothing to draw.
     */
    void update(float deltaTimeSeconds);

    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }
    void toggle() { m_visible = !m_visible; }

    /// Access the debug console so engine systems can register commands.
    DebugConsole& console() { return m_console; }

    /// Access the in-game HUD so engine systems can add anchored, data-bound
    /// widgets (issue #55). Menu screens (#58) reuse the same framework.
    Hud& hud() { return m_hud; }

    /// Access the entity inspector (issue #56) so viewport picking (#57) or the
    /// scene hierarchy (#59) can drive selection via inspector().select(id).
    EntityInspector& inspector() { return m_inspector; }

    /// Handle an ImGui InputText callback for the console (history/completion).
    /// Takes a void* (an ImGuiInputTextCallbackData*) to keep this header free of
    /// the ImGui headers; the implementation casts it. Public so the file-static
    /// callback trampoline in DebugUI.cpp can dispatch to it.
    int onConsoleTextEdit(void* callbackData);

private:
    void buildUI(float deltaTimeSeconds);
    void renderPerfOverlay();
    void renderConsole();
    void renderHud();
    void renderInspector();
    void renderPicking();
    void renderMenus();
    void renderHierarchy();
    void renderInputBindings();
    void renderScaling();
    void applyScaling();
    void renderBenchmark();
    void renderLog();
    void renderVerticalSlice();
    static game::VerticalSlice::Config sliceConfig();

    bool m_initialized{false};
    bool m_visible{false};
    bool m_showDemoWindow{false};
    bool m_showPerf{true};
    bool m_showConsole{true};
    bool m_showHud{true};
    bool m_showInspector{true};
    bool m_showPicking{true};
    bool m_showMenus{true};
    bool m_showHierarchy{true};
    bool m_showInput{true};
    bool m_showScaling{true};
    bool m_showBenchmark{true};
    bool m_showLog{true};
    bool m_showSlice{true};
    PerfStats m_perf;
    DebugConsole m_console;
    Hud m_hud;
    char m_consoleInput[256]{};

    // Demo state bound into the HUD so the overlay visibly updates from "live"
    // values, standing in for real ECS/game state. Animated in update().
    float m_demoHealth{1.0f};
    int m_demoAmmo{30};
    int m_demoScore{0};
    std::vector<std::string> m_demoInventory;
    float m_hudClock{0.0f};

    // Demo ECS scene the entity inspector (#56) reflects. In the full engine the
    // inspector points at the real registry; here it owns a small one so the panel
    // is self-contained. m_demoRegistry must outlive m_inspector's builder.
    ecs::Registry m_demoRegistry;
    std::vector<ecs::Entity> m_demoEntities;
    EntityInspector m_inspector;

    // Viewport picking (#57): a top-down pick view over the demo scene that feeds
    // the inspector on click. The core math/state lives in core/Picking.h.
    pick::Picker m_picker;

    // Interactive menus + persisted settings (#58). The menus are built on the
    // navigation core and drawn centered via the HUD framework (#55); settings
    // persist to disk. Menus are owned here; the stack holds pointers to them.
    Settings m_settings;
    Menu m_mainMenu{"Main Menu"};
    Menu m_pauseMenu{"Paused"};
    Menu m_settingsMenu{"Settings"};
    MenuStack m_menuStack;
    bool m_menuQuitRequested{false};
    void saveSettings();

    // Scene hierarchy (#59): a tree over the demo entities, kept in sync as they are
    // created/destroyed; selecting a node drives the inspector (#56).
    SceneHierarchy m_hierarchy;
    char m_renameBuffer[64]{};
    SceneHierarchy::NodeId m_renameFor{SceneHierarchy::kNoNode};
    int m_nextEntityLabel{1};

    // Input remapping (#60): rebindable actions with conflict detection, persisted
    // to disk. m_rebinding is the index of the action currently capturing input, or
    // -1 when not listening.
    InputMap m_inputMap;
    int m_rebinding{-1};

    // DPI / resolution UI scaling (#61). applyScaling() computes the effective
    // scale each frame from the framebuffer size and DPI and applies it cheaply to
    // ImGui (font + style) and the HUD; the user scale persists in settings.
    UiScaleConfig m_uiScale;
    float m_effectiveScale{1.0f};

    // Benchmark capture (#62): records FPS/frame-time/memory over a run (fed each
    // frame from update() while capturing) and exports via the #155 format.
    Benchmark m_benchmark;
    bool m_benchmarkJson{false};
    std::string m_benchmarkStatus;

    // Logging + in-engine viewer (#63): structured logs feed the console (#54) and a
    // file sink; the viewer filters by level/category and tails in real time.
    LogSystem m_log;
    int m_logFilterLevel{0};   // index into LogViewerLevel for the viewer's min level
    int m_logCategoryIndex{0}; // 0 = all categories
    bool m_logAutoScroll{true};
    float m_logClock{0.0f};    // paces the demo heartbeat log

    // Vertical-slice panel (#225): drives the headless game::VerticalSlice scenario
    // (import -> crowd -> rewind) with play/pause/step and a rewind scrubber.
    game::VerticalSlice m_slice{sliceConfig()};
    bool m_slicePlaying{false};
    bool m_sliceScrubbing{false};
    int m_sliceRewindTick{0};
};

} // namespace IKore
