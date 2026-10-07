#include "DBNPostprocessor.h"
#include <algorithm>
#include <cmath>
#include <cassert>
#include <numeric>
#include <limits>

static constexpr double NEG_INF = -std::numeric_limits<double>::infinity();

// ===== BarStateSpace =====

void BarStateSpace::build(int num_beats_in, double min_interval, double max_interval, int num_tempi) {
    num_beats = num_beats_in;

    // Build BeatStateSpace intervals (same logic as madmom beats_hmm.py)
    int imin = static_cast<int>(std::round(min_interval));
    int imax = static_cast<int>(std::round(max_interval));

    // Default: linear spacing
    intervals.clear();
    for (int i = imin; i <= imax; ++i)
        intervals.push_back(i);

    // If num_tempi is specified and fewer than the total interval count, use log spacing
    if (num_tempi > 0 && num_tempi < static_cast<int>(intervals.size())) {
        int n = num_tempi;
        while (true) {
            // logspace from log2(min) to log2(max), n points, base 2
            std::vector<double> raw(n);
            double log_min = std::log2(min_interval);
            double log_max = std::log2(max_interval);
            for (int i = 0; i < n; ++i)
                raw[i] = std::pow(2.0, log_min + (log_max - log_min) * i / (n - 1));
            // round and unique
            std::vector<int> rounded;
            for (double v : raw)
                rounded.push_back(static_cast<int>(std::round(v)));
            std::sort(rounded.begin(), rounded.end());
            rounded.erase(std::unique(rounded.begin(), rounded.end()), rounded.end());
            if (static_cast<int>(rounded.size()) >= num_tempi) {
                intervals = rounded;
                break;
            }
            ++n;
        }
    }

    num_intervals = static_cast<int>(intervals.size());

    // Compute BeatStateSpace: num_states, first_states, last_states, state_positions, state_intervals
    int beat_num_states = 0;
    for (int iv : intervals)
        beat_num_states += iv;

    std::vector<int> beat_first(num_intervals), beat_last(num_intervals);
    {
        int idx = 0;
        for (int i = 0; i < num_intervals; ++i) {
            beat_first[i] = idx;
            beat_last[i] = idx + intervals[i] - 1;
            idx += intervals[i];
        }
    }

    std::vector<double> beat_pos(beat_num_states);
    std::vector<int> beat_iv(beat_num_states);
    {
        int idx = 0;
        for (int i = 0; i < num_intervals; ++i) {
            int iv = intervals[i];
            for (int j = 0; j < iv; ++j) {
                beat_pos[idx] = static_cast<double>(j) / iv;
                beat_iv[idx] = iv;
                ++idx;
            }
        }
    }

    // BarStateSpace: stack num_beats BeatStateSpaces
    num_states = num_beats * beat_num_states;
    state_positions.resize(num_states);
    state_intervals.resize(num_states);
    first_states.resize(num_beats);
    last_states.resize(num_beats);

    for (int b = 0; b < num_beats; ++b) {
        int offset = b * beat_num_states;
        first_states[b].resize(num_intervals);
        last_states[b].resize(num_intervals);

        for (int i = 0; i < beat_num_states; ++i) {
            state_positions[offset + i] = beat_pos[i] + b;
            state_intervals[offset + i] = beat_iv[i];
        }
        for (int i = 0; i < num_intervals; ++i) {
            first_states[b][i] = beat_first[i] + offset;
            last_states[b][i] = beat_last[i] + offset;
        }
    }
}

