cmake_minimum_required(VERSION 3.10)
cmake_policy(VERSION 3.10)

# Set modern OpenGL preference policy
if(POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()

project(IKoreEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Register unit tests with CTest so the headless suite can gate PRs (see the
# HEADLESS_TESTS block at the end of this file and .github/workflows/tests.yml).
enable_testing()

include(FetchContent)

# GLFW
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.3.8
)
FetchContent_MakeAvailable(glfw)

# GLAD
set(GLAD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG        v0.1.36
)
FetchContent_MakeAvailable(glad)

# GLM
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        0.9.9.8
)
FetchContent_MakeAvailable(glm)

# stb_image
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        f58f558c120e9b32c217290b80bad1a0729fbb2c
)
FetchContent_MakeAvailable(stb)
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${stb_SOURCE_DIR})

# Assimp
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp.git
  GIT_TAG        v5.3.1
)
FetchContent_MakeAvailable(assimp)

# Bullet Physics
set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_BULLET3 OFF CACHE BOOL "" FORCE)
set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(BUILD_OPENGL3_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
set(INSTALL_LIBS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  bullet3
  GIT_REPOSITORY https://github.com/bulletphysics/bullet3.git
  GIT_TAG        3.25
)
FetchContent_MakeAvailable(bullet3)

# Dear ImGui (docking branch) for the in-engine editor/debug UI (#144).
# ImGui ships as source with no CMakeLists, so MakeAvailable only populates it;
# its sources and the GLFW + OpenGL3 backends are compiled into the IKore target.
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)
set(IMGUI_SOURCES
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# Lua + sol2 for the scripting layer (#145).
# The lua/lua repo ships no CMakeLists, so MakeAvailable only populates it and we
# build the library ourselves from its .c files (excluding the standalone
# interpreter lua.c, the compiler luac.c, and the all-in-one amalgam onelua.c).
FetchContent_Declare(
  lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG        v5.4.6
)
FetchContent_MakeAvailable(lua)
file(GLOB LUA_LIB_SOURCES "${lua_SOURCE_DIR}/*.c")
list(REMOVE_ITEM LUA_LIB_SOURCES
  "${lua_SOURCE_DIR}/lua.c"
  "${lua_SOURCE_DIR}/luac.c"
  "${lua_SOURCE_DIR}/onelua.c")
add_library(lua_static STATIC ${LUA_LIB_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR})
set_target_properties(lua_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

# sol2 is header-only; populate it and use its include dir directly (avoid running
# sol2's own CMake, which would try to locate a Lua install for its targets).
FetchContent_Declare(
  sol2
  GIT_REPOSITORY https://github.com/ThePhd/sol2.git
  GIT_TAG        v3.3.0
)
FetchContent_GetProperties(sol2)
if(NOT sol2_POPULATED)
  FetchContent_Populate(sol2)
endif()

# OpenAL for 3D Audio
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(OPENAL openal)
endif()

if(NOT OPENAL_FOUND)
    # Try to find OpenAL manually
    find_path(OPENAL_INCLUDE_DIR AL/al.h
        HINTS
        /usr/include
        /usr/local/include
        /opt/local/include
    )
    
    find_library(OPENAL_LIBRARY
        NAMES openal OpenAL32
        HINTS
        /usr/lib
        /usr/local/lib
        /opt/local/lib
    )
    
    if(OPENAL_INCLUDE_DIR AND OPENAL_LIBRARY)
        set(OPENAL_FOUND TRUE)
        set(OPENAL_LIBRARIES ${OPENAL_LIBRARY})
        set(OPENAL_INCLUDE_DIRS ${OPENAL_INCLUDE_DIR})
    endif()
endif()

if(OPENAL_FOUND)
    message(STATUS "Found OpenAL: ${OPENAL_LIBRARIES}")
    # Set preprocessor definition for conditional compilation
    add_definitions(-DOPENAL_FOUND=1)
else()
    message(WARNING "OpenAL not found. Audio system will be compiled in fallback mode without OpenAL libraries.")
    # Create dummy variables for compilation
    set(OPENAL_LIBRARIES "")
    set(OPENAL_INCLUDE_DIRS "")
    add_definitions(-DOPENAL_FOUND=0)
endif()

find_package(OpenGL REQUIRED)

# Add include directories for the new folder structure
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
# Repo root on the include path so tests using "src/..."-style includes resolve
# regardless of where the test source lives (e.g. tests/).
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Add Bullet Physics include directories
include_directories(${bullet3_SOURCE_DIR}/src)

# Add OpenAL include directories
if(OPENAL_FOUND)
    include_directories(${OPENAL_INCLUDE_DIRS})
endif()

# Define source files explicitly
set(CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/EntityPool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/PooledEntityManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/EntityTypes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Transform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/TransformableEntities.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/EntityDebugSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Component.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/EventSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/MeshComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/MaterialComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/PhysicsComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/AudioComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/AIComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/AnimationComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/NetworkComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/LODComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/AISystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/AnimationLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/NetworkSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/ECSBenchmark.cpp
)

set(AUDIO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/OpenALAudioEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/AudioSystem3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/AudioDecodeCompressed.cpp
)

set(SCENE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/SceneGraphSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/SceneManager.cpp
)

set(ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DeltaTimeDemo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PostProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Camera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CameraController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Skybox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ParticleSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ShadowMap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CascadedShadowMap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/Ibl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Frustum.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/InputComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ControllableEntities.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CameraComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EnhancedCameraEntity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Serialization.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/DebugUI.cpp
)

add_executable(IKore ${ENGINE_SOURCES} ${CORE_SOURCES} ${SCENE_SOURCES} ${AUDIO_SOURCES} ${IMGUI_SOURCES})

# Dear ImGui headers (core + backends) for the IKore target (#144)
target_include_directories(IKore PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

# Scene Graph Test executable
add_executable(test_scene_graph 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tests/test_scene_graph.cpp 
    ${CORE_SOURCES}
    ${SCENE_SOURCES}
    ${AUDIO_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Frustum.cpp
)

# Scene Graph Demo executable
add_executable(scene_graph_demo 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/demos/scene_graph_demo.cpp 
    ${CORE_SOURCES}
    ${SCENE_SOURCES}
    ${AUDIO_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Frustum.cpp
)

# MeshComponent Test executable
add_executable(test_mesh_component 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tests/test_mesh_component.cpp 
    ${CORE_SOURCES}
    ${AUDIO_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Frustum.cpp
)

# MeshComponent Minimal Test executable  
add_executable(test_mesh_component_minimal 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tests/test_mesh_component_minimal.cpp 
    ${CORE_SOURCES}
    ${AUDIO_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Frustum.cpp
)

# MeshComponent Geometry Test executable  
add_executable(test_mesh_component_geometry 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tests/test_mesh_component_geometry.cpp 
    ${CORE_SOURCES}
    ${AUDIO_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Frustum.cpp
)

# MaterialComponent Test executable
add_executable(test_material_component 
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_material_component.cpp 
    ${CORE_SOURCES}
    ${AUDIO_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Frustum.cpp
)

# PhysicsComponent Test executable (simplified)
add_executable(test_physics_component 
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_physics_component.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Component.cpp
)

# PhysicsComponent Simple Test executable
add_executable(test_physics_simple 
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_physics_simple.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Component.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/PhysicsComponent.cpp
)

# ECS storage benchmark: legacy model vs archetype ECS (Milestone 8 / #143).
# Self-contained and header-only (the legacy model is mirrored in the source),
# so no extra engine sources or deps are needed.
add_executable(test_ecs_benchmark
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ecs_benchmark.cpp
)

# OpenAL 3D Audio Test executable
add_executable(test_openal_3d_audio 
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_openal_3d_audio.cpp 
    # Minimal core sources needed for audio testing (excluding graphics dependencies)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/EntityTypes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Transform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/TransformableEntities.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/EntityDebugSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Component.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/AudioComponent.cpp
    ${AUDIO_SOURCES}
)

# Note: Placeholder test files removed from build until implemented
# These were causing linker errors due to missing main() functions:
# - simple_scene_test.cpp
# - logger_test.cpp 
# - filesystem_test.cpp
# - debug_logger_test.cpp

# EventSystem Test executable
add_executable(test_event_system 
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_event_system.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/EventSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Component.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Logger.cpp
)
target_link_libraries(test_event_system PRIVATE glm::glm)

# EventSystem Simple Test executable
add_executable(test_event_system_simple
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_event_system_simple.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/EventSystem.cpp
)
target_link_libraries(test_event_system_simple PRIVATE glm::glm)

target_link_libraries(IKore PRIVATE glfw glad glm stb_image assimp OpenGL::GL BulletDynamics BulletCollision LinearMath ${OPENAL_LIBRARIES})
target_link_libraries(test_scene_graph PRIVATE glfw glad glm stb_image assimp OpenGL::GL BulletDynamics BulletCollision LinearMath ${OPENAL_LIBRARIES})
target_link_libraries(scene_graph_demo PRIVATE glfw glad glm stb_image assimp OpenGL::GL BulletDynamics BulletCollision LinearMath ${OPENAL_LIBRARIES})
target_link_libraries(test_mesh_component PRIVATE glfw glad glm stb_image assimp OpenGL::GL BulletDynamics BulletCollision LinearMath ${OPENAL_LIBRARIES})
target_link_libraries(test_mesh_component_minimal PRIVATE glfw glad glm stb_image assimp OpenGL::GL BulletDynamics BulletCollision LinearMath ${OPENAL_LIBRARIES})
target_link_libraries(test_mesh_component_geometry PRIVATE glfw glad glm stb_image assimp OpenGL::GL BulletDynamics BulletCollision LinearMath ${OPENAL_LIBRARIES})
target_link_libraries(test_material_component PRIVATE glfw glad glm stb_image assimp OpenGL::GL BulletDynamics BulletCollision LinearMath ${OPENAL_LIBRARIES})
target_link_libraries(test_physics_component PRIVATE glm)
target_link_libraries(test_physics_simple PRIVATE BulletDynamics BulletCollision LinearMath glm)
target_link_libraries(test_openal_3d_audio PRIVATE glm ${OPENAL_LIBRARIES})

# AI Component Test executable
add_executable(test_ai_component 
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ai_component.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Component.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/AIComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/AISystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Logger.cpp
)

target_link_libraries(test_ai_component PRIVATE glm)

# AnimationComponent test
add_executable(test_animation_component 
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_animation_component.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/AnimationComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/AnimationLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Component.cpp
)
target_link_libraries(test_animation_component PRIVATE glad glfw glm::glm assimp)

# NetworkComponent test
add_executable(test_network_component 
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_network_component.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/NetworkComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/NetworkSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Component.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Entity.cpp
)
target_link_libraries(test_network_component PRIVATE glm::glm)

# ParticleComponent test
add_executable(test_particle_component 
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_particle_component.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/ParticleComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ParticleSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Component.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Entity.cpp
)
target_link_libraries(test_particle_component PRIVATE glad glfw glm::glm stb_image OpenGL::GL)

# Archetype ECS test (Milestone 8 / #140) - header-only module, no extra deps
add_executable(test_archetype_ecs
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_archetype_ecs.cpp
)

# ECS query/View test (Milestone 8 / #141) - header-only module, no extra deps
add_executable(test_ecs_query
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ecs_query.cpp
)

# ECS hot-path components + systems test (Milestone 8 / #142) - header-only
add_executable(test_ecs_systems
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ecs_systems.cpp
)

# Hot-reload file watcher test (Milestone 10 / #146) - header-only, std::filesystem
add_executable(test_file_watcher
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_file_watcher.cpp
)

# SVG floor-plan importer test (Milestone 11 / #148) - header-only, no extra deps
add_executable(test_svg_floorplan
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_svg_floorplan.cpp
)

# Tiled (.tmx) importer test (Milestone 11 / #149) - header-only, no extra deps
add_executable(test_tmx_importer
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tmx_importer.cpp
)

# GeoJSON/OSM importer test (Milestone 11 / #150) - header-only, no extra deps
add_executable(test_geojson_importer
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_geojson_importer.cpp
)

# Nav-mesh baking + pathfinding test (Milestone 11 / #151) - header-only
add_executable(test_navmesh
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_navmesh.cpp
)

# Deterministic fixed-step simulation test (Milestone 12 / #152) - header-only
add_executable(test_simulation
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_simulation.cpp
)

# Flow-field crowd simulation test (Milestone 12 / #153) - header-only
add_executable(test_crowd
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_crowd.cpp
)

# Time control + rewind/replay test (Milestone 12 / #154) - header-only
add_executable(test_timeline
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_timeline.cpp
)

# Per-tick simulation data export test (Milestone 12 / #155) - header-only
add_executable(test_data_export
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_data_export.cpp
)

# IBrain + LLM/behavior-tree NPC test (Milestone 13 / #156). Header-only AI layer
# plus the engine EventSystem for the emit/consume integration.
add_executable(test_ai_brain
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ai_brain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/EventSystem.cpp
)

# Natural-language scene authoring test (Milestone 13 / #157) - header-only;
# generates entities through the world importer/scene-graph primitives.
add_executable(test_scene_author
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scene_author.cpp
)

# Procedural quest + dialogue generation test (Milestone 13 / #158) - header-only.
add_executable(test_quest_gen
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_quest_gen.cpp
)

# Deterministic simulation core for netcode (Milestone 14 / #159) - header-only.
add_executable(test_determinism
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_determinism.cpp
)

# Rollback + input prediction netcode (Milestone 14 / #160) - header-only.
add_executable(test_rollback
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rollback.cpp
)

# Doodle game adapter for the rollback netcode core (#291) - header-only.
add_executable(test_doodle_rollback
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_doodle_rollback.cpp
)

# Desync detection + diagnostics + input-log replay (Milestone 14 / #161) - header-only.
add_executable(test_desync
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_desync.cpp
)

# Renderer-agnostic drawing->scene converter (Milestone 15 / #162) - header-only.
add_executable(test_doodle_scene
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_doodle_scene.cpp
)

# Level-spec + 3D-scene JSON save/load (Milestone 15 / #163) - header-only.
add_executable(test_level_format
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_level_format.cpp
)

# Playable top-down dungeon loop (Milestone 15 / #164) - header-only.
add_executable(test_dungeon_game
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dungeon_game.cpp
)

# Headless tower-defense mode on the room topology: doorway-graph pathing, tower
# damage, lives, win/lose, determinism (#271) - header-only.
add_executable(test_tower_defense
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tower_defense.cpp
)

# First-person tour camera (Milestone 15 / #165) - header-only.
add_executable(test_tour_camera
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tour_camera.cpp
)

# Camera capture + perspective rectification (Milestone 16 / #166) - header-only.
add_executable(test_rectify
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rectify.cpp
)

# Binarize/clean + wall vectorization (Milestone 16 / #167) - header-only.
add_executable(test_vectorize
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_vectorize.cpp
)

# Topology: rooms, doorways, connectivity graph (Milestone 16 / #168) - header-only.
add_executable(test_topology
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_topology.cpp
)

# Tier-1 symbol recognition (Milestone 16 / #169) - header-only.
add_executable(test_symbols
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_symbols.cpp
)

# Phase 2 CV robustness: lighting/skew/paper/wobble + corpus (Milestone 16 / #239)
# - header-only.
add_executable(test_cv_robust
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cv_robust.cpp
)

# Confidence + repair review model (Milestone 16 / #170) - header-only.
add_executable(test_level_review
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_level_review.cpp
)

# Portable Doodle Dungeon converter library (Milestone 17 / #171). Header-only and
# renderer-agnostic (no OpenGL, no glm), so it builds independently of the engine
# renderer and can be consumed from a mobile app or any engine.
add_library(doodle INTERFACE)
target_include_directories(doodle INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Isolated tests for the library, linking only the standalone `doodle` target.
add_executable(test_doodle_library
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_doodle_library.cpp
)
target_link_libraries(test_doodle_library PRIVATE doodle)

# Mobile app shell (Milestone 17 / #172): platform-agnostic draw->snap->play->share
# loop over the doodle library. Header-only; the GLES/native layer wraps it.
add_executable(test_app_shell
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_app_shell.cpp
)
target_link_libraries(test_app_shell PRIVATE doodle)

# On-device symbol ML classifier (Milestone 17 / #173) - header-only inference.
add_executable(test_glyph_net
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_glyph_net.cpp
)

# Phase 4 symbol recognizer: data pipeline, export, accuracy/confusion harness,
# heuristic fallback (Milestone 16/17 / #240) - header-only.
add_executable(test_symbol_recognizer
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_symbol_recognizer.cpp
)

# UGC catalog: share codes, feed, stars (Milestone 17 / #174) - header-only.
add_executable(test_level_catalog
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_level_catalog.cpp
)

# Weekly challenge rotation + per-week leaderboard: deterministic week/selection,
# replay-verified submits, week-rollover isolation (#273) - header-only.
add_executable(test_weekly_challenge
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_weekly_challenge.cpp
)

# UGC Phase 5: stable share codes + level browser (Milestone 17 / #241) - header-only.
add_executable(test_level_share
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_level_share.cpp
)

# UGC Phase 5: fastest-clear leaderboards with replay anti-cheat (Milestone 17 /
# #242) - header-only.
add_executable(test_leaderboard
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_leaderboard.cpp
)

# Shareable ghost replays: content-verified trace codec + deterministic playback
# alongside a live run, bound to the level (#272) - header-only.
add_executable(test_ghost_replay
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ghost_replay.cpp
)

# Audio decode core: robust WAV/PCM parser + tone generator (#258) - header-only.
add_executable(test_audio_decode
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_audio_decode.cpp
)

# Audio cross-fade + streaming buffer-queue logic (#258) - header-only.
add_executable(test_audio_streaming
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_audio_streaming.cpp
)

# OGG/MP3 decode via vendored stb_vorbis + dr_mp3, against real embedded test
# vectors (#258). Links the decoders' implementation TU.
add_executable(test_audio_compressed
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_audio_compressed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/AudioDecodeCompressed.cpp
)
target_include_directories(test_audio_compressed PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# SSAO kernel/occlusion math mirrored by ssao.frag (#259) - header-only.
add_executable(test_ssao_kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ssao_kernel.cpp
)

# Shared particle update step mirrored by updateCPU + particle_update.compute
# (#259) - header-only.
add_executable(test_particle_sim
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_particle_sim.cpp
)

# Particle emitter cadence (rate/bursts/looping) + readback-free live-count estimate
# shared by the CPU and GPU emission paths (#267) - header-only.
add_executable(test_particle_emit
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_particle_emit.cpp
)

# Debug-text font atlas + quad geometry for the overlay renderer (#259) -
# header-only.
add_executable(test_debug_text
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_debug_text.cpp
)

# Linear-blend skinning + root-motion delta math mirrored by skinned_phong.vert
# and AnimationComponent (#260) - header-only.
add_executable(test_skinning
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_skinning.cpp
)

# Skinned shadow-depth pass: shadow-program selection + palette-upload policy, and
# the shadow vertex shader's skinning matching the tested core (#266) - header-only.
add_executable(test_skinned_shadow
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_skinned_shadow.cpp
)

# Performance-overlay stats core (Milestone 9 / #53) - header-only.
add_executable(test_perf_stats
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_perf_stats.cpp
)

# In-engine debug console core (Milestone 9 / #54) - header-only.
add_executable(test_debug_console
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_debug_console.cpp
)

# Reusable UI/HUD framework core (Milestone 9 / #55) - header-only.
add_executable(test_hud_framework
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hud_framework.cpp
)

# Entity inspector core + archetype-ECS reflection (Milestone 9 / #56) - header-only.
add_executable(test_entity_inspector
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_entity_inspector.cpp
)

# Ray-based viewport picking core (Milestone 9 / #57) - header-only.
add_executable(test_picking
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_picking.cpp
)

# Persistent settings store (Milestone 9 / #58) - header-only.
add_executable(test_settings
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_settings.cpp
)

# Interactive menu navigation core (Milestone 9 / #58) - header-only.
add_executable(test_menu_system
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_menu_system.cpp
)

# Scene hierarchy core (Milestone 9 / #59) - header-only.
add_executable(test_scene_hierarchy
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scene_hierarchy.cpp
)

# Input remapping core (Milestone 9 / #60) - header-only.
add_executable(test_input_map
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_input_map.cpp
)

# DPI / resolution UI scaling core (Milestone 9 / #61) - header-only.
add_executable(test_ui_scaling
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_scaling.cpp
)

# Performance benchmark recorder + export (Milestone 9 / #62) - header-only.
add_executable(test_benchmark
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_benchmark.cpp
)

# Structured logging system (Milestone 9 / #63) - header-only.
add_executable(test_log_system
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_log_system.cpp
)

# Vertical-slice integration: import -> nav -> crowd -> rewind (roadmap section 6).
add_executable(test_vertical_slice
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_vertical_slice.cpp
)

# Runnable samples (roadmap section 7 / #224) - header-only, no GL context needed.
add_executable(sample_vertical_slice
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/vertical_slice_demo.cpp
)
add_executable(sample_benchmark_export
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/benchmark_export_demo.cpp
)

# Render-pass dependency graph (rendering modernization step 1 / #226) - header-only.
add_executable(test_render_pass_graph
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_render_pass_graph.cpp
)

# Concrete frame graph + executor adoption (rendering modernization P1 / #232) - header-only.
add_executable(test_frame_graph
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_frame_graph.cpp
)

# PBR BRDF math: GGX / Smith / Fresnel / sRGB (rendering P2 / #233) - header-only.
add_executable(test_pbr_brdf
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pbr_brdf.cpp
)

# PBR material + reference direct-lighting shading (rendering P2 / #234) - header-only.
add_executable(test_pbr_material
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pbr_material.cpp
)

# PBR texture/factor blending rule mirrored by pbr.frag (#269) - header-only.
add_executable(test_pbr_textures
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pbr_textures.cpp
)

# IBL split-sum math: Hammersley, GGX importance sample, BRDF LUT integration, mip
# <-> roughness mapping, mirrored by the generation shaders (#270) - header-only.
add_executable(test_ibl_brdf
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ibl_brdf.cpp
)

# HDR exposure + ACES tone-mapping resolve math (rendering P3 / #235) - header-only.
add_executable(test_tone_mapping
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tone_mapping.cpp
)

# Composing the ACES resolve with the bloom chain: linear-space compositing and the
# single tone-map + gamma invariant (#268) - header-only.
add_executable(test_tonemap_chain
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tonemap_chain.cpp
)

# Cascaded shadow map math: splits / cascade selection / frustum fit / ortho
# (rendering P4 / #236) - header-only.
add_executable(test_shadow_cascades
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shadow_cascades.cpp
)

# Per-cascade light-matrix assembly: matrix algebra, slice fit, split monotonicity,
# CPU/GPU cascade-selection agreement, count==1 degradation (rendering / #265) -
# header-only, glm-free.
add_executable(test_shadow_cascade_build
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shadow_cascade_build.cpp
)

# PCF soft-shadow filtering math: tap sets / averaging / softness (rendering P4 /
# #237) - header-only.
add_executable(test_shadow_filtering
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shadow_filtering.cpp
)

# Tangent-space normal mapping math: tangent basis / TBN transform (rendering P4 /
# #238) - header-only.
add_executable(test_normal_mapping
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_normal_mapping.cpp
)

# Lua scripting layer test (Milestone 10 / #145). Links the self-built Lua
# static lib and uses sol2 (header-only) + the header-only ECS.
add_executable(test_scripting
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scripting.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scripting/ScriptSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/EventSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Logger.cpp
)
target_include_directories(test_scripting PRIVATE
    ${sol2_SOURCE_DIR}/include
    ${lua_SOURCE_DIR})
target_link_libraries(test_scripting PRIVATE lua_static)

if(MSVC)
    target_compile_options(IKore PRIVATE /W4)
else()
    target_compile_options(IKore PRIVATE -Wall -Wextra -pedantic)
endif()

# Multi-platform options
if (APPLE)
    target_link_libraries(IKore PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()

install(TARGETS IKore RUNTIME DESTINATION bin)

# ---------------------------------------------------------------------------
# Headless unit-test gate (issue: wire the test suite into CI as a PR check).
#
# These test_* targets are header-only / deterministic - they need no GL context,
# window, or audio device - so they run anywhere and gate every PR. Registered with
# CTest under the "headless" label and aggregated into the `headless_tests` target so
# CI can build + run just this subset:
#     cmake --build build --target headless_tests -j
#     ctest --test-dir build -L headless --output-on-failure
# GL/GLFW/OpenAL/Bullet-linked tests are intentionally excluded (they need a display
# or device). When adding a new headless test, add its target name here.
# ---------------------------------------------------------------------------
set(HEADLESS_TESTS
    test_app_shell
    test_archetype_ecs
    test_audio_decode
    test_audio_streaming
    test_benchmark
    test_crowd
    test_cv_robust
    test_data_export
    test_debug_console
    test_debug_text
    test_desync
    test_determinism
    test_doodle_library
    test_doodle_rollback
    test_doodle_scene
    test_dungeon_game
    test_ecs_benchmark
    test_ecs_query
    test_ecs_systems
    test_entity_inspector
    test_file_watcher
    test_frame_graph
    test_geojson_importer
    test_ghost_replay
    test_glyph_net
    test_hud_framework
    test_ibl_brdf
    test_input_map
    test_leaderboard
    test_level_catalog
    test_level_format
    test_level_review
    test_level_share
    test_log_system
    test_menu_system
    test_navmesh
    test_normal_mapping
    test_particle_emit
    test_particle_sim
    test_pbr_brdf
    test_pbr_material
    test_pbr_textures
    test_perf_stats
    test_picking
    test_quest_gen
    test_rectify
    test_render_pass_graph
    test_rollback
    test_scene_author
    test_scene_hierarchy
    test_settings
    test_shadow_cascade_build
    test_shadow_cascades
    test_shadow_filtering
    test_simulation
    test_skinned_shadow
    test_skinning
    test_ssao_kernel
    test_svg_floorplan
    test_symbol_recognizer
    test_symbols
    test_timeline
    test_tmx_importer
    test_tone_mapping
    test_tonemap_chain
    test_topology
    test_tour_camera
    test_tower_defense
    test_ui_scaling
    test_vectorize
    test_vertical_slice
    test_weekly_challenge
)

set(_present_headless_tests "")
foreach(_t ${HEADLESS_TESTS})
    if(TARGET ${_t})
        add_test(NAME ${_t} COMMAND ${_t})
        set_tests_properties(${_t} PROPERTIES LABELS headless)
        list(APPEND _present_headless_tests ${_t})
    else()
        message(WARNING "HEADLESS_TESTS lists '${_t}' but no such target exists")
    endif()
endforeach()

# Aggregate target so CI can build only the headless suite (skipping the engine and
# the GL/audio-linked tests) before running `ctest -L headless`.
if(_present_headless_tests)
    add_custom_target(headless_tests DEPENDS ${_present_headless_tests})
endif()
