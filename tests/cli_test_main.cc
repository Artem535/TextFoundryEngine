#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "../tf/logger.h"

int main(int argc, char** argv) {
  tf::Logger::init(tf::LogLevel::Warn);

  doctest::Context context;
  context.applyCommandLine(argc, argv);
  const int result = context.run();

  tf::Logger::shutdown();
  return result;
}
