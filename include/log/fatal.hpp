#ifndef LOOMWOOD_FATAL_HPP
#define LOOMWOOD_FATAL_HPP

#include <string>
#include <string_view>
#include <source_location>
#include <stacktrace>
#include <format>

namespace lw
{
    /**
     * @brief 获取当前调用栈的字符串表示
     * @return 格式化的调用栈信息
     */
    [[nodiscard]] std::string get_callstack_string();

    /**
     * @brief 触发致命错误，显示 SDL 消息框并终止程序
     *
     * 此函数会：
     * 1. 收集错误信息和调用栈
     * 2. 显示 SDL 消息框，允许用户复制错误信息
     * 3. 终止程序
     *
     * @param message 错误消息
     * @param location 错误发生位置（通常由 PANIC 宏自动提供）
     */
    [[noreturn]] void fatal(std::string_view message,
                            std::source_location location = std::source_location::current());

    /**
     * @brief 触发致命错误（带自定义标题）
     *
     * @param title 消息框标题
     * @param message 错误消息
     * @param location 错误发生位置
     */
    [[noreturn]] void fatal_with_title(std::string_view title, std::string_view message,
                                       std::source_location location = std::source_location::current());
} // namespace lw

#endif // LOOMWOOD_FATAL_HPP
