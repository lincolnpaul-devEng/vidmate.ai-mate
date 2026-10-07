#pragma once

#include <vector>
#include <array>
#include <cstdint>

// ===== BarStateSpace =====
// Corresponds to madmom's BarStateSpace (beats_hmm.py)
struct BarStateSpace {
    int num_beats;
    int num_states;
    int num_intervals;            // intervals per beat
    std::vector<int> intervals;   // beat intervals (tempo states), length = num_intervals

    std::vector<double> state_positions;   // [0, num_beats), length = num_states
    std::vector<int> state_intervals;      // interval for each state, length = num_states

    // first_states[b] / last_states[b]: per-beat arrays of size num_intervals
    std::vector<std::vector<int>> first_states;
    std::vector<std::vector<int>> last_states;

    void build(int num_beats, double min_interval, double max_interval, int num_tempi = -1);
};

// ===== BarTransitionModel (sparse CSR) =====
// Corresponds to madmom's BarTransitionModel (beats_hmm.py)
// scipy CSR format: row = destination state, col = source state
struct BarTransitionModel {
    std::vector<uint32_t> states;       // source states (CSR indices)
    std::vector<uint32_t> pointers;     // CSR row pointers, length = num_states + 1
    std::vector<double> probabilities;  // transition probabilities
    std::vector<double> log_probabilities;

    void build(const BarStateSpace& ss, double transition_lambda);
};

// ===== RNNDownBeatTrackingObservationModel =====
// Corresponds to madmom's RNNDownBeatTrackingObservationModel (beats_hmm.py)
struct RNNDownBeatObservationModel {
    std::vector<uint32_t> pointers;  // 0=no-beat, 1=beat, 2=downbeat
    int observation_lambda;

    void build(const BarStateSpace& ss, int observation_lambda);

    // observations: shape (N, 2) = [beat_act, downbeat_act]
    // returns: shape (N, 3) = [log_no_beat, log_beat, log_downbeat]
    std::vector<std::vector<double>> log_densities(
        const std::vector<std::array<double, 2>>& observations) const;
};

// ===== HMM (Viterbi) =====
struct HMM {
    const BarTransitionModel* tm;
    const RNNDownBeatObservationModel* om;
    int num_states;

    struct ViterbiResult {
        std::vector<uint32_t> path;
        double log_prob;
    };

    ViterbiResult viterbi(const std::vector<std::array<double, 2>>& observations) const;
};

// ===== DBNDownBeatTrackingProcessor =====
// Corresponds to madmom's DBNDownBeatTrackingProcessor

struct DBNConfig {
    std::vector<int> beats_per_bar;
    double min_bpm = 55.0;
    double max_bpm = 215.0;
    int fps = 50;
    double transition_lambda = 100.0;
    int observation_lambda = 16;
    double threshold = 0.05;
    bool correct = true;

    DBNConfig() : beats_per_bar({3, 4}) {}
};

class DBNPostprocessor {
public:
    using Config = DBNConfig;

    explicit DBNPostprocessor(const Config& cfg = Config{});

    // combined_act: shape (N, 2) where col0=beat_act, col1=downbeat_act
    // returns: beat times [seconds] and beat numbers (1-indexed, 1=downbeat)
    struct Result {
        std::vector<double> times;
        std::vector<int> beat_numbers;
    };
    Result process(const std::vector<std::array<double, 2>>& combined_act) const;

    // Construct combined_act from raw sigmoid logits (beat_this output)
    static std::vector<std::array<double, 2>> logits_to_combined_act(
        const std::vector<double>& beat_logits,
        const std::vector<double>& downbeat_logits);

    // Corresponds to madmom's threshold_activations (public for testing)
    static std::pair<std::vector<std::array<double, 2>>, int>
    threshold_activations(const std::vector<std::array<double, 2>>& act, double threshold);

private:
    Config cfg_;

    struct BarHMM {
        BarStateSpace ss;
        BarTransitionModel tm;
        RNNDownBeatObservationModel om;
        HMM hmm;
    };
    std::vector<BarHMM> hmms_;
};
