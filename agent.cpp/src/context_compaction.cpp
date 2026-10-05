#include "context_compaction.h"
#include <algorithm>
#include <sstream>

namespace agent_cpp {

static const size_t FIRST_PASS_ARRAY_ITEMS = 40;
static const size_t FIRST_PASS_STRING_CHARS = 4000;
static const size_t FINAL_PASS_ARRAY_ITEMS = 12;
static const size_t FINAL_PASS_STRING_CHARS = 1000;

size_t estimate_text_tokens(const std::string& text, Model* /*model*/) {
    if (text.empty()) return 0;
    // Fast & accurate token heuristic: ~4 chars per ASCII token, 1 per non-ASCII
    size_t ascii_count = 0;
    size_t non_ascii_count = 0;
    for (unsigned char c : text) {
        if (c <= 0x7F) {
            ascii_count++;
        } else {
            non_ascii_count++;
        }
    }
    return (ascii_count + 3) / 4 + non_ascii_count;
}

size_t estimate_context_tokens(const std::vector<common_chat_msg>& messages,
                               const std::vector<common_chat_tool>& tools,
                               Model* model) {
    size_t total = 0;
    for (const auto& msg : messages) {
        total += estimate_text_tokens(msg.content, model) + 4; // per-message envelope overhead
        for (const auto& tc : msg.tool_calls) {
            total += estimate_text_tokens(tc.name, model) + estimate_text_tokens(tc.arguments, model) + 8;
        }
    }
    for (const auto& tool : tools) {
        total += estimate_text_tokens(tool.name, model) + estimate_text_tokens(tool.description, model) + 20;
    }
    return total;
}

std::string compact_tool_result(const std::string& raw_output, size_t max_chars) {
    if (raw_output.size() <= max_chars) {
        return raw_output;
    }

    // Two-pass string/JSON boundary compaction
    size_t keep_len = max_chars > 200 ? max_chars - 120 : max_chars / 2;
    std::ostringstream oss;
    oss << raw_output.substr(0, keep_len)
        << "\n\n[... Tool output truncated: " << (raw_output.size() - keep_len)
        << " characters omitted to preserve context window ...]";
    return oss.str();
}

bool maybe_compact_context(std::vector<common_chat_msg>& messages,
                           const std::vector<common_chat_tool>& tools,
                           const ContextBudget& budget,
                           Model* model) {
    if (messages.size() < 4) {
        return false;
    }

    size_t current_tokens = estimate_context_tokens(messages, tools, model);
    size_t trigger_tokens = static_cast<size_t>(budget.context_window_tokens * budget.trigger_fraction);

    if (current_tokens <= trigger_tokens) {
        return false;
    }

    // Identify system message boundary
    size_t start_idx = 0;
    while (start_idx < messages.size() && messages[start_idx].role == "system") {
        start_idx++;
    }

    if (start_idx >= messages.size() - 2) {
        return false;
    }

    // Determine how many recent messages to keep
    size_t recent_tokens = 0;
    size_t split_idx = messages.size();
    for (size_t i = messages.size(); i > start_idx; --i) {
        size_t idx = i - 1;
        size_t msg_tok = estimate_text_tokens(messages[idx].content, model) + 4;
        if (recent_tokens + msg_tok > budget.recent_target_tokens && idx > start_idx + 1) {
            split_idx = idx;
            break;
        }
        recent_tokens += msg_tok;
        split_idx = idx;
    }

    if (split_idx <= start_idx + 1) {
        split_idx = start_idx + (messages.size() - start_idx) / 2;
    }

    // Summarize older messages from start_idx to split_idx
    std::ostringstream summary;
    summary << "[CONTEXT CHECKPOINT - EARLIER TURNS COMPACTED]\n"
            << "The following summary captures key actions, edits, and states from earlier turns:\n";

    size_t tool_count = 0;
    for (size_t i = start_idx; i < split_idx; ++i) {
        const auto& m = messages[i];
        if (m.role == "user") {
            summary << "- User Goal/Prompt: " << m.content.substr(0, 160) << (m.content.size() > 160 ? "..." : "") << "\n";
        } else if (m.role == "assistant") {
            if (!m.content.empty()) {
                summary << "- Assistant Plan/Decision: " << m.content.substr(0, 160) << (m.content.size() > 160 ? "..." : "") << "\n";
            }
            for (const auto& tc : m.tool_calls) {
                summary << "  * Tool Action: " << tc.name << "(" << tc.arguments.substr(0, 100) << ")\n";
                tool_count++;
            }
        } else if (m.role == "tool") {
            summary << "  * Tool Result (" << m.tool_name << "): " << m.content.substr(0, 120) << (m.content.size() > 120 ? "..." : "") << "\n";
        }
    }
    summary << "Total earlier actions compacted: " << tool_count << ". Proceeding with current state.\n";

    common_chat_msg checkpoint_msg;
    checkpoint_msg.role = "system";
    checkpoint_msg.content = summary.str();

    // Reconstruct messages: [System Messages] + [Checkpoint] + [Recent Messages]
    std::vector<common_chat_msg> new_messages;
    new_messages.reserve(start_idx + 1 + (messages.size() - split_idx));

    for (size_t i = 0; i < start_idx; ++i) {
        new_messages.push_back(messages[i]);
    }
    new_messages.push_back(checkpoint_msg);
    for (size_t i = split_idx; i < messages.size(); ++i) {
        new_messages.push_back(messages[i]);
    }

    messages = std::move(new_messages);
    return true;
}

} // namespace agent_cpp
