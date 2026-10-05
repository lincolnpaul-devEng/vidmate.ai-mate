#pragma once

#include "chat_types.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace agent_cpp {

/// @brief Represents an executed action record in the persistent ledger
struct RunLedgerEntry {
    std::string tool_call_id;
    std::string tool_name;
    std::string arguments;
    std::string result;
    std::string status; // "success", "error", "skipped"
    int64_t timestamp_ms = 0;
    size_t turn_index = 0;
};

/// @brief Persistent run ledger tracking all executed tool actions across long autonomous sessions
class RunLedger {
private:
    std::string run_id;
    std::vector<RunLedgerEntry> entries;
    int64_t created_at_ms = 0;
    std::string goal;

public:
    RunLedger(std::string id = "", std::string user_goal = "")
        : run_id(std::move(id)), goal(std::move(user_goal)) {
        created_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void record_tool_action(const std::string& tool_call_id,
                            const std::string& tool_name,
                            const std::string& arguments,
                            const std::string& result,
                            const std::string& status,
                            size_t turn_index) {
        RunLedgerEntry entry;
        entry.tool_call_id = tool_call_id;
        entry.tool_name = tool_name;
        entry.arguments = arguments;
        entry.result = result;
        entry.status = status;
        entry.turn_index = turn_index;
        entry.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        entries.push_back(entry);
    }

    [[nodiscard]] const std::string& get_run_id() const { return run_id; }
    [[nodiscard]] const std::string& get_goal() const { return goal; }
    [[nodiscard]] const std::vector<RunLedgerEntry>& get_entries() const { return entries; }
    [[nodiscard]] size_t size() const { return entries.size(); }
    [[nodiscard]] bool empty() const { return entries.empty(); }

    std::string to_json() const;
    static RunLedger from_json(const std::string& json_str);

    bool save_to_file(const std::string& file_path) const;
    static std::optional<RunLedger> load_from_file(const std::string& file_path);

    /// @brief Replays executed tool history back into chat messages for resuming a run
    void replay_into_messages(std::vector<common_chat_msg>& messages) const;
};

} // namespace agent_cpp
