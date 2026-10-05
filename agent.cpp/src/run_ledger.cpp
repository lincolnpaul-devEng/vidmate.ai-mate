#include "run_ledger.h"
#include <fstream>
#include <sstream>

namespace agent_cpp {

bool RunLedger::save_to_file(const std::string& file_path) const {
    try {
        std::ofstream out(file_path);
        if (!out.is_open()) return false;
        out << to_json();
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<RunLedger> RunLedger::load_from_file(const std::string& file_path) {
    try {
        std::ifstream in(file_path);
        if (!in.is_open()) return std::nullopt;
        std::stringstream buffer;
        buffer << in.rdbuf();
        return from_json(buffer.str());
    } catch (...) {
        return std::nullopt;
    }
}

void RunLedger::replay_into_messages(std::vector<common_chat_msg>& messages) const {
    for (const auto& entry : entries) {
        common_chat_msg tool_msg;
        tool_msg.role = "tool";
        tool_msg.content = entry.result;
        tool_msg.tool_call_id = entry.tool_call_id;
        tool_msg.tool_name = entry.tool_name;
        messages.push_back(tool_msg);
    }
}

} // namespace agent_cpp
