#include <log/fatal.hpp>
#include <log/log.hpp>
#include <SDL3/SDL.h>
#include <format>
#include <sstream>
#include <cstdlib>

namespace lw
{
  std::string get_callstack_string()
  {
    std::ostringstream oss;
    oss << "=== Call Stack ===\n\n";

    auto trace = std::stacktrace::current();
    oss << std::format("depth：{}\n\n", trace.size());

    for (size_t i = 0; i < trace.size(); ++i)
    {
      const auto& entry = trace[i];
      oss << std::format("[{}] ", i);

      oss << std::format("{}:{}  {}", entry.source_file(), entry.source_line(), entry.description());
      oss << "\n";
    }

    return oss.str();
  }

  namespace detail
  {
    /**
     * @brief 将错误信息复制到系统剪贴板
     */
    bool copy_to_clipboard(const std::string& text)
    {
      if (!SDL_SetClipboardText(text.c_str()))
      {
        return true;
      }
      return false;
    }
  } // namespace detail

  void fatal(std::string_view message, std::source_location location)
  {
    fatal_with_title("织源工坊 - 致命错误", message, location);
  }

  void fatal_with_title(std::string_view title, std::string_view message,
                        std::source_location location)
  {
    // 构建完整的错误信息
    std::ostringstream error_stream;

    error_stream << "=== Loomwood Fatal Error ===\n\n";
    error_stream << std::format("Information：{}\n\n", message);
    error_stream << std::format("location：{}:{} in func {}\n\n", location.file_name(),
                                location.line(), location.function_name());
    error_stream << get_callstack_string();
    error_stream << "\n=== System Info ===\n";
    error_stream << std::format("Time：{}\n", __DATE__ " " __TIME__);

    std::string error_message = error_stream.str();

    // 使用 default logger 输出错误信息
    auto logger = get_default_logger();
    if (logger)
    {
      logger->critical("=== Loomwood Fatal Error ===");
      logger->critical("Information: {}", message);
      logger->critical("Location: {}:{} in func {}", location.file_name(),
                       location.line(), location.function_name());
      logger->critical("\n{}", get_callstack_string());
    }

    // 尝试复制到剪贴板（静默失败）
    detail::copy_to_clipboard(error_message);

    // SDL 消息框按钮
    constexpr SDL_MessageBoxButtonData buttons[] = {
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Copy and Abort"},
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "Abort"},
    };

    // SDL 消息框颜色方案（红色表示错误）
    const SDL_MessageBoxColorScheme colorScheme = {
      {
        {0xFF, 0x40, 0x40}, // SDL_MESSAGEBOX_COLOR_BACKGROUND
        {0xFF, 0xFF, 0xFF}, // SDL_MESSAGEBOX_COLOR_TEXT
        {0x80, 0x20, 0x20}, // SDL_MESSAGEBOX_COLOR_BUTTON_BORDER
        {0x60, 0x30, 0x30}, // SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND
        {0xFF, 0x60, 0x60} // SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED
      }
    };

    // SDL 消息框数据
    const SDL_MessageBoxData messageboxdata = {
      SDL_MESSAGEBOX_ERROR,
      nullptr, // 无父窗口
      title.data(),
      error_message.c_str(),
      std::size(buttons),
      buttons,
      &colorScheme
    };

    // 显示消息框
    int buttonId = 0;
    if (SDL_ShowMessageBox(&messageboxdata, &buttonId) < 0)
    {
      // SDL 消息框显示失败，使用 logger 输出错误
      if (logger)
      {
        logger->critical("SDL_ShowMessageBox 失败：{}", SDL_GetError());
      }
    }
    if (buttonId == 0)
    {
      // 用户点击了"复制错误信息并退出"
      detail::copy_to_clipboard(error_message);
    }

    // 终止程序
    std::abort();
  }
} // namespace lw
