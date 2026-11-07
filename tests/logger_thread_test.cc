#include <barrier>
#include <exception>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "tf/logger.h"

TEST_CASE("Logger serializes concurrent first use after shutdown") {
  tf::Logger::shutdown();
  struct RestoreLogger {
    ~RestoreLogger() {
      tf::Logger::shutdown();
      tf::Logger::init(tf::LogLevel::Warn);
    }
  } restore;
  constexpr size_t count = 16;
  std::barrier start(static_cast<std::ptrdiff_t>(count));
  std::vector<std::shared_ptr<spdlog::logger>> values(count);
  std::vector<std::exception_ptr> errors(count);
  {
    std::vector<std::jthread> threads;
    for (size_t i = 0; i < count; ++i) {
      threads.emplace_back([&, i] {
        start.arrive_and_wait();
        try {
          values[i] = tf::Logger::get();
        } catch (...) {
          errors[i] = std::current_exception();
        }
      });
    }
  }
  for (size_t i = 0; i < count; ++i) {
    CHECK_FALSE(errors[i]);
    CHECK(values[i] != nullptr);
    CHECK(values[i] == values[0]);
  }
}
