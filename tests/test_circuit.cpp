#include <gtest/gtest.h>
#include <cmath>
#include "circuit/Circuit.h"

TEST(Circuit, InitiallyEmpty) {
    Circuit c;
    EXPECT_EQ(c.nodes.size(), 0u);
    EXPECT_EQ(c.components.size(), 0u);
    EXPECT_EQ(c.groundNodeId, -1);
}

TEST(Circuit, AddNodeReturnsSequentialIds) {
    Circuit c;
    EXPECT_EQ(c.addNode({0, 0}), 0);
    EXPECT_EQ(c.addNode({100, 200}), 1);
    EXPECT_EQ(c.addNode({-50, 75}), 2);
    EXPECT_EQ(c.nodes.size(), 3u);
}

TEST(Circuit, AddNodeStoresPosition) {
    Circuit c;
    c.addNode({10.5, 20.3});
    c.addNode({-7.0, 0.0}, "test");
    EXPECT_DOUBLE_EQ(c.nodes[0].position.x, 10.5);
    EXPECT_DOUBLE_EQ(c.nodes[0].position.y, 20.3);
    EXPECT_DOUBLE_EQ(c.nodes[1].position.x, -7.0);
    EXPECT_DOUBLE_EQ(c.nodes[1].position.y, 0.0);
    EXPECT_EQ(c.nodes[1].label, "test");
}

TEST(Circuit, AddComponentReturnsSequentialIds) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    EXPECT_EQ(c.addComponent(ComponentType::Resistor, 0, 1, 1000.0), 0);
    EXPECT_EQ(c.addComponent(ComponentType::Wire, 0, 1), 1);
    EXPECT_EQ(c.addComponent(ComponentType::VoltageSource, 0, 1, 5.0), 2);
    EXPECT_EQ(c.components.size(), 3u);
}

TEST(Circuit, AddComponentStoresFields) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({1, 1});
    int id = c.addComponent(ComponentType::Resistor, 0, 1, 470.0);
    auto& comp = c.components[0];
    EXPECT_EQ(comp.id, id);
    EXPECT_EQ(comp.type, ComponentType::Resistor);
    EXPECT_EQ(comp.nodeA, 0);
    EXPECT_EQ(comp.nodeB, 1);
    EXPECT_DOUBLE_EQ(comp.value, 470.0);
}

TEST(Circuit, AddWireDefaultsValueToZero) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({1, 1});
    c.addComponent(ComponentType::Wire, 0, 1);
    EXPECT_DOUBLE_EQ(c.components[0].value, 0.0);
}

TEST(Circuit, AddGroundComponent) {
    Circuit c;
    c.addNode({0, 0});
    c.addComponent(ComponentType::Ground, 0, 0);
    EXPECT_EQ(c.components[0].type, ComponentType::Ground);
}

TEST(Circuit, GroundNodeId) {
    Circuit c;
    c.addNode({100, 200});
    c.groundNodeId = 0;
    EXPECT_EQ(c.groundNodeId, 0);
}

TEST(Circuit, FindNodeValid) {
    Circuit c;
    c.addNode({1, 2});
    c.addNode({3, 4});
    Node* n = c.findNode(1);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->id, 1);
    EXPECT_DOUBLE_EQ(n->position.x, 3);
}

TEST(Circuit, FindNodeInvalid) {
    Circuit c;
    c.addNode({0, 0});
    EXPECT_EQ(c.findNode(999), nullptr);
    EXPECT_EQ(c.findNode(-1), nullptr);
}

TEST(Circuit, FindComponentValid) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({10, 10});
    c.addComponent(ComponentType::Resistor, 0, 1, 220.0);
    Component* comp = c.findComponent(0);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->id, 0);
    EXPECT_DOUBLE_EQ(comp->value, 220.0);
}

TEST(Circuit, FindComponentInvalid) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({1, 1});
    c.addComponent(ComponentType::Wire, 0, 1);
    EXPECT_EQ(c.findComponent(5), nullptr);
    EXPECT_EQ(c.findComponent(-1), nullptr);
}

TEST(Circuit, RemoveComponent) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({1, 1});
    c.addComponent(ComponentType::Wire, 0, 1);
    c.addComponent(ComponentType::Resistor, 0, 1, 100);
    EXPECT_EQ(c.components.size(), 2u);
    c.removeComponent(0);
    EXPECT_EQ(c.components.size(), 1u);
    EXPECT_EQ(c.components[0].id, 1);
}

TEST(Circuit, RemoveComponentMiddlePreservesOrder) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({1, 1});
    c.addComponent(ComponentType::Resistor, 0, 1, 100);
    c.addComponent(ComponentType::Resistor, 0, 1, 200);
    c.addComponent(ComponentType::Resistor, 0, 1, 300);
    c.removeComponent(1);
    ASSERT_EQ(c.components.size(), 2u);
    EXPECT_EQ(c.components[0].id, 0);
    EXPECT_EQ(c.components[1].id, 2);
}

TEST(Circuit, RemoveNonExistentComponentDoesNotCrash) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({1, 1});
    c.addComponent(ComponentType::Wire, 0, 1);
    EXPECT_NO_THROW(c.removeComponent(42));
    EXPECT_EQ(c.components.size(), 1u);
}

TEST(Circuit, RemoveNodeDeletesConnectedComponents) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.addComponent(ComponentType::Resistor, 0, 1, 100);
    c.addComponent(ComponentType::Resistor, 1, 2, 200);
    c.addComponent(ComponentType::Resistor, 0, 2, 300);
    EXPECT_EQ(c.components.size(), 3u);
    c.removeNode(1);
    EXPECT_EQ(c.nodes.size(), 2u);
    EXPECT_EQ(c.components.size(), 1u);
    EXPECT_EQ(c.components[0].nodeA, 0);
    EXPECT_EQ(c.components[0].nodeB, 2);
}

