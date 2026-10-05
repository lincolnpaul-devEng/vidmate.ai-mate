#include "test_utils.h"
#include "tool.h"

using agent_cpp::json;

namespace {

class TestTool : public agent_cpp::Tool
{
  public:
    TestTool() = default;

    common_chat_tool get_definition() const override
    {
        common_chat_tool def;
        def.name = "test_tool";
        def.description = "A test tool";
        def.parameters = R"({"type": "object", "properties": {}})";
        return def;
    }

    std::string get_name() const override { return "test_tool"; }

    std::string execute(const json& args) override
    {
        json result;
        result["echo"] = args;
        return result.dump();
    }
};

}

TEST(test_tool_interface)
{
    auto tool = std::make_shared<TestTool>();

    ASSERT_EQ(tool->get_name(), "test_tool");

    auto def = tool->get_definition();
    ASSERT_EQ(def.name, "test_tool");
    ASSERT_EQ(def.description, "A test tool");

    json args;
    args["input"] = "hello";
    std::string result = tool->execute(args);
    json result_json = json::parse(result);
    ASSERT_EQ(result_json["echo"]["input"].get<std::string>(), "hello");
}

TEST(test_tool_polymorphism)
{
    std::vector<std::shared_ptr<agent_cpp::Tool>> tools;
    tools.push_back(std::make_shared<TestTool>());

    ASSERT_EQ(tools.size(), 1);
    ASSERT_EQ(tools[0]->get_name(), "test_tool");
}

int
main()
{
    std::cout << "\n=== Running Tool Unit Tests ===\n" << std::endl;

    try {
        RUN_TEST(test_tool_interface);
        RUN_TEST(test_tool_polymorphism);

        std::cout << "\n=== All tests passed! ✓ ===\n" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
