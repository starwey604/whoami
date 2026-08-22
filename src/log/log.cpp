#include <log/log.hpp>
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <toml++/toml.hpp>

namespace lw
{
  namespace
  {
    struct Storage
    {
      std::vector<sink_ptr> sinks;
      std::shared_ptr<spdlog::logger> default_logger;
    };

    Storage& storage()
    {
      static Storage s;
      return s;
    }

    std::once_flag init_flag;
  } // namespace
  void init_logging()
  {
    std::call_once(init_flag, [&]()
    {
      // parse log.toml config
      auto tbl = toml::parse_file("configs/log.toml");
      const auto is_console_enabled =
        tbl["console_sink"]["enable"].value<bool>().value_or(false);
      const auto is_file_enabled =
        tbl["file_sink"]["enable"].value<bool>().value_or(true);
      auto& [sinks, default_logger] = storage();
      if (is_console_enabled)
      {
        auto console_sink =
          std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
        sinks.push_back(console_sink);
      }
      if (is_file_enabled)
      {
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          "log/loomwood.log", 1024 * 1024 * 10, 3);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
        sinks.push_back(file_sink);
      }

      // async thread-pool
      spdlog::init_thread_pool(8192, 1);
      auto logger = std::make_shared<spdlog::async_logger>(
        "loomwood", sinks.begin(), sinks.end(), spdlog::thread_pool(),
        spdlog::async_overflow_policy::block);
      logger->set_level(spdlog::level::info);
      spdlog::register_logger(logger);
      spdlog::set_default_logger(logger);
      default_logger = logger;
    });
  }

  bool is_logging_initialized() noexcept
  {
    // 检查是否为 nullptr
    return static_cast<bool>(storage().default_logger);
  }

  std::shared_ptr<spdlog::logger> create_logger(const std::string& name,
                                                spdlog::level::level_enum level)
  {
    auto& s = storage();
    if (s.sinks.empty())
      return nullptr; // not initialized or no sinks

    auto logger = std::make_shared<spdlog::async_logger>(
      name, s.sinks.begin(), s.sinks.end(), spdlog::thread_pool(),
      spdlog::async_overflow_policy::block);
    logger->set_level(level);
    spdlog::register_logger(logger);
    return logger;
  }

  std::shared_ptr<spdlog::logger> get_default_logger()
  {
    return storage().default_logger;
  }

  void log_fatal_and_throw(const std::shared_ptr<spdlog::logger>& logger, const std::string& message)
  {
    logger->critical(message);
    throw std::runtime_error(message);
  }
} // namespace lw
