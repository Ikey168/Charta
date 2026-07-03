#include "ui/DebugUI.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace IKore {

// Unscaled base style, captured once at init. Each frame applyScaling() copies it
// back and re-applies ScaleAllSizes(scale), so scaling is not cumulative (#61).
static ImGuiStyle s_baseStyle;
static bool s_baseStyleCaptured = false;

// A tiny OSM-style neighborhood the vertical-slice panel (#225) imports.
static const char* kSliceGeoJson = R"JSON({
  "type": "FeatureCollection",
  "features": [
    { "type":"Feature", "properties":{"highway":"residential"},
      "geometry":{"type":"LineString","coordinates":[[0,0],[0.0001,0],[0.0002,0]]} },
    { "type":"Feature", "properties":{"highway":"residential"},
      "geometry":{"type":"LineString","coordinates":[[0.0002,0],[0.0002,0.0001]]} },
    { "type":"Feature", "properties":{"building":"yes","height":"10"},
      "geometry":{"type":"Polygon","coordinates":[[[0,0],[0.0001,0],[0.0001,0.0001],[0,0.0001],[0,0]]]} },
    { "type":"Feature", "properties":{"building":"yes","building:levels":"3"},
      "geometry":{"type":"Polygon","coordinates":[[[0.0003,0],[0.0004,0],[0.0004,0.0001],[0.0003,0.0001],[0.0003,0]]]} },
    { "type":"Feature", "properties":{"leisure":"park"},
      "geometry":{"type":"Polygon","coordinates":[[[0,0.0002],[0.0002,0.0002],[0.0002,0.0003],[0,0.0003],[0,0.0002]]]} }
  ]
})JSON";

// Pick a walkable cell in the far corner of the grid as the crowd's goal.
static ecs::Vec3 sliceFarWalkableCell(const world::NavGrid& grid) {
    ecs::Vec3 best{};
    float bestScore = -1.0e30f;
    for (int cz = 0; cz < grid.height; ++cz) {
        for (int cx = 0; cx < grid.width; ++cx) {
            if (!grid.isWalkable(cx, cz)) continue;
            const ecs::Vec3 c = grid.cellCenter(cx, cz);
            if (c.x + c.z > bestScore) {
                bestScore = c.x + c.z;
                best = c;
            }
        }
    }
    return best;
}

// File-static trampoline: ImGui's InputText callback dispatches here, then into
// the DebugUI instance passed as user data.
static int consoleTextEditStub(ImGuiInputTextCallbackData* data) {
    return static_cast<DebugUI*>(data->UserData)->onConsoleTextEdit(data);
}

// Draw one reflected property with the widget matching its type; an edit writes
// straight back through the setter, so it applies live to the component (#56).
static void drawProperty(const Property& prop) {
    const char* label = prop.name.c_str();
    switch (prop.type) {
        case PropertyType::Bool: {
            bool v = prop.getBool();
            if (ImGui::Checkbox(label, &v)) prop.setBool(v);
            break;
        }
        case PropertyType::Int: {
            int v = prop.getInt();
            if (ImGui::DragInt(label, &v, prop.speed, static_cast<int>(prop.minValue),
                               static_cast<int>(prop.maxValue)))
                prop.setInt(v);
            break;
        }
        case PropertyType::Float: {
            float v = prop.getFloat();
            if (ImGui::DragFloat(label, &v, prop.speed, prop.minValue, prop.maxValue)) prop.setFloat(v);
            break;
        }
        case PropertyType::Float3: {
            const Vec3f v = prop.getVec3();
            float a[3] = {v.x, v.y, v.z};
            if (ImGui::DragFloat3(label, a, prop.speed)) prop.setVec3(Vec3f{a[0], a[1], a[2]});
            break;
        }
        case PropertyType::String: {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s", prop.getString().c_str());
            if (ImGui::InputText(label, buf, sizeof(buf))) prop.setString(std::string(buf));
            break;
        }
    }
}