TEST(Circuit, RemoveNodeUnsetsGround) {
    Circuit c;
    c.addNode({0, 0});
    c.groundNodeId = 0;
    EXPECT_EQ(c.groundNodeId, 0);
    c.removeNode(0);
    EXPECT_EQ(c.groundNodeId, -1);
}

TEST(Circuit, RemoveNodeNotGroundLeavesGround) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({1, 1});
    c.groundNodeId = 0;
    c.removeNode(1);
    EXPECT_EQ(c.groundNodeId, 0);
}

TEST(Circuit, MultipleAddsAndRemoves) {
    Circuit c;
    for (int i = 0; i < 10; ++i) c.addNode({double(i), double(i)}, "n" + std::to_string(i));
    for (int i = 0; i < 9; ++i) c.addComponent(ComponentType::Wire, i, i + 1);
    EXPECT_EQ(c.nodes.size(), 10u);
    EXPECT_EQ(c.components.size(), 9u);
    c.removeNode(3);
    EXPECT_LT(c.nodes.size(), 10u);
    EXPECT_LT(c.components.size(), 9u);
}

// ─── toDistributed ──────────────────────────────────────────────

TEST(Circuit, DistributedEmptyCircuit) {
    Circuit c;
    Circuit d = c.toDistributed(4);
    EXPECT_EQ(d.nodes.size(), 0u);
    EXPECT_EQ(d.components.size(), 0u);
    EXPECT_EQ(d.groundNodeId, -1);
}

TEST(Circuit, DistributedPreservesNonWires) {
    Circuit c;
    int n0 = c.addNode({0, 0});
    int n1 = c.addNode({100, 0});
    c.addComponent(ComponentType::Resistor, n0, n1, 1000.0);
    c.addComponent(ComponentType::VoltageSource, n1, n0, 5.0);
    c.groundNodeId = 0;

    Circuit d = c.toDistributed(4);

    EXPECT_EQ(d.nodes.size(), 2u);
    EXPECT_EQ(d.groundNodeId, 0);

    int wireCount = 0, resCount = 0, vsCount = 0;
    for (const auto& comp : d.components) {
        if (comp.type == ComponentType::Wire) wireCount++;
        if (comp.type == ComponentType::Resistor) resCount++;
        if (comp.type == ComponentType::VoltageSource) vsCount++;
    }
    EXPECT_EQ(wireCount, 0);
    EXPECT_EQ(resCount, 1);
    EXPECT_EQ(vsCount, 1);
}

TEST(Circuit, DistributedWireBecomesNResistors) {
    Circuit c;
    int n0 = c.addNode({0, 0});
    int n1 = c.addNode({100, 0});
    c.addComponent(ComponentType::Wire, n0, n1);

    int N = 5;
    Circuit d = c.toDistributed(N);

    EXPECT_EQ(d.nodes.size(), 2u + (N - 1));  // original + intermediate
    int nWires = 0, nRes = 0;
    for (const auto& comp : d.components) {
        if (comp.type == ComponentType::Wire) nWires++;
        if (comp.type == ComponentType::Resistor) nRes++;
    }
    EXPECT_EQ(nWires, 0);
    EXPECT_EQ(nRes, N);
}

TEST(Circuit, DistributedIntermediateNodesOnLine) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addComponent(ComponentType::Wire, 0, 1);

    Circuit d = c.toDistributed(5);

    // Original nodes 0 and 1 preserved with same positions
    EXPECT_DOUBLE_EQ(d.nodes[0].position.x, 0.0);
    EXPECT_DOUBLE_EQ(d.nodes[1].position.x, 100.0);

    // Intermediate nodes at 20, 40, 60, 80
    EXPECT_DOUBLE_EQ(d.nodes[2].position.x, 20.0);
    EXPECT_DOUBLE_EQ(d.nodes[3].position.x, 40.0);
    EXPECT_DOUBLE_EQ(d.nodes[4].position.x, 60.0);
    EXPECT_DOUBLE_EQ(d.nodes[5].position.x, 80.0);
    for (int i = 2; i <= 5; ++i)
        EXPECT_DOUBLE_EQ(d.nodes[i].position.y, 0.0);
}

TEST(Circuit, DistributedResistorChainConnectsSequentially) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addComponent(ComponentType::Wire, 0, 1);

    Circuit d = c.toDistributed(3);

    // 3 resistors: 0->2, 2->3, 3->1
    EXPECT_EQ(d.components.size(), 3u);
    for (const auto& comp : d.components) {
        EXPECT_EQ(comp.type, ComponentType::Resistor);
    }
    EXPECT_EQ(d.components[0].nodeA, 0);
    EXPECT_EQ(d.components[0].nodeB, 2);
    EXPECT_EQ(d.components[1].nodeA, 2);
    EXPECT_EQ(d.components[1].nodeB, 3);
    EXPECT_EQ(d.components[2].nodeA, 3);
    EXPECT_EQ(d.components[2].nodeB, 1);
}

TEST(Circuit, DistributedSegments1IsNoOp) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addComponent(ComponentType::Wire, 0, 1);

    Circuit d = c.toDistributed(1);
    EXPECT_EQ(d.nodes.size(), 2u);
    EXPECT_EQ(d.components.size(), 1u);
    EXPECT_EQ(d.components[0].type, ComponentType::Wire);
}
