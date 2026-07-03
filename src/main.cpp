#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <memory>
#include <vector>
#include <cmath>
#include <string>
#include <filesystem>
#include <fstream>
#include <thread>

#include "core/Logger.h"
#include "Camera.h"
#include "CameraController.h"
#include "Shader.h"
#include "Texture.h"
#include "Model.h"
#include "Light.h"
#include "PostProcessor.h"
#include "DeltaTimeDemo.h"
#include "Skybox.h"
#include "ParticleSystem.h"
#include "ShadowMap.h"
#include "CascadedShadowMap.h"
#include "render/Ibl.h"
#include "Frustum.h"
#include "render/FrameGraph.h"
#include "render/ShadowSkinning.h"
#include "core/Entity.h"
#include "core/EntityTypes.h"
#include "core/Transform.h"
#include "core/TransformableEntities.h"
#include "InputComponent.h"
#include "ControllableEntities.h"
#include "CameraComponent.h"
#include "EnhancedCameraEntity.h"
#include "Serialization.h"
#include "core/EntityRegistration.h"
#include "core/EntityDebugSystem.h"
#include "ui/DebugUI.h"

// Forward declarations for GLFW callbacks
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

// Function to render scene objects (for both shadow pass and main rendering).
// For the shadow pass, an optional skinned depth program and bone palette let a mesh
// with bones deform to its animated pose (issue #266); boneless meshes keep the
// static path unchanged.
void renderSceneObjects(std::shared_ptr<IKore::Shader> shader, GLuint VAO, bool modelLoaded,
                       const IKore::Model& cubeModel, double currentFrameTime,
                       std::shared_ptr<IKore::Shader> skinnedShadowShader = nullptr,
                       const std::vector<glm::mat4>* bonePalette = nullptr);

// Global camera instance and controller
IKore::Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
IKore::CameraController cameraController(camera);

// Global post-processor pointer for keyboard callbacks
IKore::PostProcessor* g_postProcessor = nullptr;

// Global skybox pointer for keyboard callbacks
IKore::Skybox* g_skybox = nullptr;

// Image-based lighting for the PBR ambient term (issue #270). Generated at load from the
// skybox cubemap and on by default whenever a valid environment exists (issue #288);
// g_iblEnabled (toggle B) forces the constant-ambient fallback, which is also used
// automatically whenever no environment is present. g_iblIntensity scales the environment
// ambient (an exposure control, keys [ and ]).
IKore::IblEnvironment* g_ibl = nullptr;
bool g_iblEnabled = true;
float g_iblIntensity = 1.0f;

// Global debug UI pointer for keyboard callbacks (F1 toggles the overlay)
IKore::DebugUI* g_debugUI = nullptr;

// Global particle system manager for keyboard callbacks
IKore::ParticleSystemManager* g_particleManager = nullptr;

// Global shadow map manager for keyboard callbacks
IKore::ShadowMapManager* g_shadowManager = nullptr;

// Cascaded shadow maps for the directional light (issue #265). Opt-in: while
// g_useCascadedShadows is false the directional light keeps the original single map
// and the render is unchanged. Toggle with K; cycle the split lambda with J.
IKore::CascadedShadowMap* g_cascadedShadowMap = nullptr;
bool g_useCascadedShadows = false;
float g_cascadeSplitLambda = 0.5f;   // 0 uniform .. 1 logarithmic split
float g_cascadeBlendBand = 2.0f;     // view-depth width of the cross-cascade seam blend

// Global frustum culler for rendering optimization
IKore::FrustumCuller g_frustumCuller;
bool g_frustumCullingEnabled = true;

// Camera Component demonstration variables
std::shared_ptr<IKore::EnhancedCameraEntity> g_enhancedCamera = nullptr;
std::shared_ptr<IKore::TransformableGameObject> g_followTarget = nullptr;
int g_currentCameraMode = 0; // 0=FREE_CAMERA, 1=THIRD_PERSON, 2=ORBITAL, 3=FIRST_PERSON
bool g_useCameraComponent = false; // Toggle between old and new camera system
float g_cameraTransitionDuration = 2.0f; // Smooth transitions

// Mouse tracking variables
bool firstMouse = true;
float lastX = 1280.0f / 2.0f;
float lastY = 720.0f / 2.0f;

// Function declarations
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
// (Re)generate the IBL environment from the current skybox cubemap (issue #288).
void regenerateIblFromSkybox();

