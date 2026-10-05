#include "callbacks.h"
#include "chat.h"
#include "test_utils.h"
#include "tool.h"
#include <memory>
#include <string>
#include <vector>

class InputModifyingCallback : public agent_cpp::Callback
{
  public:
    bool add_custom_message = false;

    std::string custom_system_message =
      "You are a helpful assistant modified by callback.";

    void before_llm_call(std::vector<common_chat_msg>& messages) override
    {
        if (add_custom_message && !messages.empty()) {
            common_chat_msg system_msg;
            system_msg.role = "system";
            system_msg.content = custom_system_message;
            messages.insert(messages.begin(), system_msg);
        }
    }
};

class OutputModifyingCallback : public agent_cpp::Callback
{
  public:
    bool should_append_to_content = false;
    std::string append_text = " [MODIFIED]";

    bool should_modify_role = false;
    std::string new_role = "assistant-modified";

    void after_llm_call(common_chat_msg& parsed_msg) override
    {
        if (should_append_to_content) {
            parsed_msg.content += append_text;
        }

        if (should_modify_role) {
            parsed_msg.role = new_role;
        }
    }
};

class AgentLoopCallback : public agent_cpp::Callback
{
  public:
    bool should_add_system_msg = false;
    bool should_modify_response = false;

    void before_agent_loop(std::vector<common_chat_msg>& messages) override
    {
        if (should_add_system_msg) {
            common_chat_msg sys_msg;
            sys_msg.role = "system";
            sys_msg.content = "Initial system message";
            messages.insert(messages.begin(), sys_msg);
        }
    }

    void after_agent_loop(std::vector<common_chat_msg>& /*messages*/,
                          std::string& response) override
    {
        if (should_modify_response) {
            response = "Final: " + response;
        }
    }
};

class ToolExecutionCallback : public agent_cpp::Callback
{
  public:
    bool should_modify_tool_name = false;
    bool should_modify_arguments = false;
    bool should_wrap_result = false;
    bool should_skip_execution = false;
    std::string skip_message = "Skipped by callback";

    void before_tool_execution(std::string& tool_name,
                               std::string& arguments) override
    {
        if (should_skip_execution) {
            throw agent_cpp::ToolExecutionSkipped(skip_message);
        }
        if (should_modify_tool_name) {
            tool_name = "modified_" + tool_name;
        }
        if (should_modify_arguments) {
            arguments = "{\"modified\": true}";
        }
    }

    void after_tool_execution(std::string& /*tool_name*/,
                              agent_cpp::ToolResult& result) override
    {
        if (should_wrap_result && result.is_ok()) {
            result.recover("[RESULT: " + result.output() + "]");
        }
    }
};

TEST(test_input_modifying_callbacks)
{
    auto callback = std::make_shared<InputModifyingCallback>();
    callback->add_custom_message = true;

    std::vector<common_chat_msg> messages;
    common_chat_msg user_msg;
    user_msg.role = "user";
    user_msg.content = "Hello";
    messages.push_back(user_msg);

    ASSERT_EQ(messages.size(), 1);

    callback->before_llm_call(messages);

    ASSERT_EQ(messages.size(), 2);
    ASSERT_STREQ(messages[0].role.c_str(), "system");
}

TEST(test_input_modifying_callbacks_custom_message)
{
    auto callback = std::make_shared<InputModifyingCallback>();
    callback->add_custom_message = true;

    std::vector<common_chat_msg> messages;

    common_chat_msg user_msg;
    user_msg.role = "user";
    user_msg.content = "Hello";
    messages.push_back(user_msg);

    ASSERT_EQ(messages.size(), 1);
    ASSERT_STREQ(messages[0].role.c_str(), "user");

    callback->before_llm_call(messages);

    ASSERT_EQ(messages.size(), 2);
    ASSERT_STREQ(messages[0].role.c_str(), "system");
    ASSERT_STREQ(messages[0].content.c_str(),
                 "You are a helpful assistant modified by callback.");
    ASSERT_STREQ(messages[1].role.c_str(), "user");
}

