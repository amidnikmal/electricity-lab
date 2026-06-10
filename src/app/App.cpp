#include "app/App.h"
#include "app/UiScale.h"
#include "ui/MainWindow.h"
#include "ui/I18n.h"

#define GL_SILENCE_DEPRECATION

#include "gl_setup.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#include <cstdio>


static void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

App::App() = default;

App::~App() {
    if (m_window) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }
}

bool App::init() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_FALSE);

    // HiDPI: m_width/m_height are logical sizes; create the window in physical
    // pixels and scale fonts/style by the monitor content scale (175% Windows
    // scaling was rendering the whole UI unreadably small).
    float scaleX = 1.0f, scaleY = 1.0f;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
        glfwGetMonitorContentScale(monitor, &scaleX, &scaleY);
    m_uiScale = current_lab::app::clampUiScale(scaleX);

    m_window = glfwCreateWindow(current_lab::app::scaledWindowDimension(m_width, m_uiScale),
                                current_lab::app::scaledWindowDimension(m_height, m_uiScale),
                                "Current Lab — Milestone 1", nullptr, nullptr);
    if (!m_window) return false;

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    setupImGui();
    return true;
}

void App::setupImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    // Default ImGui font has no Cyrillic glyphs; load a system font that does
    // (needed for the Russian UI language). Fall back to the built-in font.
    const char* fontCandidates[] = {
#ifdef _WIN32
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/arial.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
    };
    static ImVector<ImWchar> glyphRanges;
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
        builder.AddRanges(io.Fonts->GetGlyphRangesGreek());
        // Math symbols used by the formula renderer and textbook units.
        builder.AddText("\xC2\xB7\xE2\x89\x88\xE2\x86\x92\xE2\x89\xA4\xE2\x89\xA5"
                        "\xE2\x88\x9E\xE2\x88\x92\xC2\xB5\xCE\xA9\xCF\x84"
                        "\xE2\x80\x94\xE2\x80\x93\xC2\xAB\xC2\xBB\xE2\x80\xA6"); // — – « » …
        builder.AddText(current_lab::i18n::allUiText()); // every translated string
        builder.BuildRanges(&glyphRanges);
    }
    bool fontLoaded = false;
    const float fontSize = current_lab::app::scaledFontSize(16.0f, m_uiScale);
    for (const char* path : fontCandidates) {
        std::FILE* probe = std::fopen(path, "rb");
        if (!probe)
            continue;
        std::fclose(probe);
        if (io.Fonts->AddFontFromFileTTF(path, fontSize, nullptr, glyphRanges.Data)) {
            fontLoaded = true;
            break;
        }
    }
    if (!fontLoaded)
        std::fprintf(stderr,
                     "Warning: no system TTF with Cyrillic glyphs found; "
                     "non-ASCII UI text will render as '?'\n");

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(m_uiScale); // paddings, spacing, scrollbars

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
}

void App::beginFrame() {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void App::endFrame() {
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(m_window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_window);
}

void App::run() {
    MainWindow mainWindow;

    while (!glfwWindowShouldClose(m_window)) {
        beginFrame();
        mainWindow.render();
        endFrame();
    }
}
