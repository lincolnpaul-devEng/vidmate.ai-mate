#pragma once

#include "chat.h"
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace agent_cpp {

using json = nlohmann::json;

/// @brief Represents an executed action record in the persistent ledger
struct RunLedgerEntry {
    std::string tool_call_id;
    std::string tool_name;
    std::string arguments;
    std::string result;
    std::string status; // "success", "error", "skipped"
    int64_t timestamp_ms = 0;
    size_t turn_index = 0;

    json to_json() const {
        return json{
            {"tool_call_id", tool_call_id},
            {"tool_name", tool_name},
            {"arguments", arguments},
            {"result", result},
            {"status", status},
            {"timestamp_ms", timestamp_ms},
            {"turn_index", turn_index}
        };
    }

    static RunLedgerEntry from_json(const json& j) {
        RunLedgerEntry entry;
        if (j.contains("tool_call_id")) entry.tool_call_id = j["tool_call_id"].get<std::string>();
        if (j.contains("tool_name")) entry.tool_name = j["tool_name"].get<std::string>();
        if (j.contains("arguments")) entry.arguments = j["arguments"].get<std::string>();
        if (j.contains("result")) entry.result = j["result"].get<std::string>();
        if (j.contains("status")) entry.status = j["status"].get<std::string>();
        if (j.contains("timestamp_ms")) entry.timestamp_ms = j["timestamp_ms"].get<int64_t>();
        if (j.contains("turn_index")) entry.turn_index = j["turn_index"].get<size_t>();
        return entry;
    }
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

    std::string to_json() const {
        json j;
        j["run_id"] = run_id;
        j["goal"] = goal;
        j["created_at_ms"] = created_at_ms;
        json arr = json::array();
        for (const auto& entry : entries) {
            arr.push_back(entry.to_json());
        }
        j["entries"] = arr;
        return j.dump(2);
    }

    static RunLedger from_json(const std::string& json_str) {
        RunLedger ledger;
        try {
            json j = json::parse(json_str);
            if (j.contains("run_id")) ledger.run_id = j["run_id"].get<std::string>();
            if (j.contains("goal")) ledger.goal = j["goal"].get<std::string>();
            if (j.contains("created_at_ms")) ledger.created_at_ms = j["created_at_ms"].get<int64_t>();
            if (j.contains("entries") && j["entries"].is_array()) {
                for (const auto& item : j["entries"]) {
                    ledger.entries.push_back(RunLedgerEntry::from_json(item));
                }
            }
        } catch (...) {
        }
        return ledger;
    }

    bool save_to_file(const std::string& file_path) const;
    static std::optional<RunLedger> load_from_file(const std::string& file_path);

    /// @brief Replays executed tool history back into chat messages for resuming a run
    void replay_into_messages(std::vector<common_chat_msg>& messages) const;
};

} // namespace agent_cpp