TEST(test_output_modifying_callbacks_content)
{
    auto callback = std::make_shared<OutputModifyingCallback>();
    callback->should_append_to_content = true;

    common_chat_msg msg;
    msg.role = "assistant";
    msg.content = "Original response";

    callback->after_llm_call(msg);

    ASSERT_STREQ(msg.content.c_str(), "Original response [MODIFIED]");
}

TEST(test_output_modifying_callbacks_role)
{
    auto callback = std::make_shared<OutputModifyingCallback>();
    callback->should_modify_role = true;

    common_chat_msg msg;
    msg.role = "assistant";
    msg.content = "Response";

    callback->after_llm_call(msg);

    ASSERT_STREQ(msg.role.c_str(), "assistant-modified");
}

TEST(test_multiple_input_modifications)
{
    auto callback = std::make_shared<InputModifyingCallback>();
    callback->add_custom_message = true;

    std::vector<common_chat_msg> messages;

    common_chat_msg user_msg;
    user_msg.role = "user";
    user_msg.content = "Test";
    messages.push_back(user_msg);

    callback->before_llm_call(messages);

    ASSERT_EQ(messages.size(), 2);
    ASSERT_STREQ(messages[0].role.c_str(), "system");
}

TEST(test_callbacks_with_empty_messages)
{
    auto callback = std::make_shared<InputModifyingCallback>();
    callback->add_custom_message = true;

    std::vector<common_chat_msg> messages;
    ASSERT_EQ(messages.size(), 0);

    // Should not add custom message when messages is empty
    callback->before_llm_call(messages);

    ASSERT_EQ(messages.size(), 0);
}

TEST(test_callback_chaining)
{
    auto callback = std::make_shared<InputModifyingCallback>();
    callback->add_custom_message = true;

    std::vector<common_chat_msg> messages1;
    common_chat_msg user_msg1;
    user_msg1.role = "user";
    user_msg1.content = "Test1";
    messages1.push_back(user_msg1);

    callback->before_llm_call(messages1);
    ASSERT_EQ(messages1.size(), 2);
    ASSERT_STREQ(messages1[0].role.c_str(), "system");

    std::vector<common_chat_msg> messages2;
    common_chat_msg user_msg2;
    user_msg2.role = "user";
    user_msg2.content = "Test2";
    messages2.push_back(user_msg2);

    callback->before_llm_call(messages2);
    ASSERT_EQ(messages2.size(), 2);
    ASSERT_STREQ(messages2[0].role.c_str(), "system");
}

TEST(test_output_callback_preserves_fields)
{
    auto callback = std::make_shared<OutputModifyingCallback>();
    callback->should_append_to_content = true;

    common_chat_msg msg;
    msg.role = "assistant";
    msg.content = "Test";
    msg.reasoning_content = "Some reasoning";

    common_chat_tool_call tool_call;
    tool_call.name = "test_tool";
    tool_call.arguments = "{}";
    tool_call.id = "call_123";
    msg.tool_calls.push_back(tool_call);

    callback->after_llm_call(msg);

    ASSERT_STREQ(msg.content.c_str(), "Test [MODIFIED]");

    ASSERT_STREQ(msg.role.c_str(), "assistant");
    ASSERT_STREQ(msg.reasoning_content.c_str(), "Some reasoning");
    ASSERT_EQ(msg.tool_calls.size(), 1);
    ASSERT_STREQ(msg.tool_calls[0].name.c_str(), "test_tool");
}

TEST(test_before_agent_loop_adds_message)
{
    auto callback = std::make_shared<AgentLoopCallback>();
    callback->should_add_system_msg = true;

    std::vector<common_chat_msg> messages;
    common_chat_msg user_msg;
    user_msg.role = "user";
    user_msg.content = "Hello";
    messages.push_back(user_msg);

    callback->before_agent_loop(messages);

    ASSERT_EQ(messages.size(), 2);
    ASSERT_STREQ(messages[0].role.c_str(), "system");
    ASSERT_STREQ(messages[0].content.c_str(), "Initial system message");
}