int main() {
    // Initialize logging system
    IKore::Logger::getInstance().initialize();
    LOG_INFO("Starting IKore Engine");

    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }
    LOG_INFO("GLFW initialized successfully");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "IKore Engine", nullptr, nullptr);
    if (!window) {
        LOG_ERROR("Failed to create GLFW window");
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    LOG_INFO("Window created successfully");
    glfwMakeContextCurrent(window);

    // Set up all callbacks
    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    // Capture mouse cursor
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Set initial camera aspect ratio
    camera.setAspectRatio(1280.0f / 720.0f);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        LOG_ERROR("Failed to initialize GLAD");
        std::cerr << "Failed to initialize GLAD\n";
        return -1;
    }
    LOG_INFO("GLAD initialized successfully");

    // Initialize the in-engine debug UI (Dear ImGui). GL context is 3.3 core,
    // so the GL3 backend uses GLSL "#version 330". Starts hidden; F1 toggles it.
    IKore::DebugUI debugUI;
    debugUI.initialize(window, "#version 330");
    g_debugUI = &debugUI;

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    const double targetFPS = 60.0;
    const double targetFrameTime = 1.0 / targetFPS;

    // === Post-Processing Initialization ===
    IKore::PostProcessor postProcessor;
    postProcessor.initialize(1280, 720);
    g_postProcessor = &postProcessor; // Set global pointer for keyboard callbacks
    
    // Configure post-processing effects
    if (auto* bloom = postProcessor.getBloomEffect()) {
        bloom->setThreshold(1.0f);
        bloom->setIntensity(0.5f);
        bloom->setBlurPasses(5);
        bloom->setEnabled(true);
    }
    
    if (auto* fxaa = postProcessor.getFXAAEffect()) {
        fxaa->setEnabled(true);
    }
    
    if (auto* ssao = postProcessor.getSSAOEffect()) {
        // Full depth-based SSAO (issue #259) is functional; keep it opt-in so the
        // default image is unchanged. KEY 3 toggles it at runtime.
        ssao->setEnabled(false);
    }
    
    LOG_INFO("Post-processing effects initialized");
    LOG_INFO("Controls: Press 1 to toggle Bloom, 2 to toggle FXAA, 3 to toggle SSAO");

    // === Skybox Initialization ===
    IKore::Skybox skybox;
    
    // Try to load skybox from directory first (if textures exist)
    bool skyboxLoaded = false;
    if (std::filesystem::exists("assets/skybox/space")) {
        skyboxLoaded = skybox.loadFromDirectory("assets/skybox/space", ".jpg");
        if (!skyboxLoaded) {
            skyboxLoaded = skybox.loadFromDirectory("assets/skybox/space", ".png");
        }
    }
    
    // Fallback: Create procedural skybox if no textures found
    if (!skyboxLoaded) {
        LOG_INFO("No skybox textures found, creating procedural test skybox");
        skyboxLoaded = skybox.initializeTestSkybox();
    }
    
    if (skyboxLoaded) {
        LOG_INFO("Skybox initialized successfully");
    } else {
        LOG_WARNING("Skybox initialization failed - skybox will be disabled");
        skybox.setEnabled(false);
    }
    
    g_skybox = &skybox; // Set global pointer for keyboard callbacks

    // Generate image-based lighting from the skybox cubemap (issue #270): irradiance,
    // prefiltered specular, and the BRDF LUT for the PBR ambient term. On by default when a
    // valid environment exists (issue #288); PBR materials fall back to constant ambient
    // when generation fails or no skybox is present. g_ibl is always published so the
    // environment can be regenerated when the skybox changes; iblOn additionally checks
    // isValid().
    IKore::IblEnvironment iblEnv;
    g_ibl = &iblEnv;
    if (skyboxLoaded && skybox.isLoaded()) {
        regenerateIblFromSkybox();
    }

    // === Particle System Initialization ===
    IKore::ParticleSystemManager particleManager;
    g_particleManager = &particleManager; // Set global pointer for keyboard callbacks
    
    // Create some default particle effects
    auto* fireSystem = particleManager.createFireEffect(glm::vec3(-2.0f, 0.0f, 0.0f));
    auto* smokeSystem = particleManager.createSmokeEffect(glm::vec3(2.0f, 0.0f, 0.0f));

    
    // Start the effects
    if (fireSystem) fireSystem->play();
    if (smokeSystem) smokeSystem->play();
    
    LOG_INFO("Particle systems initialized");
    LOG_INFO("Controls: Press 4 to toggle skybox, 5/6 to adjust intensity, 7 to toggle particles, 8 for fire, 9 for explosion, 0 for smoke, - for sparks");

    // === Entity System Initialization ===
    IKore::EntityManager& entityManager = IKore::getEntityManager();
    
    // Register all serializable entity types for deserialization
    IKore::registerAllEntityTypes();
    LOG_INFO("Entity types registered for serialization");
    
    // Create some test entities to demonstrate the system
    auto testEntity1 = entityManager.createEntity<IKore::TestEntity>("Entity System Test 1");
    testEntity1->setLifetime(10.0f); // This entity will expire after 10 seconds
    
    auto testEntity2 = entityManager.createEntity<IKore::TestEntity>("Entity System Test 2");
    // testEntity2 has infinite lifetime
    
    auto gameObject1 = entityManager.createEntity<IKore::GameObject>("GameObject 1", glm::vec3(1.0f, 2.0f, 3.0f));
    gameObject1->setRotation(glm::vec3(45.0f, 0.0f, 0.0f));
    gameObject1->setScale(glm::vec3(2.0f, 1.0f, 1.0f));
    
    auto light1 = entityManager.createEntity<IKore::LightEntity>("Main Light", IKore::LightEntity::LightType::DIRECTIONAL);
    light1->setPosition(glm::vec3(0.0f, 5.0f, 0.0f));
    light1->setColor(glm::vec3(1.0f, 0.9f, 0.8f));
    light1->setIntensity(1.5f);
    
    auto camera1 = entityManager.createEntity<IKore::EnhancedCameraEntity>("Entity Camera");
    camera1->setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
    camera1->setFieldOfView(60.0f);
    
    // Initialize all entities
    entityManager.initializeAll();
    
    // === Transform Component Demonstration ===
    LOG_INFO("=== Transform Component System Demonstration ===");
    
    // Create parent-child hierarchy with transformable entities
    auto parentObject = entityManager.createEntity<IKore::TransformableGameObject>("Parent Object");
    parentObject->getTransform().setPosition(5.0f, 0.0f, 0.0f);
    parentObject->getTransform().setRotation(0.0f, 45.0f, 0.0f);
    parentObject->getTransform().setScale(2.0f);
    
    auto childObject1 = entityManager.createEntity<IKore::TransformableGameObject>("Child Object 1");
    childObject1->getTransform().setPosition(0.0f, 2.0f, 0.0f); // Local position relative to parent
    childObject1->getTransform().setScale(0.5f);
    childObject1->setParent(parentObject.get());
    
    auto childObject2 = entityManager.createEntity<IKore::TransformableGameObject>("Child Object 2");
    childObject2->getTransform().setPosition(0.0f, -2.0f, 0.0f); // Local position relative to parent
    childObject2->getTransform().setRotation(0.0f, 0.0f, 90.0f);
    childObject2->setParent(parentObject.get());
    
    // Create transformable light
    auto transformableLight = entityManager.createEntity<IKore::TransformableLight>(
        "Transformable Point Light", 
        IKore::TransformableLight::LightType::POINT,
        glm::vec3(-3.0f, 2.0f, 0.0f), // Position
        glm::vec3(0.8f, 0.6f, 1.0f),  // Purple-ish color
        2.0f  // Intensity
    );
    
    // Create transformable camera
    auto transformableCamera = entityManager.createEntity<IKore::TransformableCamera>(
        "Transformable Camera",
        glm::vec3(0.0f, 3.0f, 8.0f), // Position
        glm::vec3(-15.0f, 0.0f, 0.0f), // Rotation (looking slightly down)
        45.0f,  // FOV
        800.0f / 600.0f,  // Aspect ratio
        0.1f,   // Near plane
        100.0f  // Far plane
    );
    
    // Make the transformable camera look at the parent object
    transformableCamera->lookAt(parentObject->getTransform().getWorldPosition());
    
    LOG_INFO("Created hierarchical transform system:");
    LOG_INFO("- Parent Object at world pos: (" + 
             std::to_string(parentObject->getTransform().getWorldPosition().x) + ", " +
             std::to_string(parentObject->getTransform().getWorldPosition().y) + ", " +
             std::to_string(parentObject->getTransform().getWorldPosition().z) + ")");
    
    LOG_INFO("- Child 1 local pos: (" + 
             std::to_string(childObject1->getTransform().getPosition().x) + ", " +
             std::to_string(childObject1->getTransform().getPosition().y) + ", " +
             std::to_string(childObject1->getTransform().getPosition().z) + ")");
    
    LOG_INFO("- Child 1 world pos: (" + 
             std::to_string(childObject1->getTransform().getWorldPosition().x) + ", " +
             std::to_string(childObject1->getTransform().getWorldPosition().y) + ", " +
             std::to_string(childObject1->getTransform().getWorldPosition().z) + ")");
    
    // Log entity system statistics
    auto stats = entityManager.getStats();
    LOG_INFO("Entity System initialized with " + std::to_string(stats.totalEntities) + " entities");
    LOG_INFO("Entity ID range: " + std::to_string(stats.lowestID) + " - " + std::to_string(stats.highestID));
    LOG_INFO("Controls: Press E to show entity stats, R to remove test entities, T to create new test entity");
    LOG_INFO("Serialization Controls: F9 to load scene, F10 to clear scene, F11 to save as JSON, F12 to save as binary");
    LOG_INFO("Debug Controls: F8 to toggle Entity Debug System");
    LOG_INFO("Transform Controls: Y to rotate parent, U to scale parent, I to move light, O to move camera, P to print hierarchy");

    // === Input Component Demonstration ===
    LOG_INFO("=== Input Component System Demonstration ===");
    
    // Initialize Input Manager with the GLFW window
    IKore::InputManager& inputManager = IKore::InputManager::getInstance();
    inputManager.initialize(window);
    
    // Create controllable entities to demonstrate Input Component
    auto controllablePlayer = entityManager.createEntity<IKore::ControllableGameObject>(
        "Controllable Player",
        glm::vec3(0.0f, 0.0f, -5.0f),  // Start behind origin
        IKore::ControllableGameObject::MovementMode::CAMERA_STYLE
    );
    controllablePlayer->setMovementSpeed(8.0f);
    controllablePlayer->setRotationSpeed(0.15f);
    controllablePlayer->setMouseCapture(false); // Start with free cursor
    
    auto controllableCamera = entityManager.createEntity<IKore::ControllableCamera>(
        "FPS Camera",
        glm::vec3(0.0f, 2.0f, 10.0f),
        IKore::ControllableCamera::CameraMode::FIRST_PERSON
    );
    controllableCamera->setMovementSpeed(6.0f);
    controllableCamera->setMouseSensitivity(0.1f);
    controllableCamera->setInputEnabled(false); // Start disabled to avoid conflicts
    
    auto controllableLight = entityManager.createEntity<IKore::ControllableLight>(
        "Controllable Light",
        IKore::ControllableLight::LightType::POINT,
        glm::vec3(3.0f, 3.0f, 0.0f),
        glm::vec3(1.0f, 0.8f, 0.6f),  // Warm white
        3.0f
    );
    controllableLight->setMovementSpeed(4.0f);
    controllableLight->setInputEnabled(false); // Start disabled to avoid conflicts
    
    LOG_INFO("Created Input Component entities:");
    LOG_INFO("- Controllable Player (WASD movement, mouse look with ESC toggle)");
    LOG_INFO("- FPS Camera (press F1 to activate, 1/2/3 to switch modes)");
    LOG_INFO("- Controllable Light (press F2 to activate, arrows to move, +/- intensity, C to cycle color)");
    LOG_INFO("Input Controls:");
    LOG_INFO("  F1: Toggle FPS Camera control");
    LOG_INFO("  F2: Toggle Light control");
    LOG_INFO("  F3: Toggle Player control");
    LOG_INFO("  ESC: Toggle mouse capture (when player control active)");
    LOG_INFO("  WASD: Movement");
    LOG_INFO("  Space/Shift: Up/Down movement");
    LOG_INFO("  Arrow Keys: Light movement (when light control active)");
    LOG_INFO("  +/-: Light intensity adjustment");
    LOG_INFO("  C: Cycle light color");
    LOG_INFO("  1/2/3: Camera mode switching (when camera control active)");
    LOG_INFO("  Mouse: Look around (when mouse captured or camera control active)");
    LOG_INFO("  Scroll: Speed/zoom adjustment");

    // === Camera Component Demonstration ===
    LOG_INFO("=== Camera Component System Demonstration ===");
    
    // Create enhanced camera entity with Camera Component
    g_enhancedCamera = entityManager.createEntity<IKore::EnhancedCameraEntity>(
        "Enhanced Camera",
        glm::vec3(0.0f, 5.0f, 10.0f),  // Initial position
        glm::vec3(0.0f, 0.0f, 0.0f),   // Look at origin
        IKore::CameraComponent::CameraType::FREE_CAMERA
    );
    g_enhancedCamera->setFieldOfView(45.0f);
    g_enhancedCamera->setAspectRatio(800.0f / 600.0f);
    
    // Create a target object for the camera to follow
    g_followTarget = entityManager.createEntity<IKore::TransformableGameObject>("Camera Follow Target");
    g_followTarget->getTransform().setPosition(0.0f, 1.0f, 0.0f);
    
    // Configure third-person camera settings
    IKore::CameraComponent::ThirdPersonConfig thirdPersonConfig;
    thirdPersonConfig.distance = 8.0f;
    thirdPersonConfig.height = 3.0f;
    thirdPersonConfig.followSpeed = 3.0f;
    thirdPersonConfig.rotationSpeed = 2.0f;
    g_enhancedCamera->setThirdPersonConfig(thirdPersonConfig);
    
    // Configure orbital camera settings
    IKore::CameraComponent::OrbitalConfig orbitalConfig;
    orbitalConfig.radius = 12.0f;
    orbitalConfig.elevation = 20.0f;
    orbitalConfig.autoOrbit = true;
    orbitalConfig.autoOrbitSpeed = 0.3f;
    g_enhancedCamera->setOrbitalConfig(orbitalConfig);
    
    LOG_INFO("Created Camera Component entities:");
    LOG_INFO("- Enhanced Camera with multiple modes");
    LOG_INFO("- Follow Target for third-person mode");
    LOG_INFO("Camera Component Controls:");
    LOG_INFO("  4: Free Camera mode");
    LOG_INFO("  5: Third-Person mode (follows target)");
    LOG_INFO("  6: Orbital mode (orbits around target)");
    LOG_INFO("  7: First-Person mode (attaches to target)");
    LOG_INFO("  C: Toggle between old camera system and Camera Component");
    LOG_INFO("  T: Increase transition duration");
    LOG_INFO("  G: Decrease transition duration");

    // Initialize delta time demo
    IKore::DeltaTimeDemo deltaDemo;
    deltaDemo.initialize();

    // === Cube vertices with normals and texture coordinates ===
    float vertices[] = {
        // positions          // normals           // texture coords
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,

        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,

        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  0.0f,
        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  0.0f,

         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  1.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,

        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  1.0f,
         0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  1.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  0.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  1.0f,

        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  0.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f
    };

    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // Texture coordinate attribute
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    std::string shaderError;
    auto shaderPtr = IKore::Shader::loadFromFilesCached("src/shaders/phong_shadows.vert", "src/shaders/phong_shadows.frag", shaderError);
    if(!shaderPtr){
        LOG_ERROR(std::string("Shader file load/compile failed: ") + shaderError);
    }

    // Opt-in metallic-roughness PBR program (issue #234). Only used to draw
    // materials that opt in; the Blinn-Phong path above is left untouched.
    auto pbrShaderPtr = IKore::Shader::loadFromFilesCached("src/shaders/pbr.vert", "src/shaders/pbr.frag", shaderError);
    if(!pbrShaderPtr){
        LOG_ERROR(std::string("PBR shader file load/compile failed: ") + shaderError);
    }

    // Load shadow mapping shaders
    auto shadowShaderPtr = IKore::Shader::loadFromFilesCached("src/shaders/shadow_depth.vert", "src/shaders/shadow_depth.frag", shaderError);
    if(!shadowShaderPtr){
        LOG_ERROR(std::string("Shadow shader file load/compile failed: ") + shaderError);
    }

    // Skinned directional shadow-depth program (issue #266): reuses shadow_depth.frag
    // but skins the vertex so an animated mesh casts its posed shadow. Selected per
    // mesh by hasBones(); boneless meshes keep shadowShaderPtr above.
    auto skinnedShadowShaderPtr = IKore::Shader::loadFromFilesCached("src/shaders/skinned_shadow_depth.vert", "src/shaders/shadow_depth.frag", shaderError);
    if(!skinnedShadowShaderPtr){
        LOG_ERROR(std::string("Skinned shadow shader file load/compile failed: ") + shaderError);
    }

    // Setup lights
    IKore::DirectionalLight dirLight(glm::vec3(-0.2f, -1.0f, -0.3f));
    dirLight.ambient = glm::vec3(0.05f, 0.05f, 0.05f);
    dirLight.diffuse = glm::vec3(0.4f, 0.4f, 0.4f);
    dirLight.specular = glm::vec3(0.5f, 0.5f, 0.5f);

    IKore::PointLight pointLight(glm::vec3(1.2f, 1.0f, 2.0f));
    pointLight.ambient = glm::vec3(0.05f, 0.05f, 0.05f);
    pointLight.diffuse = glm::vec3(0.8f, 0.8f, 0.8f);
    pointLight.specular = glm::vec3(1.0f, 1.0f, 1.0f);

    // === Shadow Mapping Initialization ===
    IKore::ShadowMapManager shadowManager;
    if (!shadowManager.initialize()) {
        LOG_ERROR("Failed to initialize shadow mapping system");
        return -1;
    }
    g_shadowManager = &shadowManager; // Set global pointer for keyboard callbacks
    
    // Create shadow map for directional light
    auto* dirShadowMap = shadowManager.createDirectionalShadowMap(0, 2048);
    if (!dirShadowMap) {
        LOG_WARNING("Failed to create directional shadow map - shadows will be disabled");
    } else {
        LOG_INFO("Directional shadow map created successfully");
    }

    // Cascaded shadow map for the directional light (issue #265). Additive and
    // opt-in: it is only used while g_useCascadedShadows is true (toggle with K), so
    // the default path stays the single directional map above.
    IKore::CascadedShadowMap cascadedShadowMap(4 /* cascades */, 2048);
    if (!cascadedShadowMap.initialize()) {
        LOG_WARNING("Failed to create cascaded shadow map - CSM path will be unavailable");
    } else {
        g_cascadedShadowMap = &cascadedShadowMap;
        LOG_INFO("Cascaded shadow map created successfully (press K to toggle)");
    }
    
    // Create shadow map for point light
    auto* pointShadowMap = shadowManager.createPointShadowMap(1, 1024);
    if (!pointShadowMap) {
        LOG_WARNING("Failed to create point shadow map - point light shadows will be disabled");
    } else {
        LOG_INFO("Point light shadow map created successfully");
    }

    // === Texture Loading ===
    IKore::TextureManager textureManager;
    
    // Load diffuse texture
    auto diffuseTexture = IKore::Texture::loadFromFileShared("assets/textures/colorful.png", IKore::Texture::Type::DIFFUSE);
    if (diffuseTexture) {
        textureManager.addTexture(diffuseTexture, "material.diffuse");
        LOG_INFO("Successfully loaded and added diffuse texture");
    } else {
        LOG_ERROR("Failed to load diffuse texture - falling back to color-only material");
    }

    // Load specular texture
    auto specularTexture = IKore::Texture::loadFromFileShared("assets/textures/specular.png", IKore::Texture::Type::SPECULAR);
    if (specularTexture) {
        textureManager.addTexture(specularTexture, "material.specular");
        LOG_INFO("Successfully loaded and added specular texture");
    } else {
        LOG_ERROR("Failed to load specular texture - falling back to color-only material");
    }

    // === Model Loading ===
    LOG_INFO("Loading 3D model...");
    IKore::Model cubeModel;
    bool modelLoaded = cubeModel.loadFromFile("assets/models/cube.obj");
    if (!modelLoaded || cubeModel.getMeshes().empty()) {
        LOG_ERROR("Failed to load model - falling back to primitive geometry");
    } else {
        LOG_INFO("Successfully loaded model with " + std::to_string(cubeModel.getMeshes().size()) + " meshes");
    }

    // Delta time tracking
    double lastFrameTime = glfwGetTime();
    double deltaTime = 0.0;
    
    // Frame timing statistics
    double frameTimeAccumulator = 0.0;
    int frameCount = 0;
    double statsTimer = 0.0;

    LOG_INFO("Starting main game loop with delta time calculation");

    // === Render frame graph ===
    // The per-frame render passes run in the order computed by the render-pass
    // dependency graph rather than a hard-coded sequence (rendering modernization
    // P1 / #232). Reordering or inserting a pass is a change to
    // buildDefaultFrameGraph(), not to the loop below. The order is computed once;
    // handlers are (re)bound each frame to the current frame's locals.
    IKore::render::FrameGraphExecutor frameGraph(IKore::render::buildDefaultFrameGraph());
    {
        std::string order;
        for (const std::string& pass : frameGraph.order()) {
            order += (order.empty() ? "" : " -> ") + pass;
        }
        LOG_INFO("Render frame graph pass order: " + order);
    }

    while (!glfwWindowShouldClose(window)) {
        auto frameStart = std::chrono::high_resolution_clock::now();

        // Calculate delta time using glfwGetTime() for precision
        double currentFrameTime = glfwGetTime();
        deltaTime = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;
        
        // Update statistics
        frameTimeAccumulator += deltaTime;
        frameCount++;
        statsTimer += deltaTime;
        
        // Log frame timing statistics every 2 seconds
        if (statsTimer >= 2.0) {
            double avgFrameTime = frameTimeAccumulator / frameCount;
            double avgFPS = 1.0 / avgFrameTime;
            LOG_INFO("Frame stats - Avg FPS: " + std::to_string(avgFPS) + 
                    ", Avg Frame Time: " + std::to_string(avgFrameTime * 1000.0) + "ms" +
                    ", Current Delta: " + std::to_string(deltaTime * 1000.0) + "ms");
            
            // Log camera status every 10 seconds
            static int logCounter = 0;
            logCounter++;
            if (logCounter >= 5) { // Every 10 seconds (5 * 2 second intervals)
                std::string cameraStatus = cameraController.getCameraStatus();
                LOG_INFO("Camera Status Update:\n" + cameraStatus);
                logCounter = 0;
            }
            
            // Reset statistics
            frameTimeAccumulator = 0.0;
            frameCount = 0;
            statsTimer = 0.0;
        }

        // Input processing
        glfwPollEvents();
        
        // Update Input Manager to process all registered callbacks
        IKore::InputManager& inputManager = IKore::InputManager::getInstance();
        inputManager.update(static_cast<float>(deltaTime));
        
        cameraController.processInput(window, static_cast<float>(deltaTime));

        // Update particle systems
        particleManager.updateAll(static_cast<float>(deltaTime));

        // Update entity system
        entityManager.updateAll(static_cast<float>(deltaTime));

        // Update Entity Debug System
        IKore::getEntityDebugSystem().update(static_cast<float>(deltaTime));

        // === Camera Component System Update ===
        if (g_enhancedCamera && g_followTarget) {
            // Update the follow target position (simulate movement)
            static float targetTimer = 0.0f;
            targetTimer += static_cast<float>(deltaTime);
            glm::vec3 targetPos = glm::vec3(
                sin(targetTimer * 0.5f) * 3.0f,
                1.0f + cos(targetTimer * 0.3f) * 0.5f,
                cos(targetTimer * 0.4f) * 2.0f
            );
            g_followTarget->getTransform().setPosition(targetPos);
            
            // Update enhanced camera
            g_enhancedCamera->update(static_cast<float>(deltaTime));
            
            // Update aspect ratio if needed (in case window was resized)
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            if (width > 0 && height > 0) {
                float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
                g_enhancedCamera->setAspectRatio(aspectRatio);
            }
        }

        // Update demo
        deltaDemo.update();
        deltaDemo.updateTestMovement();

        // Animate point light position
        pointLight.setPosition(glm::vec3(
            sin(currentFrameTime) * 2.0f,
            cos(currentFrameTime) * 1.0f,
            sin(currentFrameTime * 0.5f) * 2.0f + 2.0f
        ));

        // === FRAME RENDER GRAPH ===
        // The per-frame passes below are bound to handlers and then run in the
        // order the render-pass graph computed (see buildDefaultFrameGraph()):
        //   shadow -> scene_begin -> skybox -> forward -> particles -> postprocess

        // Camera matrices for this frame, shared by the skybox/forward/particle
        // passes. Use the Camera Component if enabled.
        glm::mat4 view, projection;
        if (g_useCameraComponent && g_enhancedCamera) {
            view = g_enhancedCamera->getViewMatrix();
            projection = g_enhancedCamera->getProjectionMatrix();
        } else {
            view = camera.getViewMatrix();
            projection = camera.getProjectionMatrix();
        }

        // Update frustum for culling
        if (g_frustumCullingEnabled) {
            g_frustumCuller.updateFrustum(view, projection);
        }

        frameGraph.setHandler(IKore::render::passes::Shadow, [&]() {
            // Update shadow maps with current light positions
            if (dirShadowMap) {
                dirShadowMap->setLightSpaceMatrix(dirLight.direction, glm::vec3(0.0f), 15.0f);
            }
            if (pointShadowMap) {
                pointShadowMap->setPointLightMatrices(pointLight.position, 25.0f);
            }
            // Render directional light shadow map. When the CSM path is on, render
            // one depth pass per cascade into the texture array; otherwise the
            // original single directional pass (unchanged).
            if (g_useCascadedShadows && g_cascadedShadowMap && shadowShaderPtr) {
                // Recover the frustum parameters from this frame's projection so the
                // cascade fit matches the camera exactly (works for either camera path).
                const float p00 = projection[0][0];
                const float p11 = projection[1][1];
                const float p22 = projection[2][2];
                const float p32 = projection[3][2];
                const float fovY = 2.0f * std::atan(1.0f / p11);
                const float aspect = p11 / p00;
                const float nearZ = p32 / (p22 - 1.0f);
                const float farZ = p32 / (p22 + 1.0f);

                g_cascadedShadowMap->computeCascades(view, fovY, aspect, nearZ, farZ,
                                                     dirLight.direction, g_cascadeSplitLambda);
                shadowShaderPtr->use();
                const auto& mats = g_cascadedShadowMap->lightMatrices();
                for (int c = 0; c < g_cascadedShadowMap->cascadeCount(); ++c) {
                    g_cascadedShadowMap->beginCascadePass(c);
                    shadowShaderPtr->setMat4("lightSpaceMatrix", glm::value_ptr(mats[static_cast<std::size_t>(c)]));
                    // The skinned depth program shares the same light matrix (issue #266).
                    if (skinnedShadowShaderPtr) {
                        skinnedShadowShaderPtr->use();
                        skinnedShadowShaderPtr->setMat4("lightSpaceMatrix", glm::value_ptr(mats[static_cast<std::size_t>(c)]));
                    }
                    renderSceneObjects(shadowShaderPtr, VAO, modelLoaded, cubeModel, currentFrameTime, skinnedShadowShaderPtr);
                }
                g_cascadedShadowMap->endShadowPass();
            } else if (dirShadowMap && shadowShaderPtr) {
                dirShadowMap->beginShadowPass();
                glm::mat4 lightSpaceMatrix = dirShadowMap->getLightSpaceMatrix();
                shadowShaderPtr->use();
                shadowShaderPtr->setMat4("lightSpaceMatrix", glm::value_ptr(lightSpaceMatrix));
                // The skinned depth program shares the same light matrix (issue #266).
                if (skinnedShadowShaderPtr) {
                    skinnedShadowShaderPtr->use();
                    skinnedShadowShaderPtr->setMat4("lightSpaceMatrix", glm::value_ptr(lightSpaceMatrix));
                }
                renderSceneObjects(shadowShaderPtr, VAO, modelLoaded, cubeModel, currentFrameTime, skinnedShadowShaderPtr);
                dirShadowMap->endShadowPass();
            }
            // Point light shadow map needs a geometry-shader path; not rendered yet.
        });

        frameGraph.setHandler(IKore::render::passes::SceneBegin, [&]() {
            // Bind the offscreen post-processing target and clear it.
            postProcessor.beginFrame();
            // Hand the frame's projection to the effect chain so the SSAO stage
            // can unproject the depth buffer (issue #259; no-op while SSAO is off).
            postProcessor.setSceneProjection(projection);
        });

        frameGraph.setHandler(IKore::render::passes::Skybox, [&]() {
            // Render skybox first (as background)
            skybox.render(view, projection);
        });

        frameGraph.setHandler(IKore::render::passes::Forward, [&]() {
        if(shaderPtr) {
            shaderPtr->use();
            
            // Set up matrices using Camera class
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::rotate(model, (float)currentFrameTime * glm::radians(50.0f), glm::vec3(0.5f, 1.0f, 0.0f));
            
            glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(model)));
            
            // Set matrices
            shaderPtr->setMat4("model", glm::value_ptr(model));
            shaderPtr->setMat4("view", glm::value_ptr(view));
            shaderPtr->setMat4("projection", glm::value_ptr(projection));
            shaderPtr->setMat3("normalMatrix", glm::value_ptr(normalMatrix));
            
            // Set camera position
            glm::vec3 cameraPos = camera.getPosition();
            shaderPtr->setVec3("viewPos", cameraPos.x, cameraPos.y, cameraPos.z);
            
            // Bind textures and set material properties
            textureManager.bindAll(shaderPtr->id());
            
            // Set material properties (support both textured and non-textured materials)
            bool hasTextures = textureManager.getTextureCount() > 0;

            // These primitives carry no normal map; pin the flag off so a value left
            // over from a prior normal-mapped model draw cannot leak in (issue #238).
            shaderPtr->setFloat("material.useNormalTexture", 0.0f);

            if (hasTextures) {
                // Using textures - set flags
                shaderPtr->setFloat("material.useDiffuseTexture", 1.0f);
                shaderPtr->setFloat("material.useSpecularTexture", 
                    textureManager.getTextureByType(IKore::Texture::Type::SPECULAR) ? 1.0f : 0.0f);
                
                // Set fallback colors for areas where texture might not cover
                shaderPtr->setVec3("material.ambient", 0.2f, 0.2f, 0.2f);
                shaderPtr->setVec3("material.diffuseColor", 1.0f, 1.0f, 1.0f);  // White to not tint texture
                shaderPtr->setVec3("material.specularColor", 0.5f, 0.5f, 0.5f);
            } else {
                // No textures - use colors only
                shaderPtr->setFloat("material.useDiffuseTexture", 0.0f);
                shaderPtr->setFloat("material.useSpecularTexture", 0.0f);
                shaderPtr->setVec3("material.ambient", 1.0f, 0.5f, 0.31f);
                shaderPtr->setVec3("material.diffuseColor", 1.0f, 0.5f, 0.31f);
                shaderPtr->setVec3("material.specularColor", 0.5f, 0.5f, 0.5f);
            }
            
            shaderPtr->setFloat("material.shininess", 32.0f);
            
            // Set directional light
            shaderPtr->setFloat("useDirLight", 1.0f);
            shaderPtr->setVec3("dirLight.direction", dirLight.direction.x, dirLight.direction.y, dirLight.direction.z);
            shaderPtr->setVec3("dirLight.ambient", dirLight.ambient.x, dirLight.ambient.y, dirLight.ambient.z);
            shaderPtr->setVec3("dirLight.diffuse", dirLight.diffuse.x, dirLight.diffuse.y, dirLight.diffuse.z);
            shaderPtr->setVec3("dirLight.specular", dirLight.specular.x, dirLight.specular.y, dirLight.specular.z);
            
            // Set directional light shadow mapping parameters
            if (dirShadowMap) {
                shaderPtr->setFloat("dirLight.castShadows", 1.0f);
                shaderPtr->setMat4("dirLight.lightSpaceMatrix", glm::value_ptr(dirShadowMap->getLightSpaceMatrix()));
                shaderPtr->setFloat("dirLight.shadowBias", dirShadowMap->getShadowBias());
                shaderPtr->setFloat("dirLight.softShadows", dirShadowMap->getSoftShadows() ? 1.0f : 0.0f);
                shaderPtr->setInt("dirLight.pcfKernelSize", dirShadowMap->getPCFKernelSize());
                // PCF filtering options (issue #237). Defaults (box kernel, softness
                // 1.0) reproduce the prior soft-shadow output.
                shaderPtr->setFloat("dirLight.poissonShadows", dirShadowMap->getPoissonSampling() ? 1.0f : 0.0f);
                shaderPtr->setFloat("dirLight.shadowSoftness", dirShadowMap->getShadowSoftness());

                // Bind shadow map texture
                dirShadowMap->bindShadowMap(15); // Use texture unit 15 for shadow map
                shaderPtr->setInt("dirLight.shadowMap", 15);
            } else {
                shaderPtr->setFloat("dirLight.castShadows", 0.0f);
            }
            
            // Set light space matrix for main shader (for FragPosLightSpace calculation)
            if (dirShadowMap) {
                shaderPtr->setMat4("lightSpaceMatrix", glm::value_ptr(dirShadowMap->getLightSpaceMatrix()));
            }

            // Cascaded shadow map uniforms (issue #265). When off, the shader ignores
            // all of these and takes the single-map path, so this is regression-safe.
            if (g_useCascadedShadows && g_cascadedShadowMap) {
                shaderPtr->setFloat("useCascades", 1.0f);
                shaderPtr->setFloat("dirLight.castShadows", 1.0f);
                const auto& mats = g_cascadedShadowMap->lightMatrices();
                const auto& splits = g_cascadedShadowMap->splitDepths();
                const int count = g_cascadedShadowMap->cascadeCount();
                shaderPtr->setInt("dirCascadeCount", count);
                for (int c = 0; c < count; ++c) {
                    const std::string idx = std::to_string(c);
                    shaderPtr->setMat4(("dirCascadeMatrices[" + idx + "]").c_str(),
                                       glm::value_ptr(mats[static_cast<std::size_t>(c)]));
                    shaderPtr->setFloat(("dirCascadeSplits[" + idx + "]").c_str(),
                                        splits[static_cast<std::size_t>(c)]);
                }
                shaderPtr->setMat4("cascadeViewMatrix", glm::value_ptr(view));
                shaderPtr->setFloat("cascadeBlendBand", g_cascadeBlendBand);
                g_cascadedShadowMap->bindArray(14); // texture unit 14 for the cascade array
                shaderPtr->setInt("dirCascadeMaps", 14);
            } else {
                shaderPtr->setFloat("useCascades", 0.0f);
            }

            // Set point light
            shaderPtr->setFloat("numPointLights", 1.0f);
            shaderPtr->setVec3("pointLights[0].position", pointLight.position.x, pointLight.position.y, pointLight.position.z);
            shaderPtr->setVec3("pointLights[0].ambient", pointLight.ambient.x, pointLight.ambient.y, pointLight.ambient.z);
            shaderPtr->setVec3("pointLights[0].diffuse", pointLight.diffuse.x, pointLight.diffuse.y, pointLight.diffuse.z);
            shaderPtr->setVec3("pointLights[0].specular", pointLight.specular.x, pointLight.specular.y, pointLight.specular.z);
            shaderPtr->setFloat("pointLights[0].constant", pointLight.constant);
            shaderPtr->setFloat("pointLights[0].linear", pointLight.linear);
            shaderPtr->setFloat("pointLights[0].quadratic", pointLight.quadratic);
            
            // Set point light shadow mapping parameters
            if (pointShadowMap) {
                shaderPtr->setFloat("pointLights[0].castShadows", 1.0f);
                shaderPtr->setFloat("pointLights[0].farPlane", 25.0f);
                shaderPtr->setFloat("pointLights[0].shadowBias", pointShadowMap->getShadowBias());
                shaderPtr->setFloat("pointLights[0].softShadows", pointShadowMap->getSoftShadows() ? 1.0f : 0.0f);
                
                // Bind shadow cubemap texture
                pointShadowMap->bindShadowMap(16); // Use texture unit 16 for point shadow cubemap
                shaderPtr->setInt("pointLights[0].shadowCubemap", 16);
            } else {
                shaderPtr->setFloat("pointLights[0].castShadows", 0.0f);
            }
            
            // No spot lights for now
            shaderPtr->setFloat("numSpotLights", 0.0f);

            // PBR frame uniforms (issue #269): set once so meshes selected onto the PBR
            // program (by Material::isPBR, via Model::renderSelectable below) are lit
            // consistently with the Phong scene. Reuses the same directional + point
            // light. Cheap and only matters when a mesh opts into PBR.
            if (pbrShaderPtr) {
                pbrShaderPtr->use();
                pbrShaderPtr->setMat4("view", glm::value_ptr(view));
                pbrShaderPtr->setMat4("projection", glm::value_ptr(projection));
                pbrShaderPtr->setVec3("viewPos", cameraPos.x, cameraPos.y, cameraPos.z);
                pbrShaderPtr->setInt("useDirLight", 1);
                pbrShaderPtr->setVec3("dirLight.direction", dirLight.direction.x, dirLight.direction.y, dirLight.direction.z);
                pbrShaderPtr->setVec3("dirLight.diffuse", dirLight.diffuse.x, dirLight.diffuse.y, dirLight.diffuse.z);
                pbrShaderPtr->setInt("numPointLights", 1);
                pbrShaderPtr->setVec3("pointLights[0].position", pointLight.position.x, pointLight.position.y, pointLight.position.z);
                pbrShaderPtr->setVec3("pointLights[0].diffuse", pointLight.diffuse.x, pointLight.diffuse.y, pointLight.diffuse.z);
                pbrShaderPtr->setFloat("pointLights[0].constant", pointLight.constant);
                pbrShaderPtr->setFloat("pointLights[0].linear", pointLight.linear);
                pbrShaderPtr->setFloat("pointLights[0].quadratic", pointLight.quadratic);

                // Image-based lighting for the ambient term (issue #270). The IBL
                // samplers always point at dedicated units 5-7 (which Mesh::render never
                // uses - it binds material maps to 0-3) so a cube sampler can never
                // alias unit 0's 2D material texture. Textures are bound and useIBL set
                // only when a valid environment exists; otherwise the shader keeps the
                // constant-ambient fallback.
                pbrShaderPtr->setInt("irradianceMap", 5);
                pbrShaderPtr->setInt("prefilterMap", 6);
                pbrShaderPtr->setInt("brdfLUT", 7);
                const bool iblOn = g_ibl && g_ibl->isValid() && g_iblEnabled;
                pbrShaderPtr->setInt("useIBL", iblOn ? 1 : 0);
                pbrShaderPtr->setFloat("iblIntensity", g_iblIntensity);
                if (iblOn) {
                    glActiveTexture(GL_TEXTURE5);
                    glBindTexture(GL_TEXTURE_CUBE_MAP, g_ibl->irradianceMap());
                    glActiveTexture(GL_TEXTURE6);
                    glBindTexture(GL_TEXTURE_CUBE_MAP, g_ibl->prefilterMap());
                    glActiveTexture(GL_TEXTURE7);
                    glBindTexture(GL_TEXTURE_2D, g_ibl->brdfLUT());
                    pbrShaderPtr->setFloat("maxReflectionLod", g_ibl->maxReflectionLod());
                    glActiveTexture(GL_TEXTURE0);
                }
                shaderPtr->use(); // restore the Phong program for the draw loop below
            }

            // Render models if available, otherwise fallback to primitive cubes
            if (modelLoaded && !cubeModel.getMeshes().empty()) {
                // Create a larger grid of objects for better frustum culling demonstration
                std::vector<glm::vec3> cubePositions;
                
                // Generate a 5x5x5 grid of cubes (125 total objects)
                for (int x = -10; x <= 10; x += 4) {
                    for (int y = -10; y <= 10; y += 4) {
                        for (int z = -30; z <= 10; z += 8) {
                            cubePositions.push_back(glm::vec3(x, y, z));
                        }
                    }
                }
                
                int renderedObjects = 0;
                int culledObjects = 0;
                
                for (size_t i = 0; i < cubePositions.size(); i++) {
                    // Calculate individual model matrix for each cube
                    glm::mat4 instanceModel = glm::mat4(1.0f);
                    instanceModel = glm::translate(instanceModel, cubePositions[i]);
                    float angle = 20.0f * i + (float)currentFrameTime * 30.0f;
                    instanceModel = glm::rotate(instanceModel, glm::radians(angle), 
                                          glm::vec3(1.0f, 0.3f, 0.5f));
                    
                    // Perform frustum culling if enabled
                    bool shouldRender = true;
                    if (g_frustumCullingEnabled) {
                        shouldRender = g_frustumCuller.isVisible(cubeModel.getBoundingBox(), instanceModel);
                        if (!shouldRender) {
                            culledObjects++;
                            continue; // Skip rendering this object
                        }
                    }
                    
                    renderedObjects++;
                    glm::mat3 cubeNormalMatrix = glm::mat3(glm::transpose(glm::inverse(instanceModel)));

                    // Render the model instance, selecting the PBR or Blinn-Phong program
                    // per mesh from Material::isPBR (issue #269). renderSelectable sets the
                    // per-mesh matrices on the chosen program; boneless Phong meshes take
                    // the existing path unchanged.
                    cubeModel.renderSelectable(shaderPtr, pbrShaderPtr, instanceModel, cubeNormalMatrix);
                }
                
                // Log culling statistics periodically
                static double lastCullingLog = 0.0;
                if (currentFrameTime - lastCullingLog > 3.0) { // Every 3 seconds
                    if (g_frustumCullingEnabled && (renderedObjects + culledObjects) > 0) {
                        float cullingRatio = (float)culledObjects / (float)(renderedObjects + culledObjects);
                        LOG_INFO("Frustum culling stats - Total: " + std::to_string(renderedObjects + culledObjects) +
                                ", Rendered: " + std::to_string(renderedObjects) + 
                                ", Culled: " + std::to_string(culledObjects) + 
                                " (" + std::to_string(cullingRatio * 100.0f) + "% culled)");
                    }
                    lastCullingLog = currentFrameTime;
                }
            } else {
                // Fallback: render multiple primitive cubes with frustum culling
                std::vector<glm::vec3> cubePositions;
                
                // Generate a 5x5x5 grid of cubes (125 total objects) for primitive fallback
                for (int x = -10; x <= 10; x += 4) {
                    for (int y = -10; y <= 10; y += 4) {
                        for (int z = -30; z <= 10; z += 8) {
                            cubePositions.push_back(glm::vec3(x, y, z));
                        }
                    }
                }
                
                // Create a simple cube bounding box for primitive cubes
                IKore::BoundingBox cubeBounds(glm::vec3(-0.5f), glm::vec3(0.5f));
                
                int renderedObjects = 0;
                int culledObjects = 0;
                
                glBindVertexArray(VAO);
                for (size_t i = 0; i < cubePositions.size(); i++) {
                    // Calculate individual model matrix for each cube
                    glm::mat4 instanceModel = glm::mat4(1.0f);
                    instanceModel = glm::translate(instanceModel, cubePositions[i]);
                    float angle = 20.0f * i + (float)currentFrameTime * 30.0f;
                    instanceModel = glm::rotate(instanceModel, glm::radians(angle), 
                                          glm::vec3(1.0f, 0.3f, 0.5f));
                    
                    // Perform frustum culling if enabled
                    bool shouldRender = true;
                    if (g_frustumCullingEnabled) {
                        shouldRender = g_frustumCuller.isVisible(cubeBounds, instanceModel);
                        if (!shouldRender) {
                            culledObjects++;
                            continue; // Skip rendering this object
                        }
                    }
                    
                    renderedObjects++;
                    glm::mat3 cubeNormalMatrix = glm::mat3(glm::transpose(glm::inverse(instanceModel)));
                    
                    // Update matrices for this cube
                    shaderPtr->setMat4("model", glm::value_ptr(instanceModel));
                    shaderPtr->setMat3("normalMatrix", glm::value_ptr(cubeNormalMatrix));
                    
                    // Render the cube
                    glDrawArrays(GL_TRIANGLES, 0, 36);
                }
                
                // Log culling statistics periodically
                static double lastPrimitiveCullingLog = 0.0;
                if (currentFrameTime - lastPrimitiveCullingLog > 3.0) { // Every 3 seconds
                    if (g_frustumCullingEnabled && (renderedObjects + culledObjects) > 0) {
                        float cullingRatio = (float)culledObjects / (float)(renderedObjects + culledObjects);
                        LOG_INFO("Primitive frustum culling stats - Total: " + std::to_string(renderedObjects + culledObjects) +
                                ", Rendered: " + std::to_string(renderedObjects) + 
                                ", Culled: " + std::to_string(culledObjects) + 
                                " (" + std::to_string(cullingRatio * 100.0f) + "% culled)");
                    }
                    lastPrimitiveCullingLog = currentFrameTime;
                }
                glBindVertexArray(0);
            }
            
            // Unbind textures
            textureManager.unbindAll();
        }

        // The PBR path is now driven per mesh by Material::isPBR via
        // Model::renderSelectable above (issue #269), replacing the hardcoded #234
        // showcase row: a glTF-style asset renders through the PBR program with its
        // imported textures, while Blinn-Phong meshes stay on the existing path.
        });  // end forward pass handler

        frameGraph.setHandler(IKore::render::passes::Particles, [&]() {
            // Render particle systems
            particleManager.renderAll(view, projection);
        });

        frameGraph.setHandler(IKore::render::passes::PostProcess, [&]() {
            // End post-processing frame (applies effects and renders to screen)
            postProcessor.endFrame();
        });

        // Run the registered passes in dependency order.
        frameGraph.execute();

        // Render Entity Debug Overlay (after all scene rendering)
        IKore::getEntityDebugSystem().renderDebugOverlay();

        // Record frame timing for the perf overlay, then draw the Dear ImGui
        // debug overlay on top (both cheap / no-op while hidden).
        debugUI.update(static_cast<float>(deltaTime));
        debugUI.render(static_cast<float>(deltaTime));

        glfwSwapBuffers(window);

        // Frame timing to cap FPS
        auto frameEnd = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = frameEnd - frameStart;
        double sleepTime = targetFrameTime - elapsed.count();
        if (sleepTime > 0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(sleepTime));
        }
    }

    // Cleanup
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);

    // Cleanup entity system
    LOG_INFO("Cleaning up Entity System...");
    entityManager.cleanupAll();
    auto finalStats = entityManager.getStats();
    LOG_INFO("Final entity count: " + std::to_string(finalStats.totalEntities));
    entityManager.clear();

    LOG_INFO("Shutting down IKore Engine");
    g_debugUI = nullptr;
    debugUI.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// Mouse callback function