// ===== exponential_transition =====
// Corresponds to madmom's exponential_transition
// from_intervals, to_intervals: interval per tempo state
// returns: prob[from_idx][to_idx]
static std::vector<std::vector<double>> exponential_transition(
    const std::vector<int>& from_intervals,
    const std::vector<int>& to_intervals,
    double lambda,
    double threshold = std::numeric_limits<double>::epsilon()  // np.spacing(1) ≈ 2.22e-16
) {
    int nf = from_intervals.size();
    int nt = to_intervals.size();
    std::vector<std::vector<double>> prob(nf, std::vector<double>(nt));

    for (int i = 0; i < nf; ++i) {
        double row_sum = 0.0;
        for (int j = 0; j < nt; ++j) {
            double ratio = static_cast<double>(to_intervals[j]) / from_intervals[i];
            double p = std::exp(-lambda * std::abs(ratio - 1.0));
            if (p <= threshold) p = 0.0;
            prob[i][j] = p;
            row_sum += p;
        }
        // normalize
        if (row_sum > 0.0) {
            for (int j = 0; j < nt; ++j)
                prob[i][j] /= row_sum;
        }
    }
    return prob;
}

// ===== BarTransitionModel =====

void BarTransitionModel::build(const BarStateSpace& ss, double transition_lambda) {
    // Collect transitions in COO format: (dest_state, src_state, prob)
    // Sort order matches scipy's csr_matrix: dest ascending, then src ascending within each dest

    // COO
    std::vector<uint32_t> coo_dest, coo_src;
    std::vector<double> coo_prob;

    int beat_num_states = ss.num_states / ss.num_beats;
    int total_states = ss.num_states;

    // --- 1. Within-beat transitions: i+1 → i (sequential steps within each beat)
    // For every state that is not a first_state, prev = state - 1
    std::vector<bool> is_first(total_states, false);
    for (int b = 0; b < ss.num_beats; ++b)
        for (int f : ss.first_states[b])
            is_first[f] = true;

    for (int s = 0; s < total_states; ++s) {
        if (!is_first[s]) {
            coo_dest.push_back(s);
            coo_src.push_back(s - 1);
            coo_prob.push_back(1.0);
        }
    }

    // --- 2. Beat-boundary tempo transitions (exponential distribution)
    for (int beat = 0; beat < ss.num_beats; ++beat) {
        const auto& to_states = ss.first_states[beat];
        const auto& from_states = ss.last_states[(beat - 1 + ss.num_beats) % ss.num_beats];

        // Retrieve the interval value for each from/to tempo state
        std::vector<int> from_int(ss.num_intervals), to_int(ss.num_intervals);
        for (int i = 0; i < ss.num_intervals; ++i) {
            from_int[i] = ss.state_intervals[from_states[i]];
            to_int[i] = ss.state_intervals[to_states[i]];
        }

        auto prob = exponential_transition(from_int, to_int, transition_lambda);

        for (int i = 0; i < ss.num_intervals; ++i) {
            for (int j = 0; j < ss.num_intervals; ++j) {
                if (prob[i][j] > 0.0) {
                    coo_dest.push_back(static_cast<uint32_t>(to_states[j]));
                    coo_src.push_back(static_cast<uint32_t>(from_states[i]));
                    coo_prob.push_back(prob[i][j]);
                }
            }
        }
    }

    // --- Build CSR: sort by dest ascending, then src ascending within each dest
    // This matches the column ordering guaranteed by scipy's csr_matrix
    std::vector<size_t> order(coo_dest.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (coo_dest[a] != coo_dest[b]) return coo_dest[a] < coo_dest[b];
        return coo_src[a] < coo_src[b];
    });

    size_t n_trans = coo_dest.size();
    states.resize(n_trans);
    probabilities.resize(n_trans);
    log_probabilities.resize(n_trans);

    for (size_t k = 0; k < n_trans; ++k) {
        states[k] = coo_src[order[k]];
        probabilities[k] = coo_prob[order[k]];
        log_probabilities[k] = std::log(coo_prob[order[k]]);
    }

    // pointers: pointers[dest] = first index in states[] for that destination
    pointers.assign(total_states + 1, 0);
    for (size_t k = 0; k < n_trans; ++k)
        pointers[coo_dest[order[k]] + 1]++;
    for (int s = 0; s < total_states; ++s)
        pointers[s + 1] += pointers[s];
}

