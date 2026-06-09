#pragma once

#include "imgui.h"
#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include <string>
#include <vector>
#include <functional>

class LogPanel {
public:
    void addMessage(const std::string& msg);
    void render();

private:
    std::vector<std::string> m_messages;
};

class InspectorPanel {
public:
    void render(Circuit& circuit, const CircuitSolution* solution,
                int selNode, int selComp);

    LogPanel& log() { return m_log; }

    std::function<void()> onChange;

private:
    LogPanel m_log;
};
