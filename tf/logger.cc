//
// Logger implementation for TextFoundry
//

#include "logger.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace tf {

std::shared_ptr<spdlog::logger> Logger::instance_ = nullptr;
bool Logger::initialized_ = false;

static spdlog::level::level_enum toSpdlogLevel(const LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return spdlog::level::trace;
    case LogLevel::Debug:
      return spdlog::level::debug;
    case LogLevel::Info:
      return spdlog::level::info;
    case LogLevel::Warn:
      return spdlog::level::warn;
    case LogLevel::Error:
      return spdlog::level::err;
    case LogLevel::Critical:
      return spdlog::level::critical;
    case LogLevel::Off:
      return spdlog::level::off;
    default:
      return spdlog::level::info;
  }
}

void Logger::init(LogLevel level) {
  if (initialized_) {
    return;
  }

  // Use stderr for logs to avoid corrupting stdout-based TUI rendering.
  instance_ = spdlog::stderr_color_mt("textfoundry");
  instance_->set_level(toSpdlogLevel(level));
  instance_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

  initialized_ = true;
}

void Logger::init(const std::string& logFile, LogLevel level) {
  if (initialized_) {
    return;
  }

  instance_ = spdlog::basic_logger_mt("textfoundry", logFile);
  instance_->set_level(toSpdlogLevel(level));
  instance_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

  initialized_ = true;
}

void Logger::shutdown() {
  // Drop our own reference first...
  instance_.reset();
  // ...then explicitly tear down spdlog's global registry while we are still
  // safely inside a normal function call (not inside the C++ runtime's
  // exit-time static-destructor sweep).
  //
  // spdlog::stderr_color_mt()/basic_logger_mt() register the logger they
  // create into spdlog::details::registry (a function-local static/Meyer's
  // singleton). Without this call, that registry keeps its own
  // std::shared_ptr<logger> alive and only gets torn down implicitly by its
  // own static destructor at process exit. The relative destruction order
  // between that registry singleton and other statics/atexit-registered
  // teardown (including this Logger's own static instance_) is unspecified
  // across translation units, and was observed to crash
  // (EXC_BAD_ACCESS inside spdlog::details::registry::~registry(), called
  // from exit()'s __cxa_finalize_ranges) deterministically on macOS CI and
  // intermittently on Windows CI once the engine's own doctest binary was
  // split out and linked as a standalone executable. Calling
  // spdlog::shutdown() here clears the registry's internal logger map and
  // thread pools synchronously and deterministically, so its later static
  // destructor has nothing left to tear down.
  spdlog::shutdown();
  initialized_ = false;
}

std::shared_ptr<spdlog::logger> Logger::get() {
  if (!initialized_) {
    init(LogLevel::Info);
  }
  return instance_;
}

void Logger::SetLevel(LogLevel level) {
  if (instance_) {
    instance_->set_level(toSpdlogLevel(level));
  }
}

}  // namespace tf