void mouse_callback(GLFWwindow* /*window*/, double xpos, double ypos) {
    if (!cameraController.isMouseLookEnabled()) {
        return;
    }

    if (firstMouse) {
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        firstMouse = false;
    }

    float xoffset = static_cast<float>(xpos) - lastX;
    float yoffset = lastY - static_cast<float>(ypos); // Reversed since y-coordinates go from bottom to top

    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);

    camera.processMouseMovement(xoffset, yoffset);
}

// Scroll callback function
void scroll_callback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    camera.processMouseScroll(static_cast<float>(yoffset));
}

// Framebuffer size callback function
void framebuffer_size_callback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
    camera.setAspectRatio(static_cast<float>(width) / static_cast<float>(height));
    if (g_postProcessor) {
        g_postProcessor->resize(width, height);
    }
}

// (Re)generate the IBL environment from the current skybox cubemap (issue #288). Called at
// load and whenever the skybox changes so the PBR ambient stays in sync with the
// environment. A no-op when there is no loaded skybox, in which case PBR keeps the
// constant-ambient fallback (g_ibl stays not-valid).
void regenerateIblFromSkybox() {
    if (!g_ibl || !g_skybox || !g_skybox->isLoaded()) return;
    if (g_ibl->generate(g_skybox->getTextureID())) {
        LOG_INFO("IBL environment (re)generated from skybox");
    } else {
        LOG_WARNING("IBL generation failed - PBR uses constant ambient");
    }
}

