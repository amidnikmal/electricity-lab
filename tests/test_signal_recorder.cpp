#include <gtest/gtest.h>
#include "physics/PhysicalUnits.h"
#include "physics/ThermalModel.h"
#include "simulation/SignalRecorder.h"
#include "solver/CircuitSolver.h"

namespace {

using current_lab::simulation::SignalChannel;
using current_lab::simulation::SignalRecorder;
using current_lab::simulation::kSignalRingSize;

namespace physics = current_lab::physics;

CircuitSolution makeNodeSolution(int nodeId, double potential) {
    CircuitSolution solution;
    solution.nodePotentials.push_back(SolutionPoint{nodeId, potential});
    return solution;
}

float newestSample(const SignalChannel& channel) {
    return channel.ring[(channel.head - 1 + kSignalRingSize) % kSignalRingSize];
}

} // namespace

TEST(SignalRecorder, AddChannelInitializesChannelsAndClearEmpties) {
    SignalRecorder recorder;

    int nodeIndex = recorder.addChannel("node voltage", SignalChannel::Kind::NodeV, 7);
    int branchIndex = recorder.addChannel("branch current", SignalChannel::Kind::BranchI, 11);

    EXPECT_EQ(nodeIndex, 0);
    EXPECT_EQ(branchIndex, 1);
    EXPECT_EQ(recorder.channelCount(), 2);

    const SignalChannel& node = recorder.channel(nodeIndex);
    EXPECT_EQ(node.label, "node voltage");
    EXPECT_EQ(node.kind, SignalChannel::Kind::NodeV);
    EXPECT_EQ(node.ref, 7);
    EXPECT_EQ(node.head, 0);
    EXPECT_EQ(node.count, 0);

    const SignalChannel& branch = recorder.channel(branchIndex);
    EXPECT_EQ(branch.label, "branch current");
    EXPECT_EQ(branch.kind, SignalChannel::Kind::BranchI);
    EXPECT_EQ(branch.ref, 11);
    EXPECT_EQ(branch.head, 0);
    EXPECT_EQ(branch.count, 0);

    recorder.clear();
    EXPECT_EQ(recorder.channelCount(), 0);
    EXPECT_TRUE(recorder.channels().empty());
}

TEST(SignalRecorder, NodeVoltageSamplesFillChronologicallyBelowCapacity) {
    constexpr int kNodeId = 17;
    constexpr int kSamples = 5;
    const float values[kSamples] = {0.25f, 1.5f, -2.0f, 3.75f, 8.125f};

    SignalRecorder recorder;
    int channelIndex = recorder.addChannel("node", SignalChannel::Kind::NodeV, kNodeId);
    physics::ThermalState thermal;

    for (int i = 0; i < kSamples; ++i)
        recorder.sample(makeNodeSolution(kNodeId, values[i]), thermal, static_cast<double>(i));

    const SignalChannel& channel = recorder.channel(channelIndex);
    EXPECT_EQ(channel.head, kSamples);
    EXPECT_EQ(channel.count, kSamples);
    for (int i = 0; i < kSamples; ++i)
        EXPECT_FLOAT_EQ(channel.ring[i], values[i]);
}

TEST(SignalRecorder, NodeVoltageSamplesWrapAndKeepMostRecentValues) {
    constexpr int kNodeId = 23;

    SignalRecorder recorder;
    int channelIndex = recorder.addChannel("node", SignalChannel::Kind::NodeV, kNodeId);
    physics::ThermalState thermal;

    for (int i = 0; i < kSignalRingSize + 3; ++i)
        recorder.sample(makeNodeSolution(kNodeId, static_cast<double>(i)), thermal,
                        static_cast<double>(i));

    const SignalChannel& channel = recorder.channel(channelIndex);
    ASSERT_EQ(channel.count, kSignalRingSize);
    EXPECT_EQ(channel.head, 3);

    for (int i = 0; i < channel.count; ++i) {
        int ringIndex = (channel.head + i) % kSignalRingSize;
        EXPECT_FLOAT_EQ(channel.ring[ringIndex], static_cast<float>(i + 3));
    }
}

TEST(SignalRecorder, SampleWritesExpectedValueForEachSignalKind) {
    constexpr int kNodeId = 101;
    constexpr int kBranchId = 202;
    constexpr int kThermalId = 303;
    constexpr double kTemperature = physics::kAmbientTemperature + 12.5;

    SignalRecorder recorder;
    int nodeIndex = recorder.addChannel("node", SignalChannel::Kind::NodeV, kNodeId);
    int currentIndex = recorder.addChannel("current", SignalChannel::Kind::BranchI, kBranchId);
    int powerIndex = recorder.addChannel("power", SignalChannel::Kind::ElemP, kBranchId);
    int temperatureIndex = recorder.addChannel("temperature", SignalChannel::Kind::ElemT, kThermalId);

    CircuitSolution solution;
    solution.nodePotentials.push_back(SolutionPoint{kNodeId, 3.75});
    solution.branches.push_back(BranchResult{kBranchId, 0.125, 4.0, 0.5});

    physics::ThermalState thermal;
    thermal.temperature[kThermalId] = kTemperature;

    recorder.sample(solution, thermal, 1.0);

    EXPECT_FLOAT_EQ(newestSample(recorder.channel(nodeIndex)), 3.75f);
    EXPECT_FLOAT_EQ(newestSample(recorder.channel(currentIndex)), 0.125f);
    EXPECT_FLOAT_EQ(newestSample(recorder.channel(powerIndex)), 0.5f);
    EXPECT_NEAR(newestSample(recorder.channel(temperatureIndex)),
                static_cast<float>(physics::celsius(kTemperature)), 1e-5f);
}

TEST(SignalRecorder, AbsentElectricalRefsSampleAsZero) {
    SignalRecorder recorder;
    int nodeIndex = recorder.addChannel("missing node", SignalChannel::Kind::NodeV, 42);
    int currentIndex = recorder.addChannel("missing current", SignalChannel::Kind::BranchI, 43);
    int powerIndex = recorder.addChannel("missing power", SignalChannel::Kind::ElemP, 44);

    CircuitSolution solution;
    solution.nodePotentials.push_back(SolutionPoint{1, 9.0});
    solution.branches.push_back(BranchResult{2, 0.25, 1.0, 0.75});

    physics::ThermalState thermal;
    recorder.sample(solution, thermal, 0.0);

    EXPECT_FLOAT_EQ(newestSample(recorder.channel(nodeIndex)), 0.0f);
    EXPECT_FLOAT_EQ(newestSample(recorder.channel(currentIndex)), 0.0f);
    EXPECT_FLOAT_EQ(newestSample(recorder.channel(powerIndex)), 0.0f);
}