// Draw one menu row with the widget matching its type. Mouse hover moves the menu
// focus and mouse edits route through the same setters keyboard/gamepad use (#58).
static void drawMenuItem(Menu& menu, int index, const MenuItem& item, bool focused) {
    ImGui::PushID(index);
    if (focused) {
        ImGui::Bullet(); // focus marker
        ImGui::SameLine();
    }

    switch (item.type) {
        case MenuItemType::Label:
            ImGui::TextDisabled("%s", item.label.c_str());
            break;
        case MenuItemType::Button:
            if (ImGui::Selectable(item.label.c_str(), focused)) {
                menu.setFocus(index);
                menu.activate();
            }
            if (ImGui::IsItemHovered()) menu.setFocus(index);
            break;
        case MenuItemType::Toggle: {
            bool v = item.getBool ? item.getBool() : false;
            if (ImGui::Checkbox(item.label.c_str(), &v)) {
                menu.setFocus(index);
                if (item.setBool) item.setBool(v);
            }
            if (ImGui::IsItemHovered()) menu.setFocus(index);
            break;
        }
        case MenuItemType::Slider: {
            float v = item.getFloat ? item.getFloat() : 0.0f;
            if (ImGui::SliderFloat(item.label.c_str(), &v, item.minValue, item.maxValue)) {
                menu.setFocus(index);
                if (item.setFloat) item.setFloat(v);
            }
            if (ImGui::IsItemHovered()) menu.setFocus(index);
            break;
        }
        case MenuItemType::Choice: {
            const int cur = item.getChoice ? item.getChoice() : 0;
            const char* preview =
                (cur >= 0 && cur < static_cast<int>(item.options.size())) ? item.options[cur].c_str() : "";
            if (ImGui::BeginCombo(item.label.c_str(), preview)) {
                for (int k = 0; k < static_cast<int>(item.options.size()); ++k) {
                    if (ImGui::Selectable(item.options[k].c_str(), k == cur)) {
                        menu.setFocus(index);
                        if (item.setChoice) item.setChoice(k);
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) menu.setFocus(index);
            break;
        }
    }
    ImGui::PopID();
}

// Gamepad buttons we let the user bind (an explicit list avoids depending on the
// exact ImGuiKey gamepad range bounds).
static const ImGuiKey kBindableGamepad[] = {
    ImGuiKey_GamepadFaceDown,  ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadFaceLeft,
    ImGuiKey_GamepadFaceUp,    ImGuiKey_GamepadDpadUp,    ImGuiKey_GamepadDpadDown,
    ImGuiKey_GamepadDpadLeft,  ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadL1,
    ImGuiKey_GamepadR1,        ImGuiKey_GamepadStart,     ImGuiKey_GamepadBack,
};

static bool isBindableGamepad(ImGuiKey key) {
    for (ImGuiKey g : kBindableGamepad) {
        if (g == key) return true;
    }
    return false;
}

// Poll for the next pressed input this frame, to capture a rebind. Mouse buttons
// and gamepad buttons are checked explicitly; everything else is treated as a key.
static InputMapBinding captureInput() {
    for (int b = 0; b < 5; ++b) {
        if (ImGui::IsMouseClicked(b)) return InputMapBinding{InputDevice::Mouse, b};
    }
    for (ImGuiKey g : kBindableGamepad) {
        if (ImGui::IsKeyPressed(g, false)) return InputMapBinding{InputDevice::Gamepad, static_cast<int>(g)};
    }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const ImGuiKey key = static_cast<ImGuiKey>(k);
        if (!ImGui::IsKeyPressed(key, false)) continue;
        if (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY) continue; // handled above
        if (isBindableGamepad(key)) continue;                                   // handled above
        return InputMapBinding{InputDevice::Keyboard, static_cast<int>(key)};
    }
    return InputMapBinding{};
}

// Human-readable label for a binding, for the panel.
static std::string bindingLabel(const InputMapBinding& binding) {
    if (!binding.bound()) return "(unbound)";
    if (binding.device == InputDevice::Mouse) {
        switch (binding.code) {
            case 0: return "Mouse Left";
            case 1: return "Mouse Right";
            case 2: return "Mouse Middle";
            default: return "Mouse " + std::to_string(binding.code);
        }
    }
    const char* name = ImGui::GetKeyName(static_cast<ImGuiKey>(binding.code)); // keys + gamepad
    return (name != nullptr && name[0] != '\0') ? std::string(name) : std::string("code ") +
                                                                          std::to_string(binding.code);
}

// Whether the input a binding refers to is currently held (shows rebinds taking
// effect immediately).
static bool bindingActive(const InputMapBinding& binding) {
    if (!binding.bound()) return false;
    if (binding.device == InputDevice::Mouse) return ImGui::IsMouseDown(binding.code);
    return ImGui::IsKeyDown(static_cast<ImGuiKey>(binding.code));
}

game::VerticalSlice::Config DebugUI::sliceConfig() {
    game::VerticalSlice::Config config;
    config.cellSize = 2.0f;
    config.historyCapacity = 1800; // a generous rewind window for the scrubber
    return config;
}

bool DebugUI::initialize(GLFWwindow* window, const char* glslVersion) {
    if (m_initialized) return true;
    if (window == nullptr) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // menus (#58) support gamepad
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // allow panels to be docked together

    ImGui::StyleColorsDark();
    s_baseStyle = ImGui::GetStyle(); // remember the unscaled style for #61
    s_baseStyleCaptured = true;

    if (!ImGui_ImplGlfw_InitForOpenGL(window, /*install_callbacks=*/true)) {
        LOG_ERROR("DebugUI: ImGui GLFW backend init failed");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init(glslVersion)) {
        LOG_ERROR("DebugUI: ImGui OpenGL3 backend init failed");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    m_console.registerCommand("version", "print the engine name", [](const std::vector<std::string>&) {
        return std::string("IKore Engine");
    });
    m_console.print("Console ready. Type 'help'.");

    // Demo HUD wired through the reusable framework (#55). Each widget is anchored
    // and bound to a live getter; in a real game these would read ECS component
    // values. update() animates the demo state so the readouts visibly change.
    m_demoInventory = {"Sword", "Shield", "Potion x3", "Map"};
    m_hud.add(hudBar("Health", HudAnchor::TopLeft, {16.0f, 16.0f}, {220.0f, 20.0f},
                     [this] { return m_demoHealth; }));
    m_hud.add(hudText("Score", HudAnchor::TopRight, {16.0f, 16.0f}, {200.0f, 24.0f},
                      [this] { return std::string("Score: ") + std::to_string(m_demoScore); }));
    m_hud.add(hudValue("Ammo", HudAnchor::BottomRight, {16.0f, 16.0f}, {140.0f, 24.0f},
                       [this] { return m_demoAmmo; }));
    m_hud.add(hudList("Inventory", HudAnchor::BottomLeft, {16.0f, 16.0f}, {180.0f, 130.0f},
                      [this] { return m_demoInventory; }));

    // Demo ECS scene for the entity inspector (#56). A few archetype-ECS entities
    // with different component sets; the inspector reflects and live-edits them.
    {
        ecs::Entity player = m_demoRegistry.create();
        m_demoRegistry.add<ecs::Transform>(player,
                                           ecs::Transform{{0.0f, 1.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});
        m_demoRegistry.add<ecs::Velocity>(player, ecs::Velocity{{1.0f, 0.0f, 0.0f}, {}});
        m_demoRegistry.add<ecs::RigidBody>(player,
                                           ecs::RigidBody{{0.0f, -9.8f, 0.0f}, 1.0f, 0.05f, false});
        m_demoEntities.push_back(player);

        ecs::Entity enemy = m_demoRegistry.create();
        m_demoRegistry.add<ecs::Transform>(enemy,
                                           ecs::Transform{{5.0f, 0.0f, 2.0f}, {}, {1.0f, 1.0f, 1.0f}});
        m_demoRegistry.add<ecs::AIAgent>(enemy, ecs::AIAgent{{0.0f, 0.0f, 0.0f}, 2.0f, 0.1f, true});
        m_demoEntities.push_back(enemy);

        ecs::Entity prop = m_demoRegistry.create();
        m_demoRegistry.add<ecs::Transform>(prop,
                                           ecs::Transform{{-3.0f, 0.0f, -1.0f}, {}, {2.0f, 2.0f, 2.0f}});
        m_demoEntities.push_back(prop);
    }
    installEcsBuilder(m_inspector, m_demoRegistry);
    if (!m_demoEntities.empty()) m_inspector.select(packEntity(m_demoEntities.front()));

    // Scene hierarchy (#59) mirroring the demo entities; Prop nests under Player to
    // show a parent/child relationship. Selecting a node drives the inspector.
    if (m_demoEntities.size() >= 3) {
        m_hierarchy.add(packEntity(m_demoEntities[0]), "Player");
        m_hierarchy.add(packEntity(m_demoEntities[1]), "Enemy");
        m_hierarchy.add(packEntity(m_demoEntities[2]), "Prop", packEntity(m_demoEntities[0]));
    }

    // Interactive menus + persisted settings (#58). Defaults first, then load any
    // saved values from disk (so a previous session's choices win), then build the
    // menus on the navigation core and open the main menu.
    m_settings.setBool("vsync", true);
    m_settings.setFloat("volume", 0.8f);
    m_settings.setInt("quality", 2);       // index into {Low, Medium, High}
    m_settings.setFloat("ui.scale", 1.0f); // user-facing UI scale (#61)
    m_settings.loadFromFile("ikore_settings.ini");
    m_uiScale.userScale = m_settings.getFloat("ui.scale", 1.0f); // persisted scale

    m_settingsMenu.add(menuToggle("VSync", [this] { return m_settings.getBool("vsync"); },
                                  [this](bool b) { m_settings.setBool("vsync", b); }));
    m_settingsMenu.add(menuSlider("Master Volume", [this] { return m_settings.getFloat("volume"); },
                                  [this](float v) { m_settings.setFloat("volume", v); }, 0.0f, 1.0f, 0.05f));
    m_settingsMenu.add(menuChoice("Quality", {"Low", "Medium", "High"},
                                  [this] { return m_settings.getInt("quality"); },
                                  [this](int i) { m_settings.setInt("quality", i); }));
    m_settingsMenu.add(menuButton("Back", [this] {
        saveSettings();
        m_menuStack.back();
    }));

    m_pauseMenu.add(menuButton("Resume", [this] { m_menuStack.closeAll(); }));
    m_pauseMenu.add(menuButton("Settings", [this] { m_menuStack.open(&m_settingsMenu); }));
    m_pauseMenu.add(menuButton("Main Menu", [this] {
        m_menuStack.closeAll();
        m_menuStack.open(&m_mainMenu);
    }));

    m_mainMenu.add(menuButton("Play", [this] { m_menuStack.closeAll(); }));
    m_mainMenu.add(menuButton("Settings", [this] { m_menuStack.open(&m_settingsMenu); }));
    m_mainMenu.add(menuButton("Quit", [this] { m_menuQuitRequested = true; }));

    m_menuStack.open(&m_mainMenu);

    // Input remapping (#60): register the default action bindings (ImGui key /
    // mouse-button codes), then load any rebindings saved from a prior session.
    m_inputMap.addAction("MoveForward", {InputDevice::Keyboard, static_cast<int>(ImGuiKey_W)});
    m_inputMap.addAction("MoveBack", {InputDevice::Keyboard, static_cast<int>(ImGuiKey_S)});
    m_inputMap.addAction("MoveLeft", {InputDevice::Keyboard, static_cast<int>(ImGuiKey_A)});
    m_inputMap.addAction("MoveRight", {InputDevice::Keyboard, static_cast<int>(ImGuiKey_D)});
    m_inputMap.addAction("Jump", {InputDevice::Keyboard, static_cast<int>(ImGuiKey_Space)});
    m_inputMap.addAction("Interact", {InputDevice::Keyboard, static_cast<int>(ImGuiKey_E)});
    m_inputMap.addAction("Fire", {InputDevice::Mouse, 0});
    m_inputMap.addAction("AltFire", {InputDevice::Mouse, 1});
    m_inputMap.loadFromFile("ikore_input.ini");

    // Logging + in-engine viewer (#63): mirror every log line into the debug
    // console (#54), open a readable file sink, and seed a few example records.
    m_log.addSink([this](const LogRecord& record) { m_console.print(record.format()); });
    m_log.openFile("ikore_log.txt");
    m_log.info("engine", "Log system online");
    m_log.debug("physics", "Broadphase pairs: 128");
    m_log.warning("render", "Shader cache miss for 'pbr'");
    m_log.error("audio", "Failed to open device (using fallback)");

    // Vertical-slice panel (#225): import the demo neighborhood, pick a far goal,
    // and spawn a crowd. The scenario is driven by the panel (play/step/rewind).
    if (m_slice.loadFromGeoJson(kSliceGeoJson)) {
        m_slice.setGoal(sliceFarWalkableCell(m_slice.grid()));
        m_slice.spawnCrowd(120);
        m_log.info("world", "Vertical slice ready: " + std::to_string(m_slice.agentCount()) + " agents");
    }

    m_initialized = true;
    LOG_INFO("DebugUI: Dear ImGui initialized (press F1 to toggle)");
    return true;
}

void DebugUI::shutdown() {
    if (!m_initialized) return;
    m_log.closeFile(); // flush any buffered log lines to disk (#63)
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void DebugUI::update(float deltaTimeSeconds) {
    // Cheap frame-time recording; runs even while the overlay is hidden so the
    // graph is populated when it is opened.
    m_perf.record(deltaTimeSeconds);

    // Benchmark capture (#62) runs regardless of panel visibility. When not
    // capturing this is a single branch, so it costs nothing when idle/disabled.
    if (m_benchmark.capturing()) {
        m_benchmark.record(static_cast<double>(deltaTimeSeconds), PerfStats::residentBytes());
    }

    // Demo heartbeat log (#63) every ~2s, to show the viewer's real-time tail and
    // the console feed updating live.
    m_logClock += deltaTimeSeconds;
    if (m_logClock >= 2.0f) {
        m_logClock = 0.0f;
        m_log.info("engine", "Heartbeat at frame " + std::to_string(m_perf.frameCount()));
    }

    // Animate the demo HUD state so the data-bound widgets visibly update. In a
    // real game these values come from ECS/game systems instead.
    m_hudClock += deltaTimeSeconds;
    m_demoHealth = 0.5f + 0.5f * std::sin(m_hudClock);       // oscillate across 0..1
    m_demoScore = static_cast<int>(m_hudClock * 100.0f);     // climbs over time
    m_demoAmmo = 30 - static_cast<int>(m_hudClock) % 31;     // counts down 30..0, loops

    // Vertical-slice panel (#225): advance the scenario in real time while playing,
    // and stop automatically once the whole crowd has arrived.
    if (m_slicePlaying) {
        m_slice.advance(static_cast<double>(deltaTimeSeconds));
        if (m_slice.tick() > 0 && m_slice.agentsMoving() == 0) m_slicePlaying = false;
    }
}

void DebugUI::render(float deltaTimeSeconds) {
    // Hidden or uninitialized overlay costs nothing.
    if (!m_initialized || !m_visible) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    applyScaling(); // DPI / resolution aware scaling (#61), before any widgets
    buildUI(deltaTimeSeconds);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void DebugUI::buildUI(float deltaTimeSeconds) {
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::Begin("IKore Debug");
    ImGui::TextUnformatted("IKore Engine - in-engine debug UI");
    ImGui::Separator();

    ImGui::Text("FPS: %.1f  (%.3f ms/frame)", io.Framerate,
                io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
    ImGui::Text("Frame delta: %.3f ms", deltaTimeSeconds * 1000.0f);

    ImGui::Separator();
    ImGui::TextUnformatted("Planned panels (Milestone 9):");
    ImGui::BulletText("FPS / performance overlay (#53)");
    ImGui::BulletText("Debug console (#54)");
    ImGui::BulletText("HUD / menus (#55, #58)");
    ImGui::BulletText("Entity inspector (#56)");
    ImGui::BulletText("Scene hierarchy viewer (#59)");
    ImGui::BulletText("Input remapping (#60)");
    ImGui::TextDisabled("Docking is enabled: drag a window onto another to dock it.");

    ImGui::Separator();
    ImGui::Checkbox("Show performance overlay (#53)", &m_showPerf);
    ImGui::Checkbox("Show console (#54)", &m_showConsole);
    ImGui::Checkbox("Show HUD (#55)", &m_showHud);
    ImGui::Checkbox("Show entity inspector (#56)", &m_showInspector);
    ImGui::Checkbox("Show scene view / picking (#57)", &m_showPicking);
    ImGui::Checkbox("Show menus (#58)", &m_showMenus);
    ImGui::Checkbox("Show scene hierarchy (#59)", &m_showHierarchy);
    ImGui::Checkbox("Show input bindings (#60)", &m_showInput);
    ImGui::Checkbox("Show UI scaling (#61)", &m_showScaling);
    ImGui::Checkbox("Show benchmark (#62)", &m_showBenchmark);
    ImGui::Checkbox("Show log viewer (#63)", &m_showLog);
    ImGui::Checkbox("Show vertical slice (#225)", &m_showSlice);
    ImGui::Checkbox("Show ImGui demo window", &m_showDemoWindow);
    ImGui::End();

    renderPerfOverlay();
    renderConsole();
    renderHud();
    renderInspector();
    renderPicking();
    renderMenus();
    renderHierarchy();
    renderInputBindings();
    renderScaling();
    renderBenchmark();
    renderLog();
    renderVerticalSlice();

    if (m_showDemoWindow) {
        ImGui::ShowDemoWindow(&m_showDemoWindow);
    }
}

void DebugUI::renderConsole() {
    if (!m_showConsole) return;

    ImGui::Begin("Console", &m_showConsole);

    // Scrollback region, leaving room for the input line at the bottom.
    const float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("console_scroll", ImVec2(0.0f, -footerHeight), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (const std::string& line : m_console.lines()) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f); // autoscroll
    ImGui::EndChild();

    ImGui::Separator();
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                      ImGuiInputTextFlags_CallbackCompletion |
                                      ImGuiInputTextFlags_CallbackHistory;
    if (ImGui::InputText("Input", m_consoleInput, sizeof(m_consoleInput), flags, &consoleTextEditStub,
                         this)) {
        m_console.execute(m_consoleInput);
        m_consoleInput[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1); // keep focus on the input
    }
    ImGui::End();
}

void DebugUI::renderInspector() {
    if (!m_showInspector) return;

    ImGui::Begin("Entity Inspector", &m_showInspector);

    // Entity picker. Selection will also be driven by viewport picking (#57) and
    // the scene hierarchy (#59) once those land, via inspector().select(id).
    ImGui::TextUnformatted("Entities");
    if (ImGui::BeginListBox("##entities", ImVec2(-1.0f, 90.0f))) {
        for (const ecs::Entity entity : m_demoEntities) {
            const EntityInspector::EntityId id = packEntity(entity);
            const bool selected = m_inspector.hasSelection() && m_inspector.selected() == id;
            char label[32];
            std::snprintf(label, sizeof(label), "Entity %u", entity.index);
            if (ImGui::Selectable(label, selected)) m_inspector.select(id);
        }
        ImGui::EndListBox();
    }

    ImGui::Separator();

    if (!m_inspector.hasSelection() || m_inspector.components().empty()) {
        ImGui::TextDisabled("No entity selected, or it has no components.");
        ImGui::End();
        return;
    }

    // Cached component views: iterated every frame with no rebuild/allocation.
    int componentIndex = 0;
    for (const ComponentView& view : m_inspector.components()) {
        ImGui::PushID(componentIndex++);
        if (ImGui::CollapsingHeader(view.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const Property& prop : view.properties) {
                drawProperty(prop);
            }
        }
        ImGui::PopID();
    }

    ImGui::End();
}

void DebugUI::renderPicking() {
    if (!m_showPicking) return;

    ImGui::Begin("Scene View (picking)", &m_showPicking);
    ImGui::TextUnformatted("Top-down pick view: hover to highlight, click to select.");
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("ImGui WantCaptureMouse: %s", io.WantCaptureMouse ? "yes" : "no");

    // Reserve the viewport rectangle. The InvisibleButton is what makes ImGui
    // consume clicks: only a click that lands on it (and not on another panel)
    // counts as a scene click - clicks elsewhere on the UI never pick (#57).
    const ImVec2 topLeft = ImGui::GetCursorScreenPos();
    const float viewW = ImGui::GetContentRegionAvail().x;
    const float viewH = 240.0f;
    ImGui::InvisibleButton("##viewport", ImVec2(viewW, viewH));
    const bool overViewport = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    // World window (x -> horizontal, z -> vertical) mapped onto the rectangle.
    const float worldMinX = -6.0f, worldMaxX = 6.0f, worldMinZ = -6.0f, worldMaxZ = 6.0f;
    auto worldToScreen = [&](float wx, float wz) {
        return ImVec2(topLeft.x + (wx - worldMinX) / (worldMaxX - worldMinX) * viewW,
                      topLeft.y + (wz - worldMinZ) / (worldMaxZ - worldMinZ) * viewH);
    };

    // Pickable bounds from the demo entities' transforms; ids match the inspector.
    std::vector<pick::Pickable> pickables;
    pickables.reserve(m_demoEntities.size());
    for (const ecs::Entity entity : m_demoEntities) {
        if (!m_demoRegistry.has<ecs::Transform>(entity)) continue;
        const ecs::Transform& t = m_demoRegistry.get<ecs::Transform>(entity);
        const pick::Vec3 half{0.5f * t.scale.x + 0.25f, 0.5f * t.scale.y + 0.25f, 0.5f * t.scale.z + 0.25f};
        pickables.push_back({packEntity(entity),
                             pick::Aabb::fromCenterHalf(
                                 pick::Vec3{t.position.x, t.position.y, t.position.z}, half)});
    }

    // Cursor -> top-down world ray (straight down onto the x/z plane).
    const float worldX = worldMinX + (io.MousePos.x - topLeft.x) / viewW * (worldMaxX - worldMinX);
    const float worldZ = worldMinZ + (io.MousePos.y - topLeft.y) / viewH * (worldMaxZ - worldMinZ);
    const pick::Ray ray{pick::Vec3{worldX, 100.0f, worldZ}, pick::Vec3{0.0f, -1.0f, 0.0f}};

    m_picker.update(ray, pickables, /*pointerOverUI=*/!overViewport, clicked);

    // A click in the viewport feeds the entity inspector (#56).
    if (clicked) {
        if (m_picker.hasSelection()) {
            m_inspector.select(m_picker.selected());
        } else {
            m_inspector.deselect();
        }
    }

    // Draw the scene: white dots, a yellow ring on hover, green fill when selected.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(topLeft, ImVec2(topLeft.x + viewW, topLeft.y + viewH), IM_COL32(28, 28, 32, 255));
    drawList->AddRect(topLeft, ImVec2(topLeft.x + viewW, topLeft.y + viewH), IM_COL32(80, 80, 90, 255));
    for (const pick::Pickable& p : pickables) {
        const float cx = 0.5f * (p.bounds.min.x + p.bounds.max.x);
        const float cz = 0.5f * (p.bounds.min.z + p.bounds.max.z);
        const ImVec2 dot = worldToScreen(cx, cz);
        ImU32 color = IM_COL32(220, 220, 220, 255);
        if (m_picker.hasSelection() && m_picker.selected() == p.id) color = IM_COL32(80, 220, 120, 255);
        drawList->AddCircleFilled(dot, 6.0f, color);
        if (m_picker.hasHover() && m_picker.hovered() == p.id) {
            drawList->AddCircle(dot, 10.0f, IM_COL32(240, 220, 80, 255), 0, 2.0f);
        }
    }

    if (m_picker.hasHover()) {
        ImGui::Text("Hover: Entity %u", unpackEntity(m_picker.hovered()).index);
    } else {
        ImGui::TextDisabled("Hover: none");
    }
    if (m_picker.hasSelection()) {
        ImGui::Text("Selected: Entity %u", unpackEntity(m_picker.selected()).index);
    } else {
        ImGui::TextDisabled("Selected: none");
    }

    ImGui::End();
}

void DebugUI::saveSettings() { m_settings.saveToFile("ikore_settings.ini"); }

void DebugUI::renderMenus() {
    if (!m_showMenus) return;

    // Map keyboard and gamepad input to menu actions. All input sources funnel
    // through the same MenuAction stream, so navigation is identical for each; the
    // mouse is handled per-item below (hover to focus, click to activate).
    if (m_menuStack.isOpen()) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp))
            m_menuStack.handle(MenuAction::Up);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown))
            m_menuStack.handle(MenuAction::Down);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft))
            m_menuStack.handle(MenuAction::Left);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight))
            m_menuStack.handle(MenuAction::Right);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown))
            m_menuStack.handle(MenuAction::Activate);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))
            m_menuStack.handle(MenuAction::Back);
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_menuStack.open(&m_pauseMenu); // Escape opens the pause menu when idle
    }

    Menu* menu = m_menuStack.current();
    if (menu == nullptr) return;

    // Center the menu window using the HUD framework's anchoring (#55), sizing it by
    // the effective UI scale (#61) so it stays proportionate across resolutions.
    const ImGuiIO& io = ImGui::GetIO();
    const HudVec2 size{340.0f * m_effectiveScale, 320.0f * m_effectiveScale};
    const HudVec2 pos =
        resolveAnchor(HudAnchor::Center, {io.DisplaySize.x, io.DisplaySize.y}, size, {0.0f, 0.0f});
    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(size.x, size.y), ImGuiCond_Always);

    ImGui::Begin(menu->title().c_str(), nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    const std::vector<MenuItem>& items = menu->items();
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        drawMenuItem(*menu, i, items[i], menu->focus() == i);
    }
    ImGui::End();

    if (m_menuQuitRequested) {
        // In the full engine this would signal the main loop to exit; here we just
        // note it and drop back so the demo stays interactive.
        LOG_INFO("DebugUI: menu Quit selected");
        m_menuQuitRequested = false;
        m_menuStack.closeAll();
    }
}

