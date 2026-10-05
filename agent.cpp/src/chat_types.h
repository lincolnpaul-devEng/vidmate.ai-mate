#pragma once

#include <string>
#include <vector>

namespace agent_cpp {

#ifndef COMMON_CHAT_TYPES_DEFINED
#define COMMON_CHAT_TYPES_DEFINED

struct common_chat_tool_call {
    std::string id;
    std::string name;
    std::string arguments;
};

struct common_chat_msg {
    std::string role;
    std::string content;
    std::string tool_name;
    std::string tool_call_id;
    std::vector<common_chat_tool_call> tool_calls;
};

struct common_chat_tool {
    std::string name;
    std::string description;
    std::string parameters;
};

#endif

} // namespace agent_cpp
