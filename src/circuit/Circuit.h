#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include "math/Vec2.h"

enum class ComponentType : uint8_t {
    Wire,
    Resistor,
    VoltageSource,
    Ground,
};

enum class EditorMode : uint8_t {
    Select,
    PlaceNode,
    PlaceWire,
    PlaceResistor,
    PlaceVoltageSource,
    PlaceGround,
};

struct Node {
    int id = 0;
    Vec2 position;
    std::string label;

    Node() = default;
    Node(int id_, Vec2 pos, std::string lbl = "")
        : id(id_), position(pos), label(std::move(lbl)) {}
};

struct Component {
    int id = 0;
    ComponentType type = ComponentType::Wire;
    int nodeA = 0;
    int nodeB = 0;
    double value = 0.0;

    Component() = default;
    Component(int id_, ComponentType t, int a, int b, double val)
        : id(id_), type(t), nodeA(a), nodeB(b), value(val) {}
};

struct Circuit {
    std::vector<Node> nodes;
    std::vector<Component> components;
    int groundNodeId = -1;

    std::vector<int> distributedSource;

    int addNode(Vec2 pos, std::string label = "") {
        int id = static_cast<int>(nodes.size());
        nodes.emplace_back(id, pos, std::move(label));
        return id;
    }

    int addComponent(ComponentType type, int nodeA, int nodeB, double value = 0.0) {
        int id = static_cast<int>(components.size());
        components.emplace_back(id, type, nodeA, nodeB, value);
        return id;
    }

    void removeComponent(int id) {
        for (auto it = components.begin(); it != components.end(); ++it) {
            if (it->id == id) { components.erase(it); return; }
        }
    }

    void removeNode(int nodeId) {
        components.erase(std::remove_if(components.begin(), components.end(),
            [nodeId](const Component& c) { return c.nodeA == nodeId || c.nodeB == nodeId; }),
            components.end());
        if (groundNodeId == nodeId) groundNodeId = -1;
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
            [nodeId](const Node& n) { return n.id == nodeId; }),
            nodes.end());
    }

    Component* findComponent(int id) {
        for (auto& c : components) if (c.id == id) return &c;
        return nullptr;
    }

    Node* findNode(int id) {
        for (auto& n : nodes) if (n.id == id) return &n;
        return nullptr;
    }

    Circuit toDistributed(int segmentsPerWire = 8) const {
        Circuit result;

        for (const auto& n : nodes)
            result.addNode(n.position, n.label);

        result.groundNodeId = groundNodeId;

        const double R_PER_UNIT = 0.5;

        for (int ci = 0; ci < (int)components.size(); ++ci) {
            const auto& c = components[ci];
            if (c.type != ComponentType::Wire || segmentsPerWire <= 1) {
                result.addComponent(c.type, c.nodeA, c.nodeB, c.value);
                result.distributedSource.push_back(ci);
                continue;
            }

            if (c.nodeA >= (int)nodes.size() || c.nodeB >= (int)nodes.size()) {
                result.addComponent(c.type, c.nodeA, c.nodeB, c.value);
                result.distributedSource.push_back(ci);
                continue;
            }

            Vec2 a = nodes[c.nodeA].position;
            Vec2 b = nodes[c.nodeB].position;
            double wireLen = (b - a).length();
            double totalR = R_PER_UNIT * wireLen;
            double rPerSeg = totalR / segmentsPerWire;

            int prevNode = c.nodeA;
            for (int i = 1; i < segmentsPerWire; ++i) {
                double t = (double)i / segmentsPerWire;
                Vec2 pos(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
                int newNode = result.addNode(pos);
                result.addComponent(ComponentType::Resistor, prevNode, newNode, rPerSeg);
                result.distributedSource.push_back(ci);
                prevNode = newNode;
            }
            result.addComponent(ComponentType::Resistor, prevNode, c.nodeB, rPerSeg);
            result.distributedSource.push_back(ci);
        }

        return result;
    }
};