void DebugUI::renderHierarchy() {
    if (!m_showHierarchy) return;

    ImGui::Begin("Scene Hierarchy", &m_showHierarchy);

    // Create/Delete stay in sync with the demo registry, so the tree reflects the
    // live scene as entities are created and destroyed.
    if (ImGui::Button("Create")) {
        ecs::Entity created = m_demoRegistry.create();
        m_demoRegistry.add<ecs::Transform>(created, ecs::Transform{});
        m_demoEntities.push_back(created);
        const SceneHierarchy::NodeId id = packEntity(created);
        m_hierarchy.add(id, "Entity " + std::to_string(m_nextEntityLabel++));
        m_inspector.select(id);
    }
    ImGui::SameLine();
    const bool haveSelection = m_inspector.hasSelection() && m_hierarchy.exists(m_inspector.selected());
    if (ImGui::Button("Delete") && haveSelection) {
        const SceneHierarchy::NodeId id = m_inspector.selected();
        for (const SceneHierarchy::NodeId gone : m_hierarchy.subtree(id)) {
            const ecs::Entity entity = unpackEntity(gone);
            if (m_demoRegistry.isValid(entity)) m_demoRegistry.destroy(entity);
            m_demoEntities.erase(std::remove_if(m_demoEntities.begin(), m_demoEntities.end(),
                                                [entity](ecs::Entity x) { return x == entity; }),
                                 m_demoEntities.end());
        }
        m_hierarchy.remove(id, /*recursive=*/true);
        m_inspector.deselect();
    }

    // Rename and reparent the current selection (re-checked after any delete above).
    if (m_inspector.hasSelection() && m_hierarchy.exists(m_inspector.selected())) {
        const SceneHierarchy::NodeId sel = m_inspector.selected();
        if (sel != m_renameFor) {
            std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", m_hierarchy.name(sel).c_str());
            m_renameFor = sel;
        }
        if (ImGui::InputText("Name", m_renameBuffer, sizeof(m_renameBuffer),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_hierarchy.rename(sel, m_renameBuffer);
        }

        const SceneHierarchy::NodeId curParent = m_hierarchy.parent(sel);
        const char* preview =
            curParent == SceneHierarchy::kNoNode ? "(root)" : m_hierarchy.name(curParent).c_str();
        if (ImGui::BeginCombo("Parent", preview)) {
            if (ImGui::Selectable("(root)", curParent == SceneHierarchy::kNoNode)) {
                m_hierarchy.reparent(sel, SceneHierarchy::kNoNode);
            }
            for (const auto& entry : m_hierarchy.flatten()) {
                const SceneHierarchy::NodeId id = entry.first;
                if (id == sel || m_hierarchy.isAncestor(sel, id)) continue; // no self / descendants
                if (ImGui::Selectable(m_hierarchy.name(id).c_str(), id == curParent)) {
                    m_hierarchy.reparent(sel, id); // guarded against cycles anyway
                }
            }
            ImGui::EndCombo();
        }
    } else {
        m_renameFor = SceneHierarchy::kNoNode;
    }

    ImGui::Separator();

    // Flat, indented listing. flatten() returns a snapshot, so selecting mid-list
    // never invalidates the iteration.
    for (const auto& entry : m_hierarchy.flatten()) {
        const SceneHierarchy::NodeId id = entry.first;
        const int depth = entry.second;
        ImGui::PushID(static_cast<int>(id & 0xFFFFFFFFu));
        if (depth > 0) ImGui::Indent(static_cast<float>(depth) * 16.0f);
        const bool selected = m_inspector.hasSelection() && m_inspector.selected() == id;
        if (ImGui::Selectable(m_hierarchy.name(id).c_str(), selected)) m_inspector.select(id);
        if (depth > 0) ImGui::Unindent(static_cast<float>(depth) * 16.0f);
        ImGui::PopID();
    }

    ImGui::End();
}

void DebugUI::renderInputBindings() {
    if (!m_showInput) return;

    ImGui::Begin("Input Bindings", &m_showInput);

    // While capturing a rebind, watch for the next input (Esc cancels). Rebinding
    // updates the map in place, so it takes effect immediately.
    if (m_rebinding >= 0 && m_rebinding < static_cast<int>(m_inputMap.actionCount())) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                           "Press an input for '%s' (Esc to cancel)...",
                           m_inputMap.actionName(static_cast<std::size_t>(m_rebinding)).c_str());
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_rebinding = -1;
        } else {
            const InputMapBinding captured = captureInput();
            if (captured.bound()) {
                m_inputMap.rebind(m_inputMap.actionName(static_cast<std::size_t>(m_rebinding)), captured);
                m_rebinding = -1;
            }
        }
    }

    for (int i = 0; i < static_cast<int>(m_inputMap.actionCount()); ++i) {
        const std::string& name = m_inputMap.actionName(static_cast<std::size_t>(i));
        const InputMapBinding binding = m_inputMap.bindingAt(static_cast<std::size_t>(i));
        const bool conflict = m_inputMap.hasConflict(name);

        ImGui::PushID(i);
        ImGui::Text("%-14s", name.c_str());
        ImGui::SameLine(150.0f);
        if (conflict) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 90, 90, 255));
        ImGui::TextUnformatted(bindingLabel(binding).c_str());
        if (conflict) {
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "(conflict)");
        }
        ImGui::SameLine(300.0f);
        if (ImGui::SmallButton("Rebind")) m_rebinding = i;
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) m_inputMap.unbind(name);
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Save")) m_inputMap.saveToFile("ikore_input.ini");
    ImGui::SameLine();
    if (ImGui::Button("Load")) m_inputMap.loadFromFile("ikore_input.ini");
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) m_inputMap.resetToDefaults();
    if (m_inputMap.anyConflicts()) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Some actions share the same input.");
    }

    // Live view: which actions are currently active, proving rebinds apply at once.
    ImGui::Separator();
    ImGui::TextDisabled("Active now:");
    for (int i = 0; i < static_cast<int>(m_inputMap.actionCount()); ++i) {
        if (bindingActive(m_inputMap.bindingAt(static_cast<std::size_t>(i)))) {
            ImGui::BulletText("%s", m_inputMap.actionName(static_cast<std::size_t>(i)).c_str());
        }
    }

    ImGui::End();
}

