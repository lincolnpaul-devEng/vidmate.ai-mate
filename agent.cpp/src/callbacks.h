#pragma once

#include "chat.h"
#include "error.h"
#include "tool_result.h"
#include <string>
#include <vector>

namespace agent_cpp {

// Interface for agent lifecycle callbacks
// Users can implement this interface to hook into various stages of the agent
// execution
class Callback
{
  public:
    virtual ~Callback() = default;

    // Called at the start of the agent loop, before any processing
    // @param messages: The current conversation messages (can be modified)
    virtual void before_agent_loop(std::vector<common_chat_msg>& messages) {}

    // Called at the end of the agent loop, after returning the final response
    // @param messages: The current conversation messages (can be modified)
    // @param response: The final response from the agent (can be modified)
    virtual void after_agent_loop(std::vector<common_chat_msg>& messages,
                                  std::string& response)
    {
    }

    // Called before making an LLM inference call
    // @param messages: The conversation messages that will be used to generate
    // the prompt (can be modified)
    virtual void before_llm_call(std::vector<common_chat_msg>& messages) {}

    // Called after receiving response from the LLM and parsing it
    // @param parsed_msg: The parsed message from the LLM (can be modified)
    virtual void after_llm_call(common_chat_msg& parsed_msg) {}

    // Called before executing a tool call
    // @param tool_name: Name of the tool to be executed (can be modified)
    // @param arguments: JSON string of the tool arguments (can be modified)
    // @throws ToolExecutionSkipped: Throw this exception to skip tool
    // execution.
    //         The exception message will be used as the tool result.
    virtual void before_tool_execution(std::string& tool_name,
                                       std::string& arguments)
    {
    }

    // Called after tool execution completes (success or error)
    // @param tool_name: Name of the tool that was executed (can be modified)
    // @param result: The result - either output string or ToolError
    //
    // To handle errors gracefully, check result.has_error() and use recover()
    // to convert to a string result and continue execution:
    //
    //   void after_tool_execution(std::string& name, ToolResult& result) {
    //       if (result.has_error()) {
    //           // Explicitly recover from error with a message the agent can
    //           see result.recover("{\"error\": \"" + result.error().message +
    //           "\"}");
    //       }
    //   }
    //
    // If result remains an error after all callbacks, the exception is
    // re-thrown.
    virtual void after_tool_execution(std::string& tool_name,
                                      ToolResult& result)
    {
        (void)tool_name;
        (void)result;
    }
};

} // namespace agent_cpp
