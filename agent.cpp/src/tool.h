#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "chat.h"

namespace agent_cpp {

using json = nlohmann::json;

// Tool class that encapsulates a tool definition and its execution logic
class Tool
{
  public:
    virtual ~Tool() = default;

    // Get the tool definition (name, description, parameters schema)
    virtual common_chat_tool get_definition() const = 0;

    // Execute the tool with given arguments
    virtual std::string execute(const json& arguments) = 0;

    // Get the tool's name
    virtual std::string get_name() const = 0;
};

} // namespace agent_cpp
