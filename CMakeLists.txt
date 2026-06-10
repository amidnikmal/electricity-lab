cmake_minimum_required(VERSION 3.20)
project(current-lab VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Sources are BOM-less UTF-8 (Cyrillic literals in I18n.cpp and tests); MSVC
# would otherwise decode them in the active code page and corrupt the strings.
if(MSVC)
    add_compile_options(/utf-8)
endif()

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

# Box2D — particle microdynamics for the visual layer (Drude-style)
FetchContent_Declare(box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG v2.4.1
)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box2d)

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
    box2d
)

# Platform-specific
if(UNIX)
    target_link_libraries(current-lab PRIVATE
        ${CMAKE_DL_LIBS}   # dlopen for glfw/GL
    )
endif()
if(WIN32)
    target_link_libraries(current-lab PRIVATE ws2_32)  # Winsock for net/HttpClient
endif()
if(MINGW)
    # Self-contained exe: otherwise Windows may resolve libstdc++-6.dll /
    # libgcc_s_seh-1.dll to a mismatched copy elsewhere on PATH (0xC0000139).
    target_link_options(current-lab PRIVATE -static)
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
    tests/test_dual_view.cpp
    tests/test_projection.cpp
    tests/test_transient.cpp
    tests/test_elements.cpp
    tests/test_mechanics.cpp
    tests/test_learning.cpp
    tests/test_diode_switch.cpp
    tests/test_fixes.cpp
    tests/test_pane_layout.cpp
    tests/test_hydraulic.cpp
    tests/test_mathtext.cpp
    tests/test_ui_scale.cpp
    tests/test_http.cpp
    tests/test_llm_robustness.cpp
    tests/test_demos.cpp
    tests/test_particle_sim.cpp
    tests/test_worlds_separation.cpp
    tests/test_chain_sim.cpp
    tests/test_chain_gear.cpp
    tests/test_water_network.cpp
    tests/test_app_path_directions.cpp
    src/circuit/Circuit.cpp
    src/solver/CircuitSolver.cpp
    src/projection/ProjectionBuilder.cpp
    src/ui/CanvasInteraction.cpp
    src/learning/TaskGenerator.cpp
    src/learning/AnkiExport.cpp
    src/assistant/LlmClient.cpp
    src/net/HttpClient.cpp
    src/ui/I18n.cpp
    src/ui/PaneLayout.cpp
    src/ui/MathTextParse.cpp
    src/physics/ParticleSim.cpp
    src/physics/ChainSim.cpp
)

add_executable(current-lab-tests ${TEST_SOURCES})
target_include_directories(current-lab-tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(current-lab-tests PRIVATE GTest::gtest_main box2d)
if(WIN32)
    target_link_libraries(current-lab-tests PRIVATE ws2_32)  # tests compile net/HttpClient.cpp
endif()
if(MINGW)
    target_link_options(current-lab-tests PRIVATE -static)
endif()

include(GoogleTest)
gtest_discover_tests(current-lab-tests)
