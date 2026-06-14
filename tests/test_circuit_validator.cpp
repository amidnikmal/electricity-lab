// Тесты для CircuitValidator (validateCircuit).
// До этого класс существовал, но не был покрыт тестами и нигде не вызывался.
// Покрываем три проверки: земля, плавающие компоненты, конфликт источников.
#include <gtest/gtest.h>

#include "circuit/Circuit.h"
#include "circuit/CircuitValidator.h"

namespace {

// Корректная цепь: источник 5В + резистор в петле с землёй → OK.
TEST(CircuitValidator, ValidGroundedCircuitReportsOk) {
    Circuit c;
    int gnd = c.addNode({0, 0});
    int top = c.addNode({0, 100});
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::VoltageSource, gnd, top, 5.0);
    c.addComponent(ComponentType::Resistor, top, gnd, 1000.0);

    CircuitValidation v = validateCircuit(c);
    EXPECT_TRUE(v.hasGround);
    EXPECT_FALSE(v.hasFloatingComponents);
    EXPECT_FALSE(v.conflictingSources);
    EXPECT_EQ(v.message, "OK");
}

// Нет земляного узла → флаг hasGround=false и сообщение про землю.
TEST(CircuitValidator, MissingGroundIsDetected) {
    Circuit c;
    int a = c.addNode({0, 0});
    int b = c.addNode({0, 100});
    // groundNodeId оставляем -1 (по умолчанию).
    c.addComponent(ComponentType::Resistor, a, b, 1000.0);

    CircuitValidation v = validateCircuit(c);
    EXPECT_FALSE(v.hasGround);
    EXPECT_NE(v.message.find("земл"), std::string::npos) << v.message;
}

// Плавающий компонент: узлы, не достижимые от земли → hasFloatingComponents.
TEST(CircuitValidator, FloatingComponentsDetected) {
    Circuit c;
    int gnd = c.addNode({0, 0});
    int top = c.addNode({0, 100});
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::VoltageSource, gnd, top, 5.0);
    c.addComponent(ComponentType::Resistor, top, gnd, 1000.0);
    // Изолированная пара узлов без пути до земли.
    int f1 = c.addNode({500, 0});
    int f2 = c.addNode({500, 100});
    c.addComponent(ComponentType::Resistor, f1, f2, 2000.0);

    CircuitValidation v = validateCircuit(c);
    EXPECT_TRUE(v.hasGround);
    EXPECT_TRUE(v.hasFloatingComponents);
    EXPECT_NE(v.message.find("плавающ"), std::string::npos) << v.message;
}

// Два источника напряжения между одной парой узлов с разными value → конфликт.
TEST(CircuitValidator, ConflictingSourcesDetected) {
    Circuit c;
    int gnd = c.addNode({0, 0});
    int top = c.addNode({0, 100});
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::VoltageSource, gnd, top, 5.0);
    c.addComponent(ComponentType::VoltageSource, gnd, top, 3.0); // конфликт: 5В vs 3В

    CircuitValidation v = validateCircuit(c);
    EXPECT_TRUE(v.conflictingSources);
    EXPECT_NE(v.message.find("онфликт"), std::string::npos) << v.message;
}

// Одинаковые источники между одной парой узлов конфликтом НЕ считаются.
TEST(CircuitValidator, IdenticalParallelSourcesAreNotConflict) {
    Circuit c;
    int gnd = c.addNode({0, 0});
    int top = c.addNode({0, 100});
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::VoltageSource, gnd, top, 5.0);
    c.addComponent(ComponentType::VoltageSource, gnd, top, 5.0); // то же значение
    c.addComponent(ComponentType::Resistor, top, gnd, 1000.0);

    CircuitValidation v = validateCircuit(c);
    EXPECT_FALSE(v.conflictingSources);
}

// Пустая цепь без узлов → понятное сообщение.
TEST(CircuitValidator, EmptyCircuitReportsNoNodes) {
    Circuit c;
    CircuitValidation v = validateCircuit(c);
    EXPECT_FALSE(v.hasGround);
    EXPECT_NE(v.message.find("узл"), std::string::npos) << v.message;
}

} // namespace