TEST(test_after_agent_loop_modifies_response)
{
    auto callback = std::make_shared<AgentLoopCallback>();
    callback->should_modify_response = true;

    std::vector<common_chat_msg> messages;
    std::string response = "Hello, world!";

    callback->after_agent_loop(messages, response);

    ASSERT_STREQ(response.c_str(), "Final: Hello, world!");
}

TEST(test_before_tool_execution_modifies_params)
{
    auto callback = std::make_shared<ToolExecutionCallback>();
    callback->should_modify_tool_name = true;
    callback->should_modify_arguments = true;

    std::string tool_name = "calculator";
    std::string arguments = "{\"x\": 5}";

    callback->before_tool_execution(tool_name, arguments);

    ASSERT_STREQ(tool_name.c_str(), "modified_calculator");
    ASSERT_STREQ(arguments.c_str(), "{\"modified\": true}");
}

TEST(test_after_tool_execution_wraps_result)
{
    auto callback = std::make_shared<ToolExecutionCallback>();
    callback->should_wrap_result = true;

    std::string tool_name = "calculator";
    agent_cpp::ToolResult result("42");

    callback->after_tool_execution(tool_name, result);

    ASSERT_TRUE(result.is_ok());
    ASSERT_STREQ(result.output().c_str(), "[RESULT: 42]");
}

TEST(test_tool_execution_skipped_exception)
{
    auto callback = std::make_shared<ToolExecutionCallback>();
    callback->should_skip_execution = true;
    callback->skip_message = "User cancelled the operation";

    std::string tool_name = "dangerous_tool";
    std::string arguments = "{\"delete\": true}";

    bool exception_caught = false;
    std::string caught_message;

    try {
        callback->before_tool_execution(tool_name, arguments);
    } catch (const agent_cpp::ToolExecutionSkipped& e) {
        exception_caught = true;
        caught_message = e.get_message();
    }

    ASSERT_TRUE(exception_caught);
    ASSERT_STREQ(caught_message.c_str(), "User cancelled the operation");
}

TEST(test_tool_execution_skipped_default_message)
{
    agent_cpp::ToolExecutionSkipped ex;
    ASSERT_STREQ(ex.what(), "Tool execution skipped");
    ASSERT_STREQ(ex.get_message().c_str(), "Tool execution skipped");
}

TEST(test_tool_execution_skipped_custom_message)
{
    agent_cpp::ToolExecutionSkipped ex("Custom skip reason");
    ASSERT_STREQ(ex.what(), "Custom skip reason");
    ASSERT_STREQ(ex.get_message().c_str(), "Custom skip reason");
}

int
main()
{
    std::cout << "\n=== Running Callbacks Unit Tests ===\n" << std::endl;

    try {
        RUN_TEST(test_input_modifying_callbacks);
        RUN_TEST(test_input_modifying_callbacks_custom_message);
        RUN_TEST(test_output_modifying_callbacks_content);
        RUN_TEST(test_output_modifying_callbacks_role);
        RUN_TEST(test_multiple_input_modifications);
        RUN_TEST(test_callbacks_with_empty_messages);
        RUN_TEST(test_callback_chaining);
        RUN_TEST(test_output_callback_preserves_fields);
        RUN_TEST(test_before_agent_loop_adds_message);
        RUN_TEST(test_after_agent_loop_modifies_response);
        RUN_TEST(test_before_tool_execution_modifies_params);
        RUN_TEST(test_after_tool_execution_wraps_result);
        RUN_TEST(test_tool_execution_skipped_exception);
        RUN_TEST(test_tool_execution_skipped_default_message);
        RUN_TEST(test_tool_execution_skipped_custom_message);

        std::cout << "\n=== All tests passed! ✓ ===\n" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