void DebugUI::applyScaling() {
    ImGuiIO& io = ImGui::GetIO();
    const float dpi = io.DisplayFramebufferScale.y > 0.0f ? io.DisplayFramebufferScale.y : 1.0f;
    m_effectiveScale = computeUiScale(m_uiScale, io.DisplaySize.y, dpi);

    // Re-apply the unscaled base style, then scale it - cheap (a struct copy plus
    // ScaleAllSizes), with no font-atlas rebuild, so the cost is negligible (#61).
    if (s_baseStyleCaptured) {
        ImGui::GetStyle() = s_baseStyle;
        ImGui::GetStyle().ScaleAllSizes(m_effectiveScale);
    }
    io.FontGlobalScale = m_effectiveScale;
    m_hud.setScale(m_effectiveScale);
}

void DebugUI::renderScaling() {
    if (!m_showScaling) return;

    ImGui::Begin("UI Scaling", &m_showScaling);

    const ImGuiIO& io = ImGui::GetIO();
    const float w = io.DisplaySize.x;
    const float h = io.DisplaySize.y;
    ImGui::Text("Resolution: %.0f x %.0f", w, h);
    ImGui::Text("DPI / framebuffer scale: %.2f", io.DisplayFramebufferScale.y);
    ImGui::Text("Aspect: %.3f (%s)", aspectRatio(w, h), aspectCategoryName(classifyAspect(w, h)));
    ImGui::Text("Effective UI scale: %.2f", m_effectiveScale);

    ImGui::Separator();
    float userScale = m_uiScale.userScale;
    if (ImGui::SliderFloat("User scale", &userScale, m_uiScale.minScale, m_uiScale.maxScale, "%.2f")) {
        m_uiScale.userScale = userScale;
        m_settings.setFloat("ui.scale", userScale);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings(); // persist once, on release
    ImGui::Checkbox("Auto-scale with resolution", &m_uiScale.autoResolutionScale);

    if (ImGui::Button("Reset")) {
        m_uiScale.userScale = 1.0f;
        m_settings.setFloat("ui.scale", 1.0f);
        saveSettings();
    }

    ImGui::End();
}

void DebugUI::renderBenchmark() {
    if (!m_showBenchmark) return;

    ImGui::Begin("Benchmark", &m_showBenchmark);

    const bool capturing = m_benchmark.capturing();
    if (capturing) {
        if (ImGui::Button("Stop")) m_benchmark.stop();
    } else {
        if (ImGui::Button("Start")) m_benchmark.start();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        m_benchmark.reset();
        m_benchmarkStatus.clear();
    }

    ImGui::Text("Capturing: %s", capturing ? "yes" : "no");
    ImGui::Text("Samples: %zu    Duration: %.2f s", m_benchmark.sampleCount(),
                m_benchmark.durationSeconds());

    if (m_benchmark.sampleCount() > 0) {
        ImGui::Text("FPS: avg %.1f (min %.1f, max %.1f)", m_benchmark.avgFps(), m_benchmark.minFps(),
                    m_benchmark.maxFps());
        ImGui::Text("Frame ms: avg %.3f (min %.3f, max %.3f)", m_benchmark.avgFrameMs(),
                    m_benchmark.minFrameMs(), m_benchmark.maxFrameMs());
        if (m_benchmark.peakMemoryBytes() > 0) {
            ImGui::Text("Peak memory: %.1f MB",
                        static_cast<double>(m_benchmark.peakMemoryBytes()) / (1024.0 * 1024.0));
        }
    }

    ImGui::Separator();
    ImGui::Checkbox("Export as JSON", &m_benchmarkJson);
    ImGui::SameLine();
    if (ImGui::Button("Export")) {
        const char* path = m_benchmarkJson ? "ikore_benchmark.json" : "ikore_benchmark.csv";
        const sim::ExportFormat format = m_benchmarkJson ? sim::ExportFormat::Json : sim::ExportFormat::Csv;
        if (m_benchmark.exportToFile(path, format)) {
            m_benchmarkStatus = "Exported " + std::to_string(m_benchmark.sampleCount()) +
                                " samples to " + path;
        } else {
            m_benchmarkStatus = "Export failed";
        }
    }
    if (!m_benchmarkStatus.empty()) ImGui::TextUnformatted(m_benchmarkStatus.c_str());

    ImGui::End();
}

static ImVec4 colorForLogLevel(LogViewerLevel level) {
    switch (level) {
        case LogViewerLevel::Trace: return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
        case LogViewerLevel::Debug: return ImVec4(0.65f, 0.75f, 0.95f, 1.0f);
        case LogViewerLevel::Info: return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        case LogViewerLevel::Warning: return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
        case LogViewerLevel::Error: return ImVec4(0.95f, 0.40f, 0.40f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

void DebugUI::renderLog() {
    if (!m_showLog) return;

    ImGui::Begin("Log Viewer", &m_showLog);

    // Filter controls: minimum level and category.
    static const char* levelNames[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
    ImGui::SetNextItemWidth(110.0f);
    ImGui::Combo("Min level", &m_logFilterLevel, levelNames, IM_ARRAYSIZE(levelNames));

    const std::vector<std::string> cats = m_log.categories();
    if (m_logCategoryIndex > static_cast<int>(cats.size())) m_logCategoryIndex = 0;
    const char* catPreview =
        m_logCategoryIndex == 0 ? "(all)" : cats[static_cast<std::size_t>(m_logCategoryIndex - 1)].c_str();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("Category", catPreview)) {
        if (ImGui::Selectable("(all)", m_logCategoryIndex == 0)) m_logCategoryIndex = 0;
        for (int i = 0; i < static_cast<int>(cats.size()); ++i) {
            if (ImGui::Selectable(cats[static_cast<std::size_t>(i)].c_str(), m_logCategoryIndex == i + 1)) {
                m_logCategoryIndex = i + 1;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_logAutoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) m_log.clear();

    ImGui::Text("Total: %llu    Warnings+: %zu    Errors: %zu",
                static_cast<unsigned long long>(m_log.totalLogged()),
                m_log.countAtLeast(LogViewerLevel::Warning), m_log.countAtLeast(LogViewerLevel::Error));
    ImGui::Separator();

    const LogViewerLevel minLevel = static_cast<LogViewerLevel>(m_logFilterLevel);
    const std::string catFilter =
        m_logCategoryIndex > 0 ? cats[static_cast<std::size_t>(m_logCategoryIndex - 1)] : std::string();

    // Real-time tail: iterate the ring in place (no per-frame allocation) and
    // auto-scroll to the newest line while pinned to the bottom.
    ImGui::BeginChild("log_scroll", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (const LogRecord& record : m_log.records()) {
        if (static_cast<int>(record.level) < static_cast<int>(minLevel)) continue;
        if (!catFilter.empty() && record.category != catFilter) continue;
        ImGui::PushStyleColor(ImGuiCol_Text, colorForLogLevel(record.level));
        ImGui::TextUnformatted(record.format().c_str());
        ImGui::PopStyleColor();
    }
    if (m_logAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}

void DebugUI::renderHud() {
    if (!m_showHud) return;

    const ImGuiIO& io = ImGui::GetIO();
    m_hud.setScreenSize(io.DisplaySize.x, io.DisplaySize.y); // resolution aware (#55)
    // HUD scale is set globally by applyScaling() (#61).

    // Borderless, click-through overlays so the HUD never obstructs gameplay input.
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

    int index = 0;
    for (const HudElement& e : m_hud.elements()) {
        if (!e.visible) continue;

        const HudVec2 pos = m_hud.positionOf(e);
        ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y));
        ImGui::SetNextWindowSize(ImVec2(e.size.x * m_hud.scale(), e.size.y * m_hud.scale()));
        ImGui::SetNextWindowBgAlpha(0.35f);

        char windowId[32];
        std::snprintf(windowId, sizeof(windowId), "##hud_%d", index++);
        if (ImGui::Begin(windowId, nullptr, flags)) {
            switch (e.widget) {
                case HudWidget::Text:
                    ImGui::TextUnformatted(e.text().c_str());
                    break;
                case HudWidget::Value:
                    ImGui::Text("%s: %d", e.name.c_str(), e.value());
                    break;
                case HudWidget::Bar: {
                    char label[48];
                    std::snprintf(label, sizeof(label), "%s %.0f%%", e.name.c_str(),
                                  static_cast<double>(e.bar()) * 100.0);
                    ImGui::ProgressBar(e.bar(), ImVec2(-1.0f, 0.0f), label);
                    break;
                }
                case HudWidget::List:
                    ImGui::TextUnformatted((e.name + ":").c_str());
                    for (const std::string& item : e.list()) {
                        ImGui::BulletText("%s", item.c_str());
                    }
                    break;
            }
        }
        ImGui::End();
    }
}

int DebugUI::onConsoleTextEdit(void* callbackData) {
    ImGuiInputTextCallbackData* data = static_cast<ImGuiInputTextCallbackData*>(callbackData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        const std::string current(data->Buf, data->Buf + data->BufTextLen);
        const std::string completed = m_console.complete(current);
        if (completed.size() > current.size()) {
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, completed.c_str());
        }
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        std::string recalled;
        if (data->EventKey == ImGuiKey_UpArrow) {
            recalled = m_console.historyPrev();
        } else if (data->EventKey == ImGuiKey_DownArrow) {
            recalled = m_console.historyNext();
        }
        data->DeleteChars(0, data->BufTextLen);
        if (!recalled.empty()) data->InsertChars(0, recalled.c_str());
    }
    return 0;
}

void DebugUI::renderPerfOverlay() {
    if (!m_showPerf) return;

    m_perf.refreshMemory(); // sampled only while the overlay is on screen

    ImGui::Begin("Performance", &m_showPerf);
    ImGui::Text("FPS: %.1f  (avg %.1f)", m_perf.fps(), m_perf.avgFps());
    ImGui::Text("Frame: %.3f ms  (avg %.3f, min %.3f, max %.3f)", static_cast<double>(m_perf.lastFrameMs()),
                static_cast<double>(m_perf.avgFrameMs()), static_cast<double>(m_perf.minFrameMs()),
                static_cast<double>(m_perf.maxFrameMs()));

    const std::vector<float> history = m_perf.historyMs();
    if (!history.empty()) {
        const float scaleMax = m_perf.maxFrameMs() * 1.25f + 1.0f;
        ImGui::PlotLines("Frame ms", history.data(), static_cast<int>(history.size()), 0, nullptr, 0.0f,
                         scaleMax, ImVec2(0.0f, 60.0f));
    }

    const long memoryBytes = m_perf.memoryBytes();
    if (memoryBytes > 0) {
        ImGui::Text("Memory (RSS): %.1f MB", static_cast<double>(memoryBytes) / (1024.0 * 1024.0));
    }
    ImGui::End();
}

void DebugUI::renderVerticalSlice() {
    if (!m_showSlice) return;

    ImGui::Begin("Vertical Slice", &m_showSlice);

    ImGui::Text("Neighborhood: %zu buildings, %zu roads   |   nav grid %d x %d",
                m_slice.city().buildings.size(), m_slice.city().roads.size(), m_slice.grid().width,
                m_slice.grid().height);
    ImGui::Text("Tick %llu   Agents %zu   Arrived %zu   Moving %d",
                static_cast<unsigned long long>(m_slice.tick()), m_slice.agentCount(),
                m_slice.arrivedCount(), m_slice.agentsMoving());

    ImGui::Separator();

    // Playback controls.
    if (m_slicePlaying) {
        if (ImGui::Button("Pause")) m_slicePlaying = false;
    } else {
        if (ImGui::Button("Play")) m_slicePlaying = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Step")) m_slice.step();
    ImGui::SameLine();
    if (ImGui::Button("Reset crowd")) {
        m_slice.resetCrowd();
        m_slicePlaying = false;
    }

    // Rewind scrubber: the handle tracks the current tick during playback; drag it
    // back and release to rewind there (which drops the future), then Play to replay.
    const int oldest = static_cast<int>(m_slice.oldestTick());
    const int now = static_cast<int>(m_slice.tick());
    if (!m_sliceScrubbing) m_sliceRewindTick = now;
    if (m_sliceRewindTick < oldest) m_sliceRewindTick = oldest;
    if (m_sliceRewindTick > now) m_sliceRewindTick = now;

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##rewind", &m_sliceRewindTick, oldest, now, "rewind to tick %d");
    m_sliceScrubbing = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit() && m_sliceRewindTick < now) {
        if (m_slice.rewindTo(static_cast<std::uint64_t>(m_sliceRewindTick))) m_slicePlaying = false;
    }
    ImGui::TextDisabled("Drag the slider left and release to rewind; press Play to replay.");

    ImGui::End();
}

} // namespace IKore
