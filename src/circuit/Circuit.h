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
    Capacitor,
    Inductor,
    Diode,  // ideal piecewise-linear: conducts A->B when forward biased
    Switch, // value >= 0.5 closed, else open
    AcVoltageSource, // синусоидальный источник: v(t)=A·sin(2π·freq·t + phase)
};

enum class EditorMode : uint8_t {
    Select,
    PlaceNode,
    PlaceWire,
    PlaceResistor,
    PlaceVoltageSource,
    PlaceGround,
    PlaceCapacitor,
    PlaceInductor,
    PlaceDiode,
    PlaceSwitch,
    PlaceAcVoltageSource,
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
    double value = 0.0;        // R (Ом), C (Ф), L (Гн), V (В) — для AC: амплитуда (В)
    double frequency = 1.0;    // Гц (только для AcVoltageSource)
    double phase = 0.0;        // рад (только для AcVoltageSource)

    Component() = default;
    Component(int id_, ComponentType t, int a, int b, double val)
        : id(id_), type(t), nodeA(a), nodeB(b), value(val) {}
};

struct DistributedWireParameters {
    int segmentsPerWire = 8;
    double resistancePerUnit = 0.5; // Ohm / world unit in the current pedagogical model
};

struct Circuit {
    std::vector<Node> nodes;
    std::vector<Component> components;
    int groundNodeId = -1;

    std::vector<int> distributedSource;
    int nextNodeId = 0;
    int nextComponentId = 0;

    int addNode(Vec2 pos, std::string label = "") {
        int id = nextNodeId++;
        nodes.emplace_back(id, pos, std::move(label));
        return id;
    }

    int addNodeWithId(int id, Vec2 pos, std::string label = "") {
        nextNodeId = std::max(nextNodeId, id + 1);
        nodes.emplace_back(id, pos, std::move(label));
        return id;
    }

    int addComponent(ComponentType type, int nodeA, int nodeB, double value = 0.0) {
        int id = nextComponentId++;
        components.emplace_back(id, type, nodeA, nodeB, value);
        return id;
    }

    int addComponentWithId(int id, ComponentType type, int nodeA, int nodeB, double value = 0.0) {
        nextComponentId = std::max(nextComponentId, id + 1);
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

    const Component* findComponent(int id) const {
        for (const auto& c : components) if (c.id == id) return &c;
        return nullptr;
    }

    Node* findNode(int id) {
        for (auto& n : nodes) if (n.id == id) return &n;
        return nullptr;
    }

    const Node* findNode(int id) const {
        for (const auto& n : nodes) if (n.id == id) return &n;
        return nullptr;
    }

    int nodeIndex(int id) const {
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            if (nodes[i].id == id) return i;
        }
        return -1;
    }

    int componentIndex(int id) const {
        for (int i = 0; i < static_cast<int>(components.size()); ++i) {
            if (components[i].id == id) return i;
        }
        return -1;
    }

    Circuit toDistributed(const DistributedWireParameters& params) const {
        Circuit result;

        for (const auto& n : nodes)
            result.addNodeWithId(n.id, n.position, n.label);

        result.groundNodeId = groundNodeId;
        // Сегменты проводов получают СВЕЖИЕ id выше всех исходных: иначе
        // провод, добавленный раньше компонента с большим id, раздаёт
        // сегментам уже занятые id (дубль ломает componentIndex/branchFor и
        // тевенин-пробу LiveSim — ревью 2026-06-12, подтверждено репро).
        result.nextComponentId = nextComponentId;

        for (const auto& c : components) {
            if (c.type != ComponentType::Wire || params.segmentsPerWire <= 1) {
                result.addComponentWithId(c.id, c.type, c.nodeA, c.nodeB, c.value);
                result.distributedSource.push_back(c.id);
                continue;
            }

            const Node* nodeA = findNode(c.nodeA);
            const Node* nodeB = findNode(c.nodeB);
            if (!nodeA || !nodeB) {
                result.addComponentWithId(c.id, c.type, c.nodeA, c.nodeB, c.value);
                result.distributedSource.push_back(c.id);
                continue;
            }

            Vec2 a = nodeA->position;
            Vec2 b = nodeB->position;
            double wireLen = (b - a).length();
            double totalR = params.resistancePerUnit * wireLen;
            double rPerSeg = totalR / params.segmentsPerWire;

            int prevNode = c.nodeA;
            for (int i = 1; i < params.segmentsPerWire; ++i) {
                double t = static_cast<double>(i) / params.segmentsPerWire;
                Vec2 pos(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
                int newNode = result.addNode(pos);
                result.addComponent(ComponentType::Resistor, prevNode, newNode, rPerSeg);
                result.distributedSource.push_back(c.id);
                prevNode = newNode;
            }
            result.addComponent(ComponentType::Resistor, prevNode, c.nodeB, rPerSeg);
            result.distributedSource.push_back(c.id);
        }

        return result;
    }

    Circuit toDistributed(int segmentsPerWire = 8) const {
        DistributedWireParameters params;
        params.segmentsPerWire = segmentsPerWire;
        return toDistributed(params);
    }
};
