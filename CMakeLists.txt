cmake_minimum_required(VERSION 3.20)
project(current-lab VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Dependencies via FetchContent ---
include(FetchContent)

# OpenGL (system)
find_package(OpenGL REQUIRED)

# GLFW — window and input
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# GLM — OpenGL Mathematics (header-only)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

# Dear ImGui — UI
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.7-docking
)
FetchContent_MakeAvailable(imgui)

# --- ImGui sources ---
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# --- Application sources ---
file(GLOB_RECURSE APP_SOURCES
    src/*.cpp
    src/*.h
)

# --- Executable ---
add_executable(current-lab
    ${APP_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(current-lab PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(current-lab PRIVATE
    OpenGL::GL
    glfw
)

# Platform-specific
if(UNIX)
    target_link_libraries(current-lab PRIVATE
        ${CMAKE_DL_LIBS}   # dlopen for glfw/GL
    )
endif()

# --- Google Test ---
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- Test executable (no GL/GUI deps) ---
set(TEST_SOURCES
    tests/test_linear_system.cpp
    tests/test_circuit.cpp
    tests/test_solver.cpp
    tests/test_consistency.cpp
    tests/test_canvas.cpp
    src/circuit/Circuit.cpp
    src/solver/CircuitSolver.cpp
)

add_executable(current-lab-tests ${TEST_SOURCES})
target_include_directories(current-lab-tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(current-lab-tests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(current-lab-tests)