// ===== RNNDownBeatObservationModel =====

void RNNDownBeatObservationModel::build(const BarStateSpace& ss, int obs_lambda) {
    observation_lambda = obs_lambda;
    double border = 1.0 / obs_lambda;

    pointers.resize(ss.num_states);
    for (int s = 0; s < ss.num_states; ++s) {
        double pos = ss.state_positions[s];
        double pos_mod = std::fmod(pos, 1.0);
        if (pos_mod < 0) pos_mod += 1.0;  // guard against floating-point rounding
        if (pos < border)        // downbeat (first beat, position < border)
            pointers[s] = 2;
        else if (pos_mod < border)  // beat (non-first beat, position % 1 < border)
            pointers[s] = 1;
        else                     // no-beat
            pointers[s] = 0;
    }
}

std::vector<std::vector<double>> RNNDownBeatObservationModel::log_densities(
    const std::vector<std::array<double, 2>>& observations) const
{
    int N = observations.size();
    std::vector<std::vector<double>> ld(N, std::vector<double>(3));
    double lambda_minus_1 = observation_lambda - 1.0;
    for (int t = 0; t < N; ++t) {
        double beat_act = observations[t][0];
        double db_act = observations[t][1];
        double no_beat = (1.0 - (beat_act + db_act)) / lambda_minus_1;
        ld[t][0] = std::log(no_beat);
        ld[t][1] = std::log(beat_act);
        ld[t][2] = std::log(db_act);
    }
    return ld;
}

// ===== HMM Viterbi (checkpointed) =====
//
// Memory: O(N * sqrt(T)) instead of O(N * T) for the backtracking matrix.
// Time:   O(N * T * 2) — forward pass once + one re-run per segment during backtrack.
//
// Algorithm:
//   Forward pass: store Viterbi values only at every K-th frame (checkpoint).
//   Backtrack: for each segment [j*K .. (j+1)*K-1], re-run that segment from its
//   checkpoint with full BT storage (K * N * 4 bytes peak), then trace back.
//   Hand-off between segments: after tracing segment j, `state` equals path[j*K-1],
//   which is the required end-state for segment j-1.

