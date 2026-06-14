#pragma once

#include <string>
#include <vector>

#include "solver/CircuitSolver.h"
#include "physics/ThermalModel.h"

namespace current_lab::simulation {

inline constexpr int kSignalRingSize = 512;

// One oscilloscope trace: a fixed-length ring of samples. `head` is the index
// where the NEXT sample will be written; `count` is how many samples are valid
// (<= kSignalRingSize). For ImGui::PlotLines pass (ring, count, head) so the
// offset unwraps the ring chronologically.
struct SignalChannel {
    enum class Kind { NodeV, BranchI, ElemT, ElemP };
    std::string label;
    Kind kind = Kind::NodeV;
    int ref = -1;                       // nodeId for NodeV; componentId otherwise
    float ring[kSignalRingSize] = {};
    int head = 0;
    int count = 0;
};

class SignalRecorder {
public:
    int addChannel(const std::string& label, SignalChannel::Kind kind, int ref) {
        m_channels.push_back({label, kind, ref, {}, 0, 0});
        return static_cast<int>(m_channels.size()) - 1;
    }

    void removeChannel(int index) {
        if (index >= 0 && index < static_cast<int>(m_channels.size()))
            m_channels.erase(m_channels.begin() + index);
    }

    void clear() { m_channels.clear(); }

    int channelCount() const { return static_cast<int>(m_channels.size()); }

    const SignalChannel& channel(int index) const { return m_channels[index]; }

    std::vector<SignalChannel>& channels() { return m_channels; }

    const std::vector<SignalChannel>& channels() const { return m_channels; }

    // Append ONE sample to EVERY channel from the current solution + thermal state.
    // `time` is accepted for future use (timebase) but need not be stored.
    void sample(const CircuitSolution& solution,
                const current_lab::physics::ThermalState& thermal, double time) {
        for (auto& ch : m_channels) {
            double value = 0.0;
            switch (ch.kind) {
            case SignalChannel::Kind::NodeV:
                for (const auto& sp : solution.nodePotentials) {
                    if (sp.nodeId == ch.ref) {
                        value = sp.potential;
                        break;
                    }
                }
                break;
            case SignalChannel::Kind::BranchI:
                for (const auto& br : solution.branches) {
                    if (br.componentId == ch.ref) {
                        value = br.current;
                        break;
                    }
                }
                break;
            case SignalChannel::Kind::ElemP:
                for (const auto& br : solution.branches) {
                    if (br.componentId == ch.ref) {
                        value = br.power;
                        break;
                    }
                }
                break;
            case SignalChannel::Kind::ElemT:
                value = current_lab::physics::celsius(
                    current_lab::physics::temperatureFor(thermal, ch.ref));
                break;
            }
            ch.ring[ch.head] = static_cast<float>(value);
            ch.head = (ch.head + 1) % kSignalRingSize;
            if (ch.count < kSignalRingSize)
                ++ch.count;
        }
    }

private:
    std::vector<SignalChannel> m_channels;
};

} // namespace current_lab::simulation
