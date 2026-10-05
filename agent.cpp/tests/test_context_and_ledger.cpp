#include "agent.h"
#include "context_compaction.h"
#include "run_ledger.h"
#include "test_utils.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace agent_cpp;

void test_tool_result_compaction() {
    std::cout << "[Testing Part C: Tool Result Compaction]..." << std::endl;

    // Small JSON should remain intact
    std::string small_json = "{\"status\": \"ok\", \"frame\": 120}";
    std::string res_small = compact_tool_result(small_json, 1000);
    assert(res_small == small_json);

    // Large JSON array (> 40 items) should be pruned
    json large_arr = json::array();
    for (int i = 0; i < 100; ++i) {
        large_arr.push_back({{"index", i}, {"val", "some relatively long text value padding data"}});
    }
    json large_doc = {{"items", large_arr}};
    std::string raw_large = large_doc.dump();
    assert(raw_large.size() > 1000);

    std::string compacted = compact_tool_result(raw_large, 800);
    assert(compacted.size() <= 800);
    assert(compacted.find("_truncated_for_context") != std::string::npos ||
           compacted.find("truncated") != std::string::npos);

    // Non-JSON oversized text string
    std::string large_text(3000, 'X');
    std::string compacted_text = compact_tool_result(large_text, 500);
    assert(compacted_text.size() <= 600);
    assert(compacted_text.find("characters omitted") != std::string::npos);

    std::cout << "  ✓ Tool result compaction passed!" << std::endl;
}

void test_dynamic_context_compaction() {
    std::cout << "[Testing Part B: Dynamic Context Compaction]..." << std::endl;

    std::vector<common_chat_msg> messages;
    common_chat_msg sys;
    sys.role = "system";
    sys.content = "You are an autonomous AI video editor.";
    messages.push_back(sys);

    // Add many simulated turns
    for (int i = 0; i < 15; ++i) {
        common_chat_msg u;
        u.role = "user";
        u.content = "User turn " + std::to_string(i) + ": Add B-roll shader animation at frame " + std::to_string(i * 30);
        messages.push_back(u);

        common_chat_msg a;
        a.role = "assistant";
        a.content = "Generating procedural shader and inserting onto Track V2.";
        messages.push_back(a);

        common_chat_msg t;
        t.role = "tool";
        t.tool_name = "generate_glsl_shader";
        t.tool_call_id = "call_" + std::to_string(i);
        t.content = "{\"status\": \"inserted\", \"track\": 2, \"frame\": " + std::to_string(i * 30) + "}";
        messages.push_back(t);
    }

    size_t initial_count = messages.size();
    assert(initial_count > 30);

    // Set a tiny budget to force compaction
    ContextBudget budget;
    budget.context_window_tokens = 500;
    budget.trigger_fraction = 0.50f; // trigger at 250 tokens
    budget.recent_target_tokens = 100;

    bool compacted = maybe_compact_context(messages, {}, budget, nullptr);
    assert(compacted == true);
    assert(messages.size() < initial_count);
    assert(messages[0].role == "system");
    assert(messages[1].role == "system");
    assert(messages[1].content.find("[CONTEXT CHECKPOINT") != std::string::npos);

    std::cout << "  ✓ Context compacted successfully: " << initial_count << " msgs -> "
              << messages.size() << " msgs with structured checkpoint." << std::endl;
}

void test_run_ledger_and_recovery() {
    std::cout << "[Testing Part D: Run Ledger & State Recovery]..." << std::endl;

    RunLedger ledger("run_101", "Create an audio-reactive particle visualization");

    ledger.record_tool_action("call_1", "get_audio_spectrum", "{\"bin_id\": \"2\"}", "{\"levels\": [0.8, 0.9]}", "success", 1);
    ledger.record_tool_action("call_2", "generate_glsl_shader", "{\"name\": \"plasma\"}", "{\"asset\": \"/tmp/plasma.mp4\"}", "success", 2);

    assert(ledger.size() == 2);
    assert(ledger.get_run_id() == "run_101");

    // Test JSON Serialization and Deserialization
    std::string json_str = ledger.to_json();
    RunLedger hydrated = RunLedger::from_json(json_str);

    assert(hydrated.size() == 2);
    assert(hydrated.get_run_id() == "run_101");
    assert(hydrated.get_entries()[0].tool_name == "get_audio_spectrum");
    assert(hydrated.get_entries()[1].tool_name == "generate_glsl_shader");

    // Test replay into messages
    std::vector<common_chat_msg> msgs;
    hydrated.replay_into_messages(msgs);
    assert(msgs.size() == 2);
    assert(msgs[0].role == "tool");
    assert(msgs[0].tool_name == "get_audio_spectrum");
    assert(msgs[1].role == "tool");
    assert(msgs[1].tool_name == "generate_glsl_shader");

    std::cout << "  ✓ Run ledger serialization & state recovery passed!" << std::endl;
}

void test_agent_config_and_turn_bounds() {
    std::cout << "[Testing Part A: Agent Config & Turn Bounds]..." << std::endl;

    AgentConfig config;
    config.max_turns = 25;
    config.budget.context_window_tokens = 64000;
    config.enable_context_compaction = true;
    config.enable_tool_compaction = true;
    config.enable_run_ledger = true;

    Agent agent(nullptr, {}, {}, "You are a test assistant", config);
    assert(agent.get_config().max_turns == 25);
    assert(agent.get_config().budget.context_window_tokens == 64000);
    assert(agent.get_config().enable_context_compaction == true);

    std::cout << "  ✓ Agent configuration & turn bounding verified!" << std::endl;
}

int main() {
    std::cout << "=== Running agent.cpp Context & Ledger Tests (Parts A-D) ===" << std::endl;
    test_agent_config_and_turn_bounds();
    test_dynamic_context_compaction();
    test_tool_result_compaction();
    test_run_ledger_and_recovery();
    std::cout << "=== All agent.cpp tests passed successfully! ===" << std::endl;
    return 0;
}