HMM::ViterbiResult HMM::viterbi(
    const std::vector<std::array<double, 2>>& observations) const
{
    const int T = static_cast<int>(observations.size());
    if (T == 0) return {};

    const auto& tm_states = tm->states;
    const auto& tm_ptr    = tm->pointers;
    const auto& tm_logp   = tm->log_probabilities;

    auto log_dens = om->log_densities(observations);

    const double log_init = -std::log(static_cast<double>(num_states));

    // Checkpoint interval K ≈ sqrt(T)
    const int K        = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(T))));
    const int num_segs = (T + K - 1) / K;

    // checkpoint[j] = Viterbi values *before* processing frame j*K
    // (= initial distribution for j=0, accumulated state otherwise)
    std::vector<std::vector<double>> checkpoints(num_segs);
    checkpoints[0].assign(num_states, log_init);

    std::vector<double> prev(num_states, log_init), curr(num_states);

    for (int t = 0; t < T; ++t) {
        for (int s = 0; s < num_states; ++s) {
            curr[s] = NEG_INF;
            const double density  = log_dens[t][om->pointers[s]];
            const uint32_t p_end  = tm_ptr[s + 1];
            for (uint32_t ptr = tm_ptr[s]; ptr < p_end; ++ptr) {
                const double val = prev[tm_states[ptr]] + tm_logp[ptr] + density;
                if (val > curr[s]) curr[s] = val;
            }
        }
        std::swap(prev, curr);

        // Store checkpoint at the start of the next segment
        const int next_seg = (t + 1) / K;
        if (next_seg < num_segs && (t + 1) == next_seg * K) {
            checkpoints[next_seg] = prev;
        }
    }
    // prev == Viterbi values after the last frame

    // Find the best final state
    int    best_state = 0;
    double best_logp  = NEG_INF;
    for (int s = 0; s < num_states; ++s) {
        if (prev[s] > best_logp) { best_logp = prev[s]; best_state = s; }
    }

    ViterbiResult res;
    res.log_prob = best_logp;
    if (std::isinf(best_logp)) return res;

    res.path.resize(T);

    // Backtrack: process segments in reverse order.
    // `state` starts as the globally best final state.
    int state = best_state;

    for (int seg = num_segs - 1; seg >= 0; --seg) {
        const int t_start = seg * K;
        const int t_end   = std::min(T, (seg + 1) * K);
        const int seg_len = t_end - t_start;

        // Re-run forward for this segment from its checkpoint, storing BT pointers.
        // seg_bt[i][s] = predecessor of state s when computing frame t_start+i.
        std::vector<std::vector<uint32_t>> seg_bt(
            seg_len, std::vector<uint32_t>(num_states, 0));

        std::vector<double> seg_prev = checkpoints[seg];
        std::vector<double> seg_curr(num_states);

        for (int i = 0; i < seg_len; ++i) {
            const int t = t_start + i;
            for (int s = 0; s < num_states; ++s) {
                seg_curr[s] = NEG_INF;
                const double density  = log_dens[t][om->pointers[s]];
                uint32_t best_pred    = 0;
                double   best_val     = NEG_INF;
                const uint32_t p_end  = tm_ptr[s + 1];
                for (uint32_t ptr = tm_ptr[s]; ptr < p_end; ++ptr) {
                    const uint32_t ps = tm_states[ptr];
                    const double   val = seg_prev[ps] + tm_logp[ptr] + density;
                    if (val > best_val) { best_val = val; best_pred = ps; }
                }
                seg_curr[s]  = best_val;
                seg_bt[i][s] = best_pred;
            }
            std::swap(seg_prev, seg_curr);
        }

        // Trace back through this segment.
        // After the loop, `state` becomes the predecessor of path[t_start],
        // which is path[t_start-1] — the required end-state for segment seg-1.
        for (int i = seg_len - 1; i >= 0; --i) {
            res.path[t_start + i] = static_cast<uint32_t>(state);
            state = seg_bt[i][state];
        }
    }

    return res;
}

// ===== DBNPostprocessor =====

DBNPostprocessor::DBNPostprocessor(const Config& cfg) : cfg_(cfg) {
    double min_interval = 60.0 * cfg_.fps / cfg_.max_bpm;
    double max_interval = 60.0 * cfg_.fps / cfg_.min_bpm;

    hmms_.resize(cfg_.beats_per_bar.size());
    for (size_t i = 0; i < cfg_.beats_per_bar.size(); ++i) {
        auto& h = hmms_[i];
        h.ss.build(cfg_.beats_per_bar[i], min_interval, max_interval);
        h.tm.build(h.ss, cfg_.transition_lambda);
        h.om.build(h.ss, cfg_.observation_lambda);
        h.hmm.tm = &h.tm;
        h.hmm.om = &h.om;
        h.hmm.num_states = h.ss.num_states;
    }
}

std::pair<std::vector<std::array<double, 2>>, int>
DBNPostprocessor::threshold_activations(
    const std::vector<std::array<double, 2>>& act, double threshold)
{
    int first = 0, last = static_cast<int>(act.size());

    int first_idx = -1, last_idx = -1;
    for (int i = 0; i < static_cast<int>(act.size()); ++i) {
        if (act[i][0] >= threshold || act[i][1] >= threshold) {
            if (first_idx < 0) first_idx = i;
            last_idx = i;
        }
    }
    if (first_idx < 0)
        return {{}, 0};  // no frame exceeds the threshold

    first = first_idx;
    last = last_idx + 1;
    std::vector<std::array<double, 2>> trimmed(act.begin() + first, act.begin() + last);
    return {trimmed, first};
}

