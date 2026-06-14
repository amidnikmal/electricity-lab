#pragma once

#include <string>
#include <unordered_set>
#include <vector>
#include "circuit/Circuit.h"

struct CircuitValidation {
    bool hasGround = false;
    bool hasFloatingComponents = false;
    bool conflictingSources = false;
    std::string message;
};

inline CircuitValidation validateCircuit(const Circuit& circuit) {
    CircuitValidation result;

    // 1. Проверка наличия земляного узла.
    if (circuit.groundNodeId < 0 || !circuit.findNode(circuit.groundNodeId)) {
        result.hasGround = false;
        if (circuit.nodes.empty()) {
            result.message = "Цепь не содержит узлов";
            return result;
        }
    } else {
        result.hasGround = true;
    }

    // 2. Поиск плавающих компонентов: узлы, не достижимые от земли.
    if (result.hasGround && circuit.nodes.size() > 1) {
        // Строим граф смежности по всем компонентам.
        std::unordered_map<int, std::vector<int>> adj;
        for (const auto& c : circuit.components) {
            adj[c.nodeA].push_back(c.nodeB);
            adj[c.nodeB].push_back(c.nodeA);
        }
        // BFS от земли.
        std::unordered_set<int> visited;
        std::vector<int> stack = {circuit.groundNodeId};
        visited.insert(circuit.groundNodeId);
        while (!stack.empty()) {
            int v = stack.back();
            stack.pop_back();
            auto it = adj.find(v);
            if (it == adj.end()) continue;
            for (int nbr : it->second) {
                if (visited.insert(nbr).second)
                    stack.push_back(nbr);
            }
        }
        if (visited.size() < circuit.nodes.size()) {
            result.hasFloatingComponents = true;
        }
    }

    // 3. Конфликтующие источники напряжения: два VoltageSource между
    //    одной парой узлов с разными value.
    struct SourceKey {
        int a, b;
        bool operator==(const SourceKey& o) const {
            return (a == o.a && b == o.b) || (a == o.b && b == o.a);
        }
    };
    struct SourceKeyHash {
        std::size_t operator()(const SourceKey& k) const {
            // коммутативный хэш: min^max
            int lo = k.a < k.b ? k.a : k.b;
            int hi = k.a < k.b ? k.b : k.a;
            return std::hash<int>()(lo) ^ (std::hash<int>()(hi) << 1);
        }
    };
    std::unordered_map<SourceKey, double, SourceKeyHash> seenSources;
    for (const auto& c : circuit.components) {
        if (c.type != ComponentType::VoltageSource) continue;
        SourceKey key{c.nodeA, c.nodeB};
        auto it = seenSources.find(key);
        if (it != seenSources.end()) {
            if (std::abs(it->second - c.value) > 1e-9) {
                result.conflictingSources = true;
                break;
            }
        } else {
            seenSources[key] = c.value;
        }
    }

    // Итоговое сообщение.
    std::string msg;
    if (!result.hasGround)
        msg = "Отсутствует земляной узел (Ground)";
    if (result.hasFloatingComponents) {
        if (!msg.empty()) msg += "; ";
        msg += "Есть плавающие компоненты (нет пути до земли)";
    }
    if (result.conflictingSources) {
        if (!msg.empty()) msg += "; ";
        msg += "Конфликтующие источники напряжения между одной парой узлов";
    }
    if (!msg.empty())
        result.message = msg;
    else if (!result.message.empty())
        ; // оставляем ранее установленное сообщение (например, пустая цепь)
    else
        result.message = "OK";

    return result;
}