// Keyboard callback for toggling post-processing effects
void key_callback(GLFWwindow* /*window*/, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_F1 && g_debugUI) {
            // Toggle the in-engine debug UI overlay
            g_debugUI->toggle();
            LOG_INFO(std::string("Debug UI ") + (g_debugUI->isVisible() ? "shown" : "hidden"));
        }
        if (key == GLFW_KEY_1 && g_postProcessor) {
            // Toggle Bloom
            auto* bloom = g_postProcessor->getBloomEffect();
            if (bloom) {
                bloom->setEnabled(!bloom->isEnabled());
                LOG_INFO("Bloom effect " + std::string(bloom->isEnabled() ? "enabled" : "disabled"));
            }
        }
        else if (key == GLFW_KEY_2 && g_postProcessor) {
            // Toggle FXAA
            auto* fxaa = g_postProcessor->getFXAAEffect();
            if (fxaa) {
                fxaa->setEnabled(!fxaa->isEnabled());
                LOG_INFO("FXAA effect " + std::string(fxaa->isEnabled() ? "enabled" : "disabled"));
            }
        }
        else if (key == GLFW_KEY_3 && g_postProcessor) {
            // Toggle SSAO
            auto* ssao = g_postProcessor->getSSAOEffect();
            if (ssao) {
                ssao->setEnabled(!ssao->isEnabled());
                LOG_INFO("SSAO effect " + std::string(ssao->isEnabled() ? "enabled" : "disabled"));
            }
        }
        else if (key == GLFW_KEY_H && g_postProcessor) {
            // Toggle HDR ACES tone-mapping resolve (issue #235). Off by default; the
            // LDR effect chain is the fallback.
            const bool on = !g_postProcessor->isToneMapEnabled();
            g_postProcessor->setToneMapEnabled(on);
            LOG_INFO(std::string("HDR ACES tone mapping ") + (on ? "enabled" : "disabled"));
        }
        else if ((key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) && g_postProcessor) {
            // Increase exposure.
            g_postProcessor->setExposure(g_postProcessor->getExposure() * 1.25f);
            LOG_INFO("Exposure: " + std::to_string(g_postProcessor->getExposure()));
        }
        else if ((key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) && g_postProcessor) {
            // Decrease exposure.
            g_postProcessor->setExposure(g_postProcessor->getExposure() / 1.25f);
            LOG_INFO("Exposure: " + std::to_string(g_postProcessor->getExposure()));
        }
        else if (key == GLFW_KEY_4 && g_skybox) {
            // Toggle Skybox
            g_skybox->setEnabled(!g_skybox->isEnabled());
            LOG_INFO("Skybox " + std::string(g_skybox->isEnabled() ? "enabled" : "disabled"));
            // The environment changed: refresh IBL from the current skybox cubemap (issue #288).
            if (g_skybox->isEnabled()) regenerateIblFromSkybox();
        }
        else if (key == GLFW_KEY_5 && g_skybox) {
            // Decrease skybox intensity
            float currentIntensity = g_skybox->getIntensity();
            float newIntensity = std::max(0.1f, currentIntensity - 0.1f);
            g_skybox->setIntensity(newIntensity);
            LOG_INFO("Skybox intensity decreased to: " + std::to_string(newIntensity));
        }
        else if (key == GLFW_KEY_6 && g_skybox) {
            // Increase skybox intensity
            float currentIntensity = g_skybox->getIntensity();
            float newIntensity = std::min(3.0f, currentIntensity + 0.1f);
            g_skybox->setIntensity(newIntensity);
            LOG_INFO("Skybox intensity increased to: " + std::to_string(newIntensity));
        }
        else if (key == GLFW_KEY_7 && g_particleManager) {
            // Toggle particle systems
            g_particleManager->setGlobalEnabled(!g_particleManager->isGlobalEnabled());
            LOG_INFO("Particle systems " + std::string(g_particleManager->isGlobalEnabled() ? "enabled" : "disabled"));
        }
        else if (key == GLFW_KEY_8 && g_particleManager) {
            // Create fire effect at camera position
            glm::vec3 firePos = camera.getPosition() + camera.getFront() * 2.0f;
            g_particleManager->createFireEffect(firePos);
            LOG_INFO("Fire effect created at camera position");
        }
        else if (key == GLFW_KEY_9 && g_particleManager) {
            // Create explosion effect at camera position
            glm::vec3 explosionPos = camera.getPosition() + camera.getFront() * 3.0f;
            g_particleManager->createExplosionEffect(explosionPos);
            LOG_INFO("Explosion effect created at camera position");
        }
        else if (key == GLFW_KEY_0 && g_particleManager) {
            // Create smoke effect at camera position
            glm::vec3 smokePos = camera.getPosition() + camera.getFront() * 2.0f;
            g_particleManager->createSmokeEffect(smokePos);
            LOG_INFO("Smoke effect created at camera position");
        }
        else if (key == GLFW_KEY_MINUS && g_particleManager) {
            // Create sparks effect at camera position
            glm::vec3 sparksPos = camera.getPosition() + camera.getFront() * 2.0f;
            g_particleManager->createSparksEffect(sparksPos);
            LOG_INFO("Sparks effect created at camera position");
        }
        else if (key == GLFW_KEY_F1 && g_shadowManager) {
            // Toggle shadows globally
            g_shadowManager->setEnabled(!g_shadowManager->isEnabled());
            LOG_INFO("Shadow mapping " + std::string(g_shadowManager->isEnabled() ? "enabled" : "disabled"));
        }
        else if (key == GLFW_KEY_F2 && g_shadowManager) {
            // Cycle shadow quality (Low, Medium, High)
            static int qualityLevel = 1; // 0=Low, 1=Medium, 2=High
            qualityLevel = (qualityLevel + 1) % 3;
            float quality = qualityLevel == 0 ? 0.3f : (qualityLevel == 1 ? 0.6f : 1.0f);
            g_shadowManager->setShadowQuality(quality);
            std::string qualityName = qualityLevel == 0 ? "Low" : (qualityLevel == 1 ? "Medium" : "High");
            LOG_INFO("Shadow quality set to: " + qualityName);
        }
        else if (key == GLFW_KEY_K && g_cascadedShadowMap) {
            // Toggle cascaded shadow maps for the directional light (issue #265).
            g_useCascadedShadows = !g_useCascadedShadows;
            LOG_INFO("Cascaded shadow maps " + std::string(g_useCascadedShadows ? "enabled" : "disabled") +
                     " (" + std::to_string(g_cascadedShadowMap->cascadeCount()) + " cascades)");
        }
        else if (key == GLFW_KEY_J && g_cascadedShadowMap) {
            // Cycle the cascade split lambda (uniform <-> logarithmic split blend).
            static const float lambdas[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
            static int li = 2; // starts at 0.5
            li = (li + 1) % 5;
            g_cascadeSplitLambda = lambdas[li];
            LOG_INFO("Cascade split lambda: " + std::to_string(g_cascadeSplitLambda));
        }
        else if (key == GLFW_KEY_B && g_ibl && g_ibl->isValid()) {
            // Toggle image-based lighting for the PBR ambient term (issue #270).
            g_iblEnabled = !g_iblEnabled;
            LOG_INFO("Image-based lighting " + std::string(g_iblEnabled ? "enabled" : "disabled"));
        }
        else if (key == GLFW_KEY_LEFT_BRACKET) {
            // Decrease the IBL ambient intensity/exposure (issue #288).
            g_iblIntensity = std::max(0.0f, g_iblIntensity - 0.1f);
            LOG_INFO("IBL intensity: " + std::to_string(g_iblIntensity));
        }
        else if (key == GLFW_KEY_RIGHT_BRACKET) {
            // Increase the IBL ambient intensity/exposure (issue #288).
            g_iblIntensity = std::min(4.0f, g_iblIntensity + 0.1f);
            LOG_INFO("IBL intensity: " + std::to_string(g_iblIntensity));
        }
        else if (key == GLFW_KEY_C) {
            // Toggle frustum culling
            g_frustumCullingEnabled = !g_frustumCullingEnabled;
            LOG_INFO("Frustum culling " + std::string(g_frustumCullingEnabled ? "enabled" : "disabled"));
        }
        else if (key == GLFW_KEY_E) {
            // Show entity system statistics
            IKore::EntityManager& entityMgr = IKore::getEntityManager();
            auto stats = entityMgr.getStats();
            LOG_INFO("Entity System Statistics:");
            LOG_INFO("  Total entities: " + std::to_string(stats.totalEntities));
            LOG_INFO("  Valid entities: " + std::to_string(stats.validEntities));
            LOG_INFO("  Invalid entities: " + std::to_string(stats.invalidEntities));
            LOG_INFO("  ID range: " + std::to_string(stats.lowestID) + " - " + std::to_string(stats.highestID));
            
            // List all entities by type
            auto gameObjects = entityMgr.getEntitiesOfType<IKore::GameObject>();
            auto lights = entityMgr.getEntitiesOfType<IKore::LightEntity>();
            auto cameras = entityMgr.getEntitiesOfType<IKore::EnhancedCameraEntity>();
            auto testEntities = entityMgr.getEntitiesOfType<IKore::TestEntity>();
            
            LOG_INFO("  GameObjects: " + std::to_string(gameObjects.size()));
            LOG_INFO("  Lights: " + std::to_string(lights.size()));
            LOG_INFO("  Cameras: " + std::to_string(cameras.size()));
            LOG_INFO("  Test entities: " + std::to_string(testEntities.size()));
        }
        else if (key == GLFW_KEY_R) {
            // Remove test entities
            IKore::EntityManager& entityMgr = IKore::getEntityManager();
            auto testEntities = entityMgr.getEntitiesOfType<IKore::TestEntity>();
            size_t removed = 0;
            
            for (auto& entity : testEntities) {
                if (entityMgr.removeEntity(entity)) {
                    removed++;
                }
            }
            
            LOG_INFO("Removed " + std::to_string(removed) + " test entities");
        }
        else if (key == GLFW_KEY_T) {
            // Create new test entity
            IKore::EntityManager& entityMgr = IKore::getEntityManager();
            static int testCounter = 0;
            testCounter++;
            
            auto newEntity = entityMgr.createEntity<IKore::TestEntity>("Dynamic Test Entity " + std::to_string(testCounter));
            newEntity->setLifetime(5.0f + (testCounter % 5)); // Varying lifetimes
            newEntity->initialize();
            
            LOG_INFO("Created new test entity with ID: " + std::to_string(newEntity->getID()));
        }
        // Transform Component controls
        else if (key == GLFW_KEY_Y) {
            // Rotate parent object
            static IKore::EntityManager& entityMgr = IKore::getEntityManager();
            entityMgr.forEach([](std::shared_ptr<IKore::Entity> entity) {
                auto transformableObj = std::dynamic_pointer_cast<IKore::TransformableGameObject>(entity);
                if (transformableObj && transformableObj->getName() == "Parent Object") {
                    transformableObj->getTransform().rotate(0.0f, 15.0f, 0.0f);
                    glm::vec3 pos = transformableObj->getTransform().getWorldPosition();
                    LOG_INFO("Rotated parent object - World position: (" + 
                            std::to_string(pos.x) + ", " + std::to_string(pos.y) + ", " + std::to_string(pos.z) + ")");
                }
            });
        }
        else if (key == GLFW_KEY_U) {
            // Scale parent object
            static IKore::EntityManager& entityMgr = IKore::getEntityManager();
            entityMgr.forEach([](std::shared_ptr<IKore::Entity> entity) {
                auto transformableObj = std::dynamic_pointer_cast<IKore::TransformableGameObject>(entity);
                if (transformableObj && transformableObj->getName() == "Parent Object") {
                    glm::vec3 currentScale = transformableObj->getTransform().getScale();
                    float newScale = currentScale.x > 3.0f ? 0.5f : currentScale.x * 1.2f;
                    transformableObj->getTransform().setScale(newScale);
                    LOG_INFO("Scaled parent object to: " + std::to_string(newScale));
                }
            });
        }
        else if (key == GLFW_KEY_I) {
            // Move transformable light
            static IKore::EntityManager& entityMgr = IKore::getEntityManager();
            entityMgr.forEach([](std::shared_ptr<IKore::Entity> entity) {
                auto light = std::dynamic_pointer_cast<IKore::TransformableLight>(entity);
                if (light && light->getName() == "Transformable Point Light") {
                    light->getTransform().translate(1.0f, 0.0f, 0.0f);
                    glm::vec3 pos = light->getTransform().getWorldPosition();
                    LOG_INFO("Moved light to: (" + 
                            std::to_string(pos.x) + ", " + std::to_string(pos.y) + ", " + std::to_string(pos.z) + ")");
                }
            });
        }
        else if (key == GLFW_KEY_O) {
            // Move transformable camera
            static IKore::EntityManager& entityMgr = IKore::getEntityManager();
            entityMgr.forEach([](std::shared_ptr<IKore::Entity> entity) {
                auto camera = std::dynamic_pointer_cast<IKore::TransformableCamera>(entity);
                if (camera && camera->getName() == "Transformable Camera") {
                    camera->getTransform().translate(0.0f, 0.0f, -1.0f);
                    glm::vec3 pos = camera->getTransform().getWorldPosition();
                    LOG_INFO("Moved camera to: (" + 
                            std::to_string(pos.x) + ", " + std::to_string(pos.y) + ", " + std::to_string(pos.z) + ")");
                }
            });
        }
        else if (key == GLFW_KEY_P) {
            // Print transform hierarchy information
            static IKore::EntityManager& entityMgr = IKore::getEntityManager();
            LOG_INFO("=== Transform Hierarchy Status ===");
            entityMgr.forEach([](std::shared_ptr<IKore::Entity> entity) {
                auto transformableObj = std::dynamic_pointer_cast<IKore::TransformableGameObject>(entity);
                if (transformableObj) {
                    glm::vec3 localPos = transformableObj->getTransform().getPosition();
                    glm::vec3 worldPos = transformableObj->getTransform().getWorldPosition();
                    glm::vec3 scale = transformableObj->getTransform().getScale();
                    glm::vec3 rotation = transformableObj->getTransform().getRotation();
                    
                    LOG_INFO("Object: " + transformableObj->getName());
                    LOG_INFO("  Local Pos: (" + std::to_string(localPos.x) + ", " + std::to_string(localPos.y) + ", " + std::to_string(localPos.z) + ")");
                    LOG_INFO("  World Pos: (" + std::to_string(worldPos.x) + ", " + std::to_string(worldPos.y) + ", " + std::to_string(worldPos.z) + ")");
                    LOG_INFO("  Scale: (" + std::to_string(scale.x) + ", " + std::to_string(scale.y) + ", " + std::to_string(scale.z) + ")");
                    LOG_INFO("  Rotation: (" + std::to_string(rotation.x) + ", " + std::to_string(rotation.y) + ", " + std::to_string(rotation.z) + ")");
                    LOG_INFO("  Children: " + std::to_string(transformableObj->getChildren().size()));
                }
            });
        }
        // Input Component controls
        else if (key == GLFW_KEY_F1) {
            // Toggle FPS Camera control
            static IKore::EntityManager& entityMgr = IKore::getEntityManager();
            entityMgr.forEach([](std::shared_ptr<IKore::Entity> entity) {
                auto camera = std::dynamic_pointer_cast<IKore::ControllableCamera>(entity);
                if (camera && camera->getName() == "FPS Camera") {
                    camera->setInputEnabled(!camera->isInputEnabled());
                    LOG_INFO("FPS Camera control " + std::string(camera->isInputEnabled() ? "enabled" : "disabled"));
                    
                    // Disable other controllers when this is enabled
                    if (camera->isInputEnabled()) {
                        entityMgr.forEach([](std::shared_ptr<IKore::Entity> other) {
                            auto player = std::dynamic_pointer_cast<IKore::ControllableGameObject>(other);
                            if (player && player->getName() == "Controllable Player") {
                                player->setInputEnabled(false);
                                player->setMouseCapture(false);
                            }
                            auto light = std::dynamic_pointer_cast<IKore::ControllableLight>(other);
                            if (light && light->getName() == "Controllable Light") {
                                light->setInputEnabled(false);
                            }
                        });
                    }
                }
            });
        }
        else if (key == GLFW_KEY_F2) {
            // Toggle Light control
            static IKore::EntityManager& entityMgr = IKore::getEntityManager();
            entityMgr.forEach([](std::shared_ptr<IKore::Entity> entity) {
                auto light = std::dynamic_pointer_cast<IKore::ControllableLight>(entity);
                if (light && light->getName() == "Controllable Light") {
                    light->setInputEnabled(!light->isInputEnabled());
                    LOG_INFO("Controllable Light control " + std::string(light->isInputEnabled() ? "enabled" : "disabled"));
                    
                    // Disable other controllers when this is enabled
                    if (light->isInputEnabled()) {
                        entityMgr.forEach([](std::shared_ptr<IKore::Entity> other) {
                            auto player = std::dynamic_pointer_cast<IKore::ControllableGameObject>(other);
                            if (player && player->getName() == "Controllable Player") {
                                player->setInputEnabled(false);
                                player->setMouseCapture(false);
                            }
                            auto camera = std::dynamic_pointer_cast<IKore::ControllableCamera>(other);
                            if (camera && camera->getName() == "FPS Camera") {
                                camera->setInputEnabled(false);
                            }
                        });
                    }
                }
            });
        }
        else if (key == GLFW_KEY_F3) {
            // Toggle Player control
            static IKore::EntityManager& entityMgr = IKore::getEntityManager();
            entityMgr.forEach([](std::shared_ptr<IKore::Entity> entity) {
                auto player = std::dynamic_pointer_cast<IKore::ControllableGameObject>(entity);
                if (player && player->getName() == "Controllable Player") {
                    player->setInputEnabled(!player->isInputEnabled());
                    LOG_INFO("Controllable Player control " + std::string(player->isInputEnabled() ? "enabled" : "disabled"));
                    
                    // Disable other controllers when this is enabled
                    if (player->isInputEnabled()) {
                        entityMgr.forEach([](std::shared_ptr<IKore::Entity> other) {
                            auto camera = std::dynamic_pointer_cast<IKore::ControllableCamera>(other);
                            if (camera && camera->getName() == "FPS Camera") {
                                camera->setInputEnabled(false);
                            }
                            auto light = std::dynamic_pointer_cast<IKore::ControllableLight>(other);
                            if (light && light->getName() == "Controllable Light") {
                                light->setInputEnabled(false);
                            }
                        });
                    }
                }
            });
        }
        // Camera Component controls
        else if (key == GLFW_KEY_4 && g_enhancedCamera) {
            // Set Free Camera mode
            g_enhancedCamera->setCameraType(IKore::CameraComponent::CameraType::FREE_CAMERA, g_cameraTransitionDuration);
            g_currentCameraMode = 0;
            LOG_INFO("Camera Component: Free Camera mode");
        }
        else if (key == GLFW_KEY_5 && g_enhancedCamera && g_followTarget) {
            // Set Third-Person mode
            g_enhancedCamera->setFollowTarget(g_followTarget);
            g_enhancedCamera->setCameraType(IKore::CameraComponent::CameraType::THIRD_PERSON, g_cameraTransitionDuration);
            g_currentCameraMode = 1;
            LOG_INFO("Camera Component: Third-Person mode (following target)");
        }
        else if (key == GLFW_KEY_6 && g_enhancedCamera && g_followTarget) {
            // Set Orbital mode
            g_enhancedCamera->setFollowTarget(g_followTarget);
            g_enhancedCamera->setCameraType(IKore::CameraComponent::CameraType::ORBITAL, g_cameraTransitionDuration);
            g_currentCameraMode = 2;
            LOG_INFO("Camera Component: Orbital mode (orbiting around target)");
        }
        else if (key == GLFW_KEY_7 && g_enhancedCamera && g_followTarget) {
            // Set First-Person mode
            g_enhancedCamera->setFollowTarget(g_followTarget);
            g_enhancedCamera->setCameraType(IKore::CameraComponent::CameraType::FIRST_PERSON, g_cameraTransitionDuration);
            g_currentCameraMode = 3;
            LOG_INFO("Camera Component: First-Person mode (attached to target)");
        }
        else if (key == GLFW_KEY_C) {
            // Toggle between old camera system and Camera Component
            g_useCameraComponent = !g_useCameraComponent;
            LOG_INFO("Camera System: " + std::string(g_useCameraComponent ? 
                    "Using Camera Component" : "Using old camera system"));
            if (g_useCameraComponent && g_enhancedCamera) {
                LOG_INFO("Camera Component Debug Info:\n" + g_enhancedCamera->getDebugInfo());
            }
        }
        else if (key == GLFW_KEY_T) {
            // Increase transition duration
            g_cameraTransitionDuration += 0.5f;
            if (g_cameraTransitionDuration > 5.0f) g_cameraTransitionDuration = 5.0f;
            LOG_INFO("Camera transition duration: " + std::to_string(g_cameraTransitionDuration) + " seconds");
        }
        else if (key == GLFW_KEY_G) {
            // Decrease transition duration
            g_cameraTransitionDuration -= 0.5f;
            if (g_cameraTransitionDuration < 0.0f) g_cameraTransitionDuration = 0.0f;
            LOG_INFO("Camera transition duration: " + std::to_string(g_cameraTransitionDuration) + " seconds");
        }
        // Serialization controls
        else if (key == GLFW_KEY_F11) {
            // Save scene to JSON format
            IKore::SerializationManager& manager = IKore::SerializationManager::getInstance();
            try {
                manager.saveScene("saved_scene.json");
                LOG_INFO("Scene saved to saved_scene.json (JSON format)");
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to save scene: " + std::string(e.what()));
            }
        }
        else if (key == GLFW_KEY_F12) {
            // Save scene to binary format
            IKore::SerializationManager& manager = IKore::SerializationManager::getInstance();
            try {
                manager.saveScene("saved_scene.bin");
                LOG_INFO("Scene saved to saved_scene.bin (Binary format)");
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to save scene: " + std::string(e.what()));
            }
        }
        else if (key == GLFW_KEY_F9) {
            // Load scene (auto-detects format)
            IKore::SerializationManager& manager = IKore::SerializationManager::getInstance();
            std::string filename = "saved_scene.json"; // Default to JSON
            
            // Check if binary file exists
            std::ifstream binFile("saved_scene.bin");
            if (binFile.good()) {
                binFile.close();
                filename = "saved_scene.bin";
            }
            
            try {
                manager.loadScene(filename);
                LOG_INFO("Scene loaded from " + filename);
                
                // Re-initialize all loaded entities
                IKore::EntityManager& entityMgr = IKore::getEntityManager();
                entityMgr.initializeAll();
                LOG_INFO("All loaded entities initialized");
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to load scene: " + std::string(e.what()));
            }
        }
        else if (key == GLFW_KEY_F10) {
            // Clear current scene
            IKore::EntityManager& entityMgr = IKore::getEntityManager();
            LOG_INFO("Clearing current scene...");
            entityMgr.clear();
            LOG_INFO("Scene cleared - all entities destroyed");
        }
        else if (key == GLFW_KEY_F8) {
            // Toggle Entity Debug System
            IKore::getEntityDebugSystem().toggleDebugMode();
        }
    }
}

