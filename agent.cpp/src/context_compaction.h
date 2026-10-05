#pragma once

#include "chat.h"
#include "model.h"
#include "tool_result.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace agent_cpp {

using json = nlohmann::json;

/// @brief Configuration and budget parameters for context compaction
struct ContextBudget {
    size_t context_window_tokens = 128000;
    size_t max_output_tokens = 4096;
    float trigger_fraction = 0.70f;
    size_t reserve_tokens = 8192;
    size_t recent_target_tokens = 16000;
    size_t max_tool_result_chars = 16000;
};

/// @brief Estimate the number of tokens in a string (heuristic if model not available)
size_t estimate_text_tokens(const std::string& text, Model* model = nullptr);

/// @brief Estimate total tokens across a message history
size_t estimate_context_tokens(const std::vector<common_chat_msg>& messages,
                               const std::vector<common_chat_tool>& tools = {},
                               Model* model = nullptr);

/// @brief Compact a raw tool result string to prevent blowing the context window
std::string compact_tool_result(const std::string& raw_output,
                                size_t max_chars = 16000);

/// @brief Perform context compaction on messages if token estimate exceeds budget threshold
/// Returns true if compaction was performed
bool maybe_compact_context(std::vector<common_chat_msg>& messages,
                           const std::vector<common_chat_tool>& tools,
                           const ContextBudget& budget,
                           Model* model = nullptr);

} // namespace agent_cpp
