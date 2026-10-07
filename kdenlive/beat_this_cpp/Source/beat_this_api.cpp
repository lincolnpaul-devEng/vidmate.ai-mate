#define MINIAUDIO_IMPLEMENTATION
#include "beat_this_api.h"
#include "MelSpectrogram.h"
#include "InferenceProcessor.h"
#include "Postprocessor.h"
#include "DBNPostprocessor.h"

#include <iostream>
#include <memory>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <set>
#include <stdexcept>
#include <codecvt>
#include <locale>
#include "miniaudio.h"
#include "CDSPResampler.h"
namespace BeatThis {

// Pimpl implementation
class BeatThis::Impl {
public:
    Ort::Env env;
    std::unique_ptr<Ort::Session> session;
    bool use_dbn;

    explicit Impl(const std::string& onnx_model_path, bool use_dbn_)
        : env(ORT_LOGGING_LEVEL_WARNING, "beat_this_cpp_api"), use_dbn(use_dbn_) {
        // Check if file exists
        std::ifstream file_check(onnx_model_path);
        if (!file_check.good()) {
            throw std::runtime_error("ONNX model file not found: " + onnx_model_path);
        }
        file_check.close();
        
        Ort::SessionOptions session_options;

#ifdef _WIN32
#include <codecvt>
#include <locale>
#endif
        
        #ifdef _WIN32
        // On Windows, ONNX Runtime expects a wide string path
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::wstring w_model_path = converter.from_bytes(onnx_model_path);
        const ORTCHAR_T* model_path_ort = w_model_path.c_str();
#else
        // On non-Windows, ONNX Runtime expects a narrow string path
        const ORTCHAR_T* model_path_ort = onnx_model_path.c_str();
#endif
        
        try {
            session = std::make_unique<Ort::Session>(env, model_path_ort, session_options);
        } catch (const Ort::Exception& e) {
            std::cerr << "ONNX Runtime error during session creation: " << e.what() << std::endl;
            std::cerr << "Error code: " << e.GetOrtErrorCode() << std::endl;
            throw std::runtime_error("ONNX Runtime error: " + std::string(e.what()));
        }
    }
};

BeatThis::BeatThis(const std::string& onnx_model_path, bool use_dbn)
    : pImpl(std::make_unique<Impl>(onnx_model_path, use_dbn)) {
}

BeatThis::~BeatThis() = default;

namespace {
    // Helper function to calculate beat counts
    std::vector<int> calculate_beat_counts(const std::vector<float>& beats, 
                                          const std::vector<float>& downbeats) {
        std::vector<int> beat_counts(beats.size());
        
        if (beats.empty() || downbeats.empty()) {
            return beat_counts;
        }

        // Check if all downbeats are beats
        std::set<float> beats_set(beats.begin(), beats.end());
        for (float db_time : downbeats) {
            if (beats_set.find(db_time) == beats_set.end()) {
                std::cerr << "Error: Not all downbeats are beats. Cannot calculate beat counts." << std::endl;
                return beat_counts;
            }
        }

        // Handle pickup measure and beat counting
        int start_counter = 1;
        if (downbeats.size() >= 2) {
            auto it_first_downbeat = std::lower_bound(beats.begin(), beats.end(), downbeats[0]);
            auto it_second_downbeat = std::lower_bound(beats.begin(), beats.end(), downbeats[1]);

            int first_downbeat_idx = std::distance(beats.begin(), it_first_downbeat);
            int second_downbeat_idx = std::distance(beats.begin(), it_second_downbeat);

            int beats_in_first_measure = second_downbeat_idx - first_downbeat_idx;
            int pickup_beats = first_downbeat_idx;

            if (pickup_beats < beats_in_first_measure) {
                start_counter = beats_in_first_measure - pickup_beats;
            } else {
                std::cerr << "WARNING: There are more beats in the pickup measure than in the first measure. "
                         << "The beat count will start from 2 without trying to estimate the length of the pickup measure." << std::endl;
                start_counter = 1;
            }
        } else {
            std::cerr << "WARNING: There are less than two downbeats in the predictions. Something may be wrong. "
                     << "The beat count will start from 2 without trying to estimate the length of the pickup measure." << std::endl;
            start_counter = 1;
        }

        // Calculate beat counts
        int counter = start_counter;
        size_t downbeat_idx = 0;
        float next_downbeat = (downbeat_idx < downbeats.size()) ? downbeats[downbeat_idx] : -1.0f;

        for (size_t i = 0; i < beats.size(); ++i) {
            float beat_time = beats[i];
            
            if (downbeat_idx < downbeats.size() && std::abs(beat_time - next_downbeat) < 1e-6f) {
                counter = 1;
                downbeat_idx++;
                next_downbeat = (downbeat_idx < downbeats.size()) ? downbeats[downbeat_idx] : -1.0f;
            } else {
                counter++;
            }
            beat_counts[i] = counter;
        }

        return beat_counts;
    }

