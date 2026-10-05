#pragma once

#include <exception>
#include <stdexcept>
#include <string>
#include <variant>

namespace agent_cpp {

/// @brief Represents a failure that occurred during tool execution
/// Named "Failure" to distinguish from the ToolError exception class
struct ToolFailure
{
    std::string message;

    explicit ToolFailure(const std::string& msg)
      : message(msg)
    {
    }

    explicit ToolFailure(const std::exception& e)
      : message(e.what())
    {
    }
};

/// @brief Result of a tool execution - either a success string or an error
///
/// Usage in callbacks:
///   void after_tool_execution(std::string& tool_name, ToolResult& result) {
///       if (result.has_error()) {
///           // Recover from error by providing a message the agent can see
///           result.recover("{\"error\": \"" + result.error().message + "\"}");
///       } else {
///           // Handle success
///           std::cout << result.output() << std::endl;
///       }
///   }
class ToolResult
{
  private:
    std::variant<std::string, ToolFailure> value_;

  public:
    /// @brief Construct a successful result
    ToolResult(std::string output)
      : value_(std::move(output))
    {
    }

    /// @brief Construct from C-string
    ToolResult(const char* output)
      : value_(std::string(output))
    {
    }

    /// @brief Construct an error result
    ToolResult(ToolFailure err)
      : value_(std::move(err))
    {
    }

    /// @brief Construct an error from an exception
    static ToolResult from_exception(const std::exception& e)
    {
        return ToolResult(ToolFailure(e));
    }

    /// @brief Check if this result is an error
    [[nodiscard]] bool has_error() const
    {
        return std::holds_alternative<ToolFailure>(value_);
    }

    /// @brief Check if this result is successful
    [[nodiscard]] bool is_ok() const
    {
        return std::holds_alternative<std::string>(value_);
    }

    /// @brief Get the error (undefined behavior if is_ok())
    [[nodiscard]] const ToolFailure& error() const
    {
        return std::get<ToolFailure>(value_);
    }

    /// @brief Get the output string (undefined behavior if has_error())
    [[nodiscard]] const std::string& output() const
    {
        return std::get<std::string>(value_);
    }

    /// @brief Get the output string (undefined behavior if has_error())
    [[nodiscard]] std::string& output()
    {
        return std::get<std::string>(value_);
    }

    /// @brief Explicitly recover from an error by providing a success value
    /// This makes error recovery intent clear and explicit
    void recover(std::string recovery_message)
    {
        value_ = std::move(recovery_message);
    }
};

} // namespace agent_cpp
