cmake_minimum_required(VERSION 3.10)
cmake_policy(VERSION 3.10)

# CMake 4.x (as installed on the macOS/Windows CI runners) removed compatibility with
# projects requesting cmake_minimum_required(VERSION <3.5). Several FetchContent
# dependencies (glad, glfw, glm, bullet, imgui) still declare such an old minimum, so
# there they fail to configure with "Compatibility with CMake < 3.5 has been removed"
# (older CMake, e.g. on the Linux runners, only warns). Provide a policy floor so those
# subprojects configure on every runner. A -DCMAKE_POLICY_VERSION_MINIMUM=... override
# still wins.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

# Set modern OpenGL preference policy
if(POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()

project(IKoreEngine VERSION 1.0.0 LANGUAGES C CXX)

# Release version for packaging (#369): defaults to the project version, overridable from CI
# with -DIKORE_RELEASE_VERSION=<tag> so a tagged build stamps its real version.
if(NOT DEFINED IKORE_RELEASE_VERSION)
    set(IKORE_RELEASE_VERSION "${PROJECT_VERSION}")
endif()

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
# assimp v5.3.1 compiles its own library with -Werror. Newer compilers (e.g. clang 21 on
# the macOS runner) add warnings the old code trips - -Wnontrivial-memcall in
# SceneCombiner.cpp - which then fail the build. Don't treat the vendored library's
# warnings as errors; this only affects assimp's own sources, not ours.
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
# assimp v5.3.1 bundles an old zlib whose zutil.h redefines fdopen(); against a very
# new SDK (macOS Xcode 26 / clang) that macro clashes with the system <stdio.h> and the
# vendored zlib fails to compile. macOS ships a usable system zlib, so use it there
# instead of the bundled copy. Linux/Windows keep the bundled zlib (it compiles on their
# toolchains, and the Windows runner has no system zlib to find).
if(APPLE)
    set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "" FORCE)
endif()
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