    // Helper function to resample audio using r8brain (high quality, MIT license)
    bool resample_audio(const std::vector<float>& in_buffer, int in_rate,
                       std::vector<float>& out_buffer, int out_rate) {
        static constexpr int kChunkSize = 4096;
        r8b::CDSPResampler24 resampler(in_rate, out_rate, kChunkSize);

        const size_t total_in = in_buffer.size();
        const size_t estimated_out = static_cast<size_t>(
            static_cast<double>(total_in) * out_rate / in_rate + 1);
        out_buffer.reserve(estimated_out);

        // Convert float→double per chunk to avoid a full-size double copy (~84 MB)
        std::vector<double> chunk_d(kChunkSize);
        size_t pos = 0;
        while (pos < total_in) {
            const int chunk = static_cast<int>(std::min<size_t>(kChunkSize, total_in - pos));
            for (int i = 0; i < chunk; ++i) chunk_d[i] = in_buffer[pos + i];
            double* op = nullptr;
            const int nout = resampler.process(chunk_d.data(), chunk, op);
            for (int i = 0; i < nout; ++i) {
                out_buffer.push_back(static_cast<float>(op[i]));
            }
            pos += chunk;
        }

        return true;
    }

    // Helper function to convert to mono
    std::vector<float> convert_to_mono(const std::vector<float>& audio_data, int channels) {
        if (channels == 1) {
            return audio_data;
        }

        size_t num_frames = audio_data.size() / channels;
        std::vector<float> mono_buffer(num_frames);
        
        for (size_t i = 0; i < num_frames; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < channels; ++ch) {
                sum += audio_data[i * channels + ch];
            }
            mono_buffer[i] = sum / channels;
        }
        
        return mono_buffer;
    }
}

BeatResult BeatThis::process_audio(const std::vector<float>& audio_data, 
                                  int samplerate, int channels) {
    try {
        // Convert to mono
        auto mono_audio = convert_to_mono(audio_data, channels);
        
        // Resample if necessary
        std::vector<float> resampled_buffer;
        constexpr int target_samplerate = 22050;
        
        if (samplerate != target_samplerate) {
            if (!resample_audio(mono_audio, samplerate, resampled_buffer, target_samplerate)) {
                throw std::runtime_error("Failed to resample audio");
            }
            // Release mono_audio early (~42 MB for 4-min stereo) — no longer needed after resampling
            std::vector<float>().swap(mono_audio);
        } else {
            resampled_buffer = std::move(mono_audio);
        }

        // Compute Mel Spectrogram
        MelSpectrogram spect_computer;
        auto spectrogram = spect_computer.compute(resampled_buffer);

        // Run Inference
        InferenceProcessor processor(*(pImpl->session), pImpl->env);
        auto beat_downbeat_logits = processor.process_spectrogram(spectrogram);

        if (pImpl->use_dbn) {
            // DBN post-processing (madmom-compatible Viterbi beat tracking)
            const auto& beat_logits_f = beat_downbeat_logits.first;
            const auto& db_logits_f = beat_downbeat_logits.second;
            std::vector<double> beat_logits_d(beat_logits_f.begin(), beat_logits_f.end());
            std::vector<double> db_logits_d(db_logits_f.begin(), db_logits_f.end());

            auto combined_act = DBNPostprocessor::logits_to_combined_act(beat_logits_d, db_logits_d);
            DBNPostprocessor dbn;
            auto dbn_result = dbn.process(combined_act);

            // DBN returns beat_numbers directly; no conversion needed
            std::vector<float> beats;
            std::vector<float> downbeats;
            std::vector<int> beat_counts;
            for (size_t i = 0; i < dbn_result.times.size(); ++i) {
                float t = static_cast<float>(dbn_result.times[i]);
                beats.push_back(t);
                beat_counts.push_back(dbn_result.beat_numbers[i]);
                if (dbn_result.beat_numbers[i] == 1)
                    downbeats.push_back(t);
            }
            return BeatResult{std::move(beats), std::move(downbeats), std::move(beat_counts)};
        }

        // Minimal post-processing (default)
        Postprocessor postprocessor;
        auto beat_downbeat_times = postprocessor.process(
            beat_downbeat_logits.first, beat_downbeat_logits.second);

        // Calculate beat counts
        std::vector<int> beat_counts = calculate_beat_counts(
            beat_downbeat_times.first, beat_downbeat_times.second);

        return BeatResult{
            std::move(beat_downbeat_times.first),
            std::move(beat_downbeat_times.second),
            std::move(beat_counts)
        };

    } catch (const Ort::Exception& e) {
        std::cerr << "ONNX Runtime error: " << e.what() << std::endl;
        std::cerr << "Error code: " << e.GetOrtErrorCode() << std::endl;
        throw std::runtime_error("ONNX Runtime error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        std::cerr << "Processing error: " << e.what() << std::endl;
        throw std::runtime_error("Processing error: " + std::string(e.what()));
    }
}

BeatResult BeatThis::process_audio(const float* audio_data, size_t num_samples, 
                                  int samplerate, int channels) {
    std::vector<float> audio_vector(audio_data, audio_data + num_samples);
    return process_audio(audio_vector, samplerate, channels);
}


} // namespace BeatThis