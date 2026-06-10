#pragma once

struct GLFWwindow;

class App {
public:
    App();
    ~App();

    bool init();
    void run();

private:
    void setupImGui();
    void beginFrame();
    void endFrame();

    GLFWwindow* m_window = nullptr;
    int m_width = 1280;  // logical size; physical = logical * m_uiScale
    int m_height = 800;
    float m_uiScale = 1.0f; // monitor content scale (1.75 = 175% Windows scaling)
};