# Skinned skeletal-animation loading test (issue #287): loads the animated glTF and checks
# the bone palette actually animates. Needs a GL context (Model creates VBOs), so it is a
# gl-labelled test run under Xvfb.
add_executable(test_skinned_animation
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_skinned_animation.cpp
    ${CORE_SOURCES}
    ${AUDIO_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Frustum.cpp
)
target_link_libraries(test_skinned_animation PRIVATE glfw glad glm stb_image assimp OpenGL::GL BulletDynamics BulletCollision LinearMath ${OPENAL_LIBRARIES})

# Skinned-render golden-image test (issue #287 / #290): renders the animated model through
# the real skinned vertex program to an FBO and compares to a committed golden. Engine- and
# GL-linked; registered as a gl-labelled test below.
add_executable(render_skinned_golden
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/render_skinned_golden.cpp
    ${CORE_SOURCES}
    ${AUDIO_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Frustum.cpp
)
target_link_libraries(render_skinned_golden PRIVATE glfw glad glm stb_image assimp OpenGL::GL BulletDynamics BulletCollision LinearMath ${OPENAL_LIBRARIES})

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
    # EntityDebugSystem.cpp is intentionally omitted (issue #294): it is a graphics
    # (glad/GL) source that this audio-only test never references, and compiling it here
    # broke the build with "glad/glad.h: No such file or directory" since this target does
    # not link glad. Excluding it keeps the target true to its "no graphics deps" comment.
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

# Animation blending + crossfade transitions (#321). Pure CPU palette-blend math (no GL
# context needed), but it links AnimationComponent.cpp + assimp, so it runs under the
# gl-labelled job alongside test_animation_component.
add_executable(test_animation_blend
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_animation_blend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/components/AnimationComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Component.cpp
)
target_link_libraries(test_animation_blend PRIVATE glad glfw glm::glm assimp)

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

# Co-op shared-world mode: N avatars in one DungeonGame over rollback (#356) - header-only.
add_executable(test_coop_shared_world
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_coop_shared_world.cpp
)

# Real-time two-player versus over the rollback session (#293) - header-only.
add_executable(test_versus
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_versus.cpp
)

# Performance-regression gate (#295): times deterministic workloads and compares
# ratio-normalized results to a committed baseline. Not a headless CTest (it is
# timing-based); run in its own CI job via --check.
add_executable(perf_gate
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/perf_gate.cpp
)

# Headless offscreen render + golden-image regression harness (#290). Uses an EGL
# surfaceless context + system GL, so it is Linux/EGL-only: guard the target so it does
# not break configuration on platforms without EGL. The render-golden CI job builds it
# directly with g++, so this target is a local convenience.
find_library(EGL_LIBRARY NAMES EGL)
if(EGL_LIBRARY AND TARGET OpenGL::GL)
    add_executable(render_golden
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/render_golden.cpp
    )
    target_link_libraries(render_golden PRIVATE ${EGL_LIBRARY} OpenGL::GL)
endif()

# Desync detection + diagnostics + input-log replay (Milestone 14 / #161) - header-only.
add_executable(test_desync
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_desync.cpp
)

# Desync equality/digest covers the #319/#320 state (keys/doors/switches/projectiles) (#337) - header-only.
add_executable(test_desync_features
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_desync_features.cpp
)

# Desync recovery (resync from snapshot) + connection-quality HUD (#360) - header-only.
add_executable(test_desync_recovery
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_desync_recovery.cpp
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

# Keys, locked doors, switches, and hazards over the dungeon loop (#319) - header-only.
add_executable(test_dungeon_features
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dungeon_features.cpp
)

# DungeonGame -> live ECS runtime bridge (engine wiring) - header-only.
add_executable(test_dungeon_runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dungeon_runtime.cpp
)

# Render-list builder for a live DungeonRuntime (statics + ECS actors) (#333) - header-only.
add_executable(test_dungeon_render_data
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dungeon_render_data.cpp
)

# GL render pass that draws the DungeonRuntime draw list offscreen (#333). Links glad/glfw/
# glm and needs a GL context, so it is a gl-labelled test (run under Xvfb + software GL).
add_executable(test_dungeon_render_gl
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dungeon_render_gl.cpp
)
target_link_libraries(test_dungeon_render_gl PRIVATE glad glfw glm::glm)

# Per-type mesh/material selection for the live renderer (decorate hook) (#350) - header-only.
add_executable(test_dungeon_decor
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dungeon_decor.cpp
)

# GL render pass drawing a real per-type mesh per entity offscreen (#350). Links glad/glfw/
# glm and needs a GL context, so it is a gl-labelled test (run under Xvfb + software GL).
add_executable(test_dungeon_mesh_gl
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dungeon_mesh_gl.cpp
)
target_link_libraries(test_dungeon_mesh_gl PRIVATE glad glfw glm::glm)

# Selectable enemy behaviors: patrol / flee / ranged, default chase (#320) - header-only.
add_executable(test_enemy_behaviors
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_enemy_behaviors.cpp
)

# Pushable blocks (Sokoban) + block-aware solver over DungeonGame rules (#345) - header-only.
add_executable(test_pushable_blocks
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pushable_blocks.cpp
)

# GeoJSON/OSM map data -> playable DungeonGame world (#313) - header-only.
add_executable(test_city_game
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_city_game.cpp
)

# Richer OSM import: region kinds, POIs, barriers -> hazards/walls/coins (#365) - header-only.
add_executable(test_city_osm_rich
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_city_osm_rich.cpp
)

# Deterministic solvability validator + auto-solver over DungeonGame rules (#316) - header-only.
add_executable(test_solver
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_solver.cpp
)

# Multi-room levels from a cv::Topology room/doorway graph (#344) - header-only.
add_executable(test_multi_room
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_multi_room.cpp
)

# Ordered campaign with unlocks + persistent progress via the Settings store (#347) - header-only.
add_executable(test_campaign
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_campaign.cpp
)

# Authored tutorial/onboarding levels wired as the campaign's first world (#349) - header-only.
add_executable(test_tutorial
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tutorial.cpp
)

# Headless tower-defense mode on the room topology: doorway-graph pathing, tower
# damage, lives, win/lose, determinism (#271) - header-only.
add_executable(test_tower_defense
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tower_defense.cpp
)

# Single-elimination tournament bracket over verified match records (#358) - header-only.
add_executable(test_tournament
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tournament.cpp
)

# Mobile touch controls + GLES render-path selection (#367) - header-only.
add_executable(test_touch_controls
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_touch_controls.cpp
)

# First-person tour camera (Milestone 15 / #165) - header-only.
add_executable(test_tour_camera
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tour_camera.cpp
)

# Top-down/follow game camera + theme palette for Doodlebound (#354) - header-only.
add_executable(test_game_camera
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_game_camera.cpp
)

# GL render from the game camera with a theme, offscreen (#354). Links glad/glfw/glm and
# needs a GL context, so it is a gl-labelled test (run under Xvfb + software GL).
add_executable(test_game_camera_gl
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_game_camera_gl.cpp
)
target_link_libraries(test_game_camera_gl PRIVATE glad glfw glm::glm)

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

# Recognized drawing symbols -> game spawns -> playable DungeonGame (#314) - header-only.
add_executable(test_symbol_spawns
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_symbol_spawns.cpp
)

# Extended color+shape vocabulary: keys/doors/switches/hazards + enemy behaviors (#336) - header-only.
add_executable(test_symbol_vocabulary
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_symbol_vocabulary.cpp
)

# Photographed drawing -> rectify (de-warp) -> recognized spawns -> playable (#315) - header-only.
add_executable(test_photo_level
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_photo_level.cpp
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

# Gameplay-state -> animation clip + crossfade decision layer (#351) - header-only.
add_executable(test_actor_anim
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_actor_anim.cpp
)

# On-device symbol ML classifier (Milestone 17 / #173) - header-only inference.
add_executable(test_glyph_net
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_glyph_net.cpp
)

# Digit-glyph recognition -> @id feature links via GlyphNet (#361) - header-only.
add_executable(test_glyph_digits
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_glyph_digits.cpp
)

# Labeled glyph corpus + recognition-accuracy gate (#364) - header-only.
add_executable(test_glyph_corpus
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_glyph_corpus.cpp
)

# Phase 4 symbol recognizer: data pipeline, export, accuracy/confusion harness,
# heuristic fallback (Milestone 16/17 / #240) - header-only.
add_executable(test_symbol_recognizer
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_symbol_recognizer.cpp
)

# Solver-guided auto-repair of unsolvable captured levels (#362) - header-only.
add_executable(test_level_autorepair
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_level_autorepair.cpp
)

# UGC catalog: share codes, feed, stars (Milestone 17 / #174) - header-only.
add_executable(test_level_catalog
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_level_catalog.cpp
)

# Solver-as-oracle seeded procedural level generator (#348) - header-only.
add_executable(test_level_gen
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_level_gen.cpp
)

# In-engine level editor: command layer, undo/redo, live solver fairness (#363) - header-only.
add_executable(test_level_editor
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_level_editor.cpp
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

# Par-based 1-3 star ratings on leaderboard + weekly submissions (#346) - header-only.
add_executable(test_star_rating
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_star_rating.cpp
)

# Solver-backed fairness gate for leaderboard/weekly-challenge submissions (#334) - header-only.
add_executable(test_fairness_gate
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fairness_gate.cpp
)

# Shareable ghost replays: content-verified trace codec + deterministic playback
# alongside a live run, bound to the level (#272) - header-only.
add_executable(test_ghost_replay
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ghost_replay.cpp
)

# Async ghost race: live run vs a shared ghost, lead + finish order, level-bound (#292)
# - header-only.
add_executable(test_ghost_race
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ghost_race.cpp
)

# Gameplay-event detection -> particle burst + sound cue mapping (#353) - header-only.
add_executable(test_game_events
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_game_events.cpp
)

# GL render of gameplay-event particle bursts offscreen (#353). Links glad/glfw/glm and needs
# a GL context, so it is a gl-labelled test (run under Xvfb + software GL).
add_executable(test_game_fx_gl
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_game_fx_gl.cpp
)
target_link_libraries(test_game_fx_gl PRIVATE glad glfw glm::glm)

# Shareable spectator replays: content-verified run/match codec + deterministic
# per-tick playback (single run and versus) (#317) - header-only.
add_executable(test_replay
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_replay.cpp
)

# N-player (3-4) free-for-all versus over rollback: winner + standings, convergence,
# anti-cheat record (#318) - header-only.
add_executable(test_versus_ffa
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_versus_ffa.cpp
)

# Deterministic Solver-oracle AI opponent -> input stream for Versus/FFA (#357) - header-only.
add_executable(test_ai_opponent
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ai_opponent.cpp
)

# FFA standings HUD + spectator replay scrubber over the HUD framework (#335) - header-only.
add_executable(test_spectator_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_spectator_ui.cpp
)

# Replay scrubber input controls + spectator HUD draw-list resolution (#352) - header-only.
add_executable(test_spectator_controls
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_spectator_controls.cpp
)

# Spectator lobby: list live/finished sessions + route to standings/replay (#359) - header-only.
add_executable(test_spectator_lobby
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_spectator_lobby.cpp
)

# GL render of the spectator HUD (scrubber bar + panels) offscreen (#352). Links glad/glfw/
# glm and needs a GL context, so it is a gl-labelled test (run under Xvfb + software GL).
add_executable(test_spectator_hud_gl
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_spectator_hud_gl.cpp
)
target_link_libraries(test_spectator_hud_gl PRIVATE glad glfw glm::glm)

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

# Unified save model, corruption-resistant writes, cloud sync, share/import (#370) - header-only.
add_executable(test_save_sync
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_save_sync.cpp
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

# Platform input: rebind -> GameInput, gamepad, DPI/safe-area UI scaling (#368) - header-only.
add_executable(test_input_mapping
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_input_mapping.cpp
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

# Doodle cores wired into the live ECS: GeoJSON city -> playable world -> DungeonRuntime
# spawns/steps ECS entities, validated by the solver. Header-only, no GL context.
add_executable(sample_doodle_runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/doodle_runtime_demo.cpp
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

install(TARGETS IKore RUNTIME DESTINATION bin COMPONENT ikore_runtime)

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
    test_actor_anim
    test_ai_opponent
    test_app_shell
    test_archetype_ecs
    test_audio_decode
    test_audio_streaming
    test_benchmark
    test_campaign
    test_city_game
    test_city_osm_rich
    test_coop_shared_world
    test_crowd
    test_cv_robust
    test_data_export
    test_debug_console
    test_debug_text
    test_desync
    test_desync_features
    test_desync_recovery
    test_determinism
    test_doodle_library
    test_doodle_rollback
    test_doodle_scene
    test_dungeon_decor
    test_dungeon_features
    test_dungeon_game
    test_dungeon_render_data
    test_dungeon_runtime
    test_ecs_benchmark
    test_ecs_query
    test_ecs_systems
    test_enemy_behaviors
    test_entity_inspector
    test_fairness_gate
    test_file_watcher
    test_frame_graph
    test_game_camera
    test_game_events
    test_geojson_importer
    test_ghost_race
    test_ghost_replay
    test_glyph_corpus
    test_glyph_digits
    test_glyph_net
    test_hud_framework
    test_ibl_brdf
    test_input_map
    test_input_mapping
    test_leaderboard
    test_level_autorepair
    test_level_catalog
    test_level_editor
    test_level_format
    test_level_gen
    test_level_review
    test_level_share
    test_log_system
    test_menu_system
    test_multi_room
    test_navmesh
    test_normal_mapping
    test_particle_emit
    test_particle_sim
    test_pbr_brdf
    test_pbr_material
    test_pbr_textures
    test_perf_stats
    test_photo_level
    test_picking
    test_pushable_blocks
    test_quest_gen
    test_rectify
    test_render_pass_graph
    test_replay
    test_rollback
    test_save_sync
    test_scene_author
    test_scene_hierarchy
    test_settings
    test_shadow_cascade_build
    test_shadow_cascades
    test_shadow_filtering
    test_simulation
    test_skinned_shadow
    test_skinning
    test_solver
    test_spectator_controls
    test_spectator_lobby
    test_spectator_ui
    test_ssao_kernel
    test_star_rating
    test_svg_floorplan
    test_symbol_recognizer
    test_symbol_spawns
    test_symbol_vocabulary
    test_symbols
    test_timeline
    test_tmx_importer
    test_tone_mapping
    test_tonemap_chain
    test_topology
    test_touch_controls
    test_tour_camera
    test_tournament
    test_tower_defense
    test_tutorial
    test_ui_scaling
    test_vectorize
    test_versus
    test_versus_ffa
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

# --- GL / audio tests (issue #294) -------------------------------------------------
#
# These tests link the engine's GL / GLFW / OpenAL / Bullet stack and were excluded from
# the headless gate because they need a display or an audio device. They are registered
# here under the `gl` label so CI can run them under a virtual display (Xvfb) + a software
# GL (mesa/llvmpipe) + a null OpenAL device: `ctest -L gl`.
#
# Triaged to run headlessly under that setup. Any test that genuinely cannot (documented
# below) is quarantined by leaving it out of this list rather than being silently dropped.
set(GL_TESTS
    test_ai_brain
    test_ai_component
    test_animation_blend
    test_animation_component
    test_audio_compressed
    test_dungeon_mesh_gl
    test_dungeon_render_gl
    test_event_system
    test_event_system_simple
    test_game_camera_gl
    test_game_fx_gl
    test_material_component
    test_mesh_component
    test_mesh_component_geometry
    test_mesh_component_minimal
    test_network_component
    test_openal_3d_audio
    test_particle_component
    test_physics_component
    test_physics_simple
    test_scene_graph
    test_scripting
    test_skinned_animation
    test_spectator_hud_gl
)

set(_present_gl_tests "")
foreach(_t ${GL_TESTS})
    if(TARGET ${_t})
        add_test(NAME ${_t} COMMAND ${_t})
        set_tests_properties(${_t} PROPERTIES LABELS gl)
        list(APPEND _present_gl_tests ${_t})
    else()
        message(WARNING "GL_TESTS lists '${_t}' but no such target exists")
    endif()
endforeach()

# Aggregate target so CI can build just the GL/audio tests before running `ctest -L gl`.
if(_present_gl_tests)
    add_custom_target(gl_tests DEPENDS ${_present_gl_tests})
endif()

# test_particle_component initializes the real particle system, which loads its shaders from
# src/shaders/ by relative path, so it must run from the repository root (issue #294).
if(TARGET test_particle_component)
    set_tests_properties(test_particle_component PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# test_skinned_animation loads assets/models/animated_bone.gltf by relative path (issue #287).
if(TARGET test_skinned_animation)
    set_tests_properties(test_skinned_animation PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Skinned-render golden test (issue #287 / #290): run the render_skinned_golden tool in
# --check mode against the committed golden, under the gl label + Xvfb (it loads the model,
# shaders and golden by relative path, so it runs from the repo root). Regenerate the golden
# with: render_skinned_golden --emit tools/golden/skinned.ppm  (from the repo root).
if(TARGET render_skinned_golden)
    add_test(NAME test_skinned_golden COMMAND render_skinned_golden --check tools/golden/skinned.ppm)
    set_tests_properties(test_skinned_golden PROPERTIES LABELS gl WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_dependencies(gl_tests render_skinned_golden)
endif()

# ---------------------------------------------------------------------------
# Release packaging (CPack) - per-platform installable artifacts (#369).
#
# The main executable is already installed to bin/ (above); ship the runtime assets
# alongside it so the package is self-contained, then let CPack roll it into a per-platform
# archive (ZIP on Windows, tar.gz elsewhere). The release workflow builds Release, runs
# `cpack`, and attaches the result to a GitHub Release. Adding this only defines the
# packaging targets/variables at configure time; it does not affect a normal build or the
# test gates.
#
# Packaging must not rebuild the whole test suite: the install/preinstall target only needs
# the installed target (IKore) up to date, so a CPack run after `--target IKore` is fast and
# is not tripped by an unrelated target. The release job builds IKore, then runs cpack.
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY TRUE)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION bin COMPONENT ikore_runtime)
endif()

# Package only our runtime component. The vendored FetchContent deps (bullet, glfw, assimp,
# ...) register their own install rules for static libs and headers we do not ship and that
# a game build does not produce; scoping CPack to ikore_runtime keeps the package to just the
# executable + assets and avoids trying to install unbuilt dependency artifacts.
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL ikore_runtime)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)

set(CPACK_PACKAGE_NAME "IKoreEngine")
set(CPACK_PACKAGE_VENDOR "IKore")
set(CPACK_PACKAGE_VERSION "${IKORE_RELEASE_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Doodlebound - draw a level, then play it")
set(CPACK_PACKAGE_FILE_NAME "IKoreEngine-${IKORE_RELEASE_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "IKoreEngine")
set(CPACK_STRIP_FILES ON)
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
