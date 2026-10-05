#include "agent.h"
#include "error.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace agent_cpp {

using json = nlohmann::json;

Agent::Agent(std::shared_ptr<Model> model,
             std::vector<std::unique_ptr<Tool>> tools,
             std::vector<std::unique_ptr<Callback>> callbacks,
             const std::string& instructions,
             AgentConfig config)
  : callbacks(std::move(callbacks))
  , instructions(instructions)
  , model(std::move(model))
  , tools(std::move(tools))
  , config(config)
  , ledger("agent_run", instructions)
{
}

void
Agent::ensure_system_message(std::vector<common_chat_msg>& messages)
{
    if (!instructions.empty()) {
        bool has_instructions = !messages.empty() &&
                                messages[0].role == "system" &&
                                messages[0].content == instructions;

        if (!has_instructions) {
            common_chat_msg system_msg;
            system_msg.role = "system";
            system_msg.content = instructions;
            messages.insert(messages.begin(), system_msg);
        }
    }
}

std::vector<common_chat_tool>
Agent::get_tool_definitions() const
{
    std::vector<common_chat_tool> tool_definitions;
    tool_definitions.reserve(tools.size());
    for (const auto& tool : tools) {
        tool_definitions.push_back(tool->get_definition());
    }
    return tool_definitions;
}

std::string
Agent::run_loop(std::vector<common_chat_msg>& messages,
                const ResponseCallback& callback)
{
    ensure_system_message(messages);

    for (const auto& cb : callbacks) {
        cb->before_agent_loop(messages);
    }

    std::vector<common_chat_tool> tool_definitions = get_tool_definitions();
    size_t turn_count = 0;

    while (turn_count < config.max_turns) {
        turn_count++;

        // Item B: Dynamic Context Compaction before inference if token pressure is high
        if (config.enable_context_compaction) {
            maybe_compact_context(messages, tool_definitions, config.budget, model.get());
        }

        for (const auto& cb : callbacks) {
            cb->before_llm_call(messages);
        }

        // If we are on the final turn, we can instruct the model to conclude if tools are returned
        auto parsed_msg = model->generate(messages, tool_definitions, callback);

        for (const auto& cb : callbacks) {
            cb->after_llm_call(parsed_msg);
        }

        messages.push_back(parsed_msg);

        // Model returned final text without tool calls
        if (parsed_msg.tool_calls.empty()) {
            std::string response = parsed_msg.content;
            for (const auto& cb : callbacks) {
                cb->after_agent_loop(messages, response);
            }
            return response;
        }

        // Execute returned tool calls
        for (const auto& tool_call : parsed_msg.tool_calls) {
            std::string tool_name = tool_call.name;
            std::string tool_arguments = tool_call.arguments;

            ToolResult result("");
            bool tool_skipped = false;
            std::string status = "success";

            try {
                for (const auto& cb : callbacks) {
                    cb->before_tool_execution(tool_name, tool_arguments);
                }
            } catch (const ToolExecutionSkipped& e) {
                json response;
                response["skipped"] = e.get_message();
                result = response.dump();
                tool_skipped = true;
                status = "skipped";
            }

            if (!tool_skipped) {
                try {
                    json args;
                    try {
                        args = json::parse(tool_arguments);
                    } catch (const json::parse_error& e) {
                        throw ToolArgumentError(tool_name, e.what());
                    }

                    auto tool_it = std::find_if(
                      tools.begin(),
                      tools.end(),
                      [&tool_name](const std::unique_ptr<Tool>& t) {
                          return t->get_name() == tool_name;
                      });

                    if (tool_it == tools.end()) {
                        throw ToolNotFoundError(tool_name);
                    }

                    result = (*tool_it)->execute(args);
                } catch (const std::exception& e) {
                    result = ToolResult::from_exception(e);
                    status = "error";
                }
            }

            // Callbacks can inspect or recover from errors
            for (const auto& cb : callbacks) {
                cb->after_tool_execution(tool_name, result);
            }

            // If still an error after callbacks, re-throw
            if (result.has_error()) {
                if (config.enable_run_ledger) {
                    ledger.record_tool_action(tool_call.id, tool_name, tool_arguments, result.error().message, "error", turn_count);
                }
                throw ToolError(tool_name, result.error().message);
            }

            // Item C: Two-pass tool result compaction to keep model context bounded
            std::string raw_output = result.output();
            std::string final_tool_output = config.enable_tool_compaction
                ? compact_tool_result(raw_output, config.budget.max_tool_result_chars)
                : raw_output;

            // Item D: Persistent Run Ledger recording
            if (config.enable_run_ledger) {
                ledger.record_tool_action(tool_call.id, tool_name, tool_arguments, final_tool_output, status, turn_count);
            }

            common_chat_msg tool_msg;
            tool_msg.role = "tool";
            tool_msg.content = final_tool_output;
            tool_msg.tool_call_id = tool_call.id;
            tool_msg.tool_name = tool_name;
            messages.push_back(tool_msg);
        }
    }

    // If max_turns reached, return last message content or concluding summary
    std::string fallback_response = messages.empty() ? "" : messages.back().content;
    for (const auto& cb : callbacks) {
        cb->after_agent_loop(messages, fallback_response);
    }
    return fallback_response;
}

std::vector<llama_token>
Agent::build_prompt_tokens()
{
    if (!model) {
        return {};
    }

    std::vector<common_chat_msg> system_messages;
    if (!instructions.empty()) {
        common_chat_msg system_msg;
        system_msg.role = "system";
        system_msg.content = instructions;
        system_messages.push_back(system_msg);
    }

    std::vector<common_chat_tool> tool_definitions = get_tool_definitions();

    common_chat_templates_inputs inputs;
    inputs.messages = system_messages;
    inputs.tools = tool_definitions;
    inputs.tool_choice = COMMON_CHAT_TOOL_CHOICE_AUTO;
    inputs.add_generation_prompt = false;
    inputs.enable_thinking = false;

    auto params = common_chat_templates_apply(model->get_templates(), inputs);

    return model->tokenize(params.prompt);
}

bool
Agent::load_or_create_cache(const std::string& cache_path)
{
    if (!model) {
        return false;
    }

    if (std::filesystem::exists(cache_path)) {
        auto cached_tokens = model->load_cache(cache_path);
        if (!cached_tokens.empty()) {
            printf("Loaded prompt cache from '%s' (%zu tokens)\n",
                   cache_path.c_str(),
                   cached_tokens.size());
            return true;
        }
    }

    auto prompt_tokens = build_prompt_tokens();
    if (prompt_tokens.empty()) {
        return true;
    }

    printf("Creating prompt cache at '%s' (%zu tokens)\n",
           cache_path.c_str(),
           prompt_tokens.size());

    // warms the KV cache
    model->generate_from_tokens(prompt_tokens);

    return model->save_cache(cache_path);
}

} // namespace agent_cpp
