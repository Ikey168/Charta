cmake_minimum_required(VERSION 3.22.1)
project(doodlebound_android LANGUAGES CXX)
find_package(game-activity REQUIRED CONFIG)

# Deliberately independent of the root desktop CMake configuration. The doodle,
# mobile, CV, and game APIs are header-only and use the C++ standard library.
add_library(doodlebound SHARED
    NativeBridge.cpp
    render/MobileRenderer.cpp
)
target_compile_features(doodlebound PRIVATE cxx_std_17)
target_include_directories(doodlebound PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../src
)
target_compile_options(doodlebound PRIVATE -Wall -Wextra -Wpedantic)
# Keep symbols in the CMake release output for the handoff archive. Gradle
# strips the copy packaged into the APK independently.
target_compile_options(doodlebound PRIVATE "$<$<CONFIG:Release>:-g>")
# JNI entry points in the GameActivity prefab archive are looked up by the VM,
# so the linker needs an explicit undefined symbol to retain that object.
target_link_options(doodlebound PRIVATE "-Wl,-u,Java_com_google_androidgamesdk_GameActivity_initializeNativeCode")
target_link_libraries(doodlebound PRIVATE GLESv3 EGL android log game-activity::game-activity_static)
