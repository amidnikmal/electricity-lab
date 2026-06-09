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
    int m_width = 1280;
    int m_height = 800;
};