DBNPostprocessor::Result DBNPostprocessor::process(
    const std::vector<std::array<double, 2>>& combined_act) const
{
    auto [act_thresh, first] = threshold_activations(combined_act, cfg_.threshold);

    if (act_thresh.empty())
        return {};

    // Run Viterbi on each HMM
    std::vector<HMM::ViterbiResult> results(hmms_.size());
    for (size_t i = 0; i < hmms_.size(); ++i)
        results[i] = hmms_[i].hmm.viterbi(act_thresh);

    // Select the best HMM (highest log_prob)
    int best = 0;
    for (size_t i = 1; i < results.size(); ++i)
        if (results[i].log_prob > results[best].log_prob)
            best = static_cast<int>(i);

    const auto& path = results[best].path;
    const auto& ss = hmms_[best].ss;
    const auto& om = hmms_[best].om;

    // Derive beat numbers from state_positions
    std::vector<int> beat_numbers(path.size());
    for (size_t i = 0; i < path.size(); ++i)
        beat_numbers[i] = static_cast<int>(ss.state_positions[path[i]]) + 1;

    Result result;

    if (cfg_.correct) {
        // beat_range: frames where om.pointers[state] >= 1 (beat or downbeat)
        std::vector<bool> beat_range(path.size());
        for (size_t i = 0; i < path.size(); ++i)
            beat_range[i] = (om.pointers[path[i]] >= 1);

        if (std::none_of(beat_range.begin(), beat_range.end(), [](bool v){ return v; }))
            return {};

        // Find rising/falling edges of beat_range
        std::vector<int> idx;
        if (beat_range[0]) idx.push_back(0);
        for (size_t i = 1; i < beat_range.size(); ++i) {
            if (static_cast<int>(beat_range[i]) != static_cast<int>(beat_range[i-1]))
                idx.push_back(static_cast<int>(i));
        }
        if (beat_range.back()) idx.push_back(static_cast<int>(beat_range.size()));

        // For each True segment, pick the frame with the highest activation
        for (size_t k = 0; k + 1 < idx.size(); k += 2) {
            int left = idx[k];
            int right = idx[k + 1];
            // argmax over both columns (mirrors Python: argmax(act[left:right]) // 2)
            int best_peak = left;
            double best_val = NEG_INF;
            for (int i = left; i < right; ++i) {
                // compare both columns
                if (act_thresh[i][0] > best_val) { best_val = act_thresh[i][0]; best_peak = i; }
                if (act_thresh[i][1] > best_val) { best_val = act_thresh[i][1]; best_peak = i; }
            }
            result.times.push_back(static_cast<double>(best_peak + first) / cfg_.fps);
            result.beat_numbers.push_back(beat_numbers[best_peak]);
        }
    } else {
        // Emit a beat at each transition of beat_numbers
        for (size_t i = 1; i < beat_numbers.size(); ++i) {
            if (beat_numbers[i] != beat_numbers[i-1]) {
                result.times.push_back(static_cast<double>(static_cast<int>(i) + first) / cfg_.fps);
                result.beat_numbers.push_back(beat_numbers[i]);
            }
        }
    }

    return result;
}

std::vector<std::array<double, 2>> DBNPostprocessor::logits_to_combined_act(
    const std::vector<double>& beat_logits,
    const std::vector<double>& downbeat_logits)
{
    assert(beat_logits.size() == downbeat_logits.size());
    constexpr double epsilon = 1e-5;
    size_t N = beat_logits.size();
    std::vector<std::array<double, 2>> out(N);
    for (size_t i = 0; i < N; ++i) {
        double bp = 1.0 / (1.0 + std::exp(-beat_logits[i]));
        double dp = 1.0 / (1.0 + std::exp(-downbeat_logits[i]));
        bp = bp * (1.0 - epsilon) + epsilon / 2.0;
        dp = dp * (1.0 - epsilon) + epsilon / 2.0;
        out[i][0] = std::max(bp - dp, epsilon / 2.0);
        out[i][1] = dp;
    }
    return out;
}