// Function to render scene objects (for both shadow pass and main rendering)
void renderSceneObjects(std::shared_ptr<IKore::Shader> shader, GLuint VAO, bool modelLoaded,
                       const IKore::Model& cubeModel, double currentFrameTime,
                       std::shared_ptr<IKore::Shader> skinnedShadowShader,
                       const std::vector<glm::mat4>* bonePalette) {
    // Create a larger grid of objects for better frustum culling demonstration
    std::vector<glm::vec3> cubePositions;
    
    // Generate a 5x5x5 grid of cubes (125 total objects)
    for (int x = -10; x <= 10; x += 4) {
        for (int y = -10; y <= 10; y += 4) {
            for (int z = -30; z <= 10; z += 8) {
                cubePositions.push_back(glm::vec3(x, y, z));
            }
        }
    }
    
    // Render models if available, otherwise fallback to primitive cubes
    if (modelLoaded && !cubeModel.getMeshes().empty()) {
        // Shadow pass: choose the skinned depth program for a mesh with bones so its
        // shadow follows the animated pose, else the static program (issue #266). The
        // choice mirrors the forward pass's skinned-vs-static shader selection.
        const bool useSkinned =
            IKore::render::shadowProgramFor(cubeModel.hasBones()) == IKore::render::ShadowProgram::Skinned &&
            skinnedShadowShader != nullptr;
        std::shared_ptr<IKore::Shader> activeShader = useSkinned ? skinnedShadowShader : shader;
        activeShader->use();

        // Upload the bone palette once for the skinned program (clamped to the shader's
        // finalBonesMatrices[MAX_BONES] capacity); the shaders' rigid fallback covers a
        // missing palette.
        if (useSkinned && bonePalette != nullptr) {
            const int count =
                IKore::render::shadowPaletteUploadCount(static_cast<int>(bonePalette->size()));
            for (int b = 0; b < count; ++b) {
                activeShader->setMat4(("finalBonesMatrices[" + std::to_string(b) + "]").c_str(),
                                      glm::value_ptr((*bonePalette)[static_cast<std::size_t>(b)]));
            }
        }

        for (size_t i = 0; i < cubePositions.size(); i++) {
            // Calculate individual model matrix for each cube
            glm::mat4 instanceModel = glm::mat4(1.0f);
            instanceModel = glm::translate(instanceModel, cubePositions[i]);
            float angle = 20.0f * i + (float)currentFrameTime * 30.0f;
            instanceModel = glm::rotate(instanceModel, glm::radians(angle),
                                  glm::vec3(1.0f, 0.3f, 0.5f));

            glm::mat3 cubeNormalMatrix = glm::mat3(glm::transpose(glm::inverse(instanceModel)));

            // Set matrices for this instance
            activeShader->setMat4("model", glm::value_ptr(instanceModel));
            activeShader->setMat3("normalMatrix", glm::value_ptr(cubeNormalMatrix));

            // Render the model
            cubeModel.render(activeShader);
        }
    } else {
        // Fallback: render primitive cubes using VAO
        glBindVertexArray(VAO);
        
        for (size_t i = 0; i < cubePositions.size(); i++) {
            // Calculate individual model matrix for each cube
            glm::mat4 instanceModel = glm::mat4(1.0f);
            instanceModel = glm::translate(instanceModel, cubePositions[i]);
            float angle = 20.0f * i + (float)currentFrameTime * 30.0f;
            instanceModel = glm::rotate(instanceModel, glm::radians(angle), 
                                  glm::vec3(1.0f, 0.3f, 0.5f));
            
            glm::mat3 cubeNormalMatrix = glm::mat3(glm::transpose(glm::inverse(instanceModel)));
            
            // Set matrices for this instance
            shader->setMat4("model", glm::value_ptr(instanceModel));
            shader->setMat3("normalMatrix", glm::value_ptr(cubeNormalMatrix));
            
            // Draw the cube
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
        
        glBindVertexArray(0);
    }
}
