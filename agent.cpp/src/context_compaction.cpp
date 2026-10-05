#include "context_compaction.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace agent_cpp {

static const size_t FIRST_PASS_ARRAY_ITEMS = 40;
static const size_t FIRST_PASS_STRING_CHARS = 4000;
static const size_t FINAL_PASS_ARRAY_ITEMS = 12;
static const size_t FINAL_PASS_STRING_CHARS = 1000;
static const size_t MAX_OBJECT_FIELDS = 60;

size_t estimate_text_tokens(const std::string& text, Model* model) {
    if (text.empty()) return 0;
    if (model) {
        try {
            auto tokens = model->tokenize(text);
            if (!tokens.empty()) return tokens.size();
        } catch (...) {
            // fallback to heuristic
        }
    }
    // Heuristic: ~4 chars per token for ASCII, 1 char per token for non-ASCII
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

static json compact_json_value(const json& val, size_t max_array_items, size_t max_str_chars) {
    if (val.is_string()) {
        std::string s = val.get<std::string>();
        if (s.size() > max_str_chars) {
            return s.substr(0, max_str_chars) + "\n...[" + std::to_string(s.size() - max_str_chars) + " chars omitted]";
        }
        return val;
    }
    if (val.is_array()) {
        json arr = json::array();
        size_t count = 0;
        for (const auto& item : val) {
            if (count++ >= max_array_items) {
                arr.push_back({{"_omitted_items", val.size() - max_array_items}});
                break;
            }
            arr.push_back(compact_json_value(item, max_array_items, max_str_chars));
        }
        return arr;
    }
    if (val.is_object()) {
        json obj = json::object();
        size_t count = 0;
        for (auto it = val.begin(); it != val.end(); ++it) {
            if (count++ >= MAX_OBJECT_FIELDS) {
                obj["_omitted_fields"] = val.size() - MAX_OBJECT_FIELDS;
                break;
            }
            obj[it.key()] = compact_json_value(it.value(), max_array_items, max_str_chars);
        }
        return obj;
    }
    return val;
}

std::string compact_tool_result(const std::string& raw_output, size_t max_chars) {
    if (raw_output.size() <= max_chars) {
        return raw_output;
    }

    // Try JSON compaction
    try {
        json parsed = json::parse(raw_output);
        
        // Pass 1: moderate pruning
        json pass1 = compact_json_value(parsed, FIRST_PASS_ARRAY_ITEMS, FIRST_PASS_STRING_CHARS);
        pass1["_truncated_for_context"] = true;
        std::string res1 = pass1.dump();
        if (res1.size() <= max_chars) {
            return res1;
        }

        // Pass 2: aggressive pruning
        json pass2 = compact_json_value(parsed, FINAL_PASS_ARRAY_ITEMS, FINAL_PASS_STRING_CHARS);
        pass2["_truncated_for_context"] = true;
        std::string res2 = pass2.dump();
        if (res2.size() <= max_chars) {
            return res2;
        }
    } catch (...) {
        // Fallback to text slicing
    }

    // Fallback: hard string truncation with notice
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
    // Need at least 4 messages to compact (system, user, assistant, tool...)
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
        // Not enough middle messages to compact effectively
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
