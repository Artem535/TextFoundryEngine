#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace cli {

struct AppConfig {
  std::string data_path = ".tfe-data";
  std::string project_key = "default";
  bool strict = false;
  bool json = false;
};

int RunApplication(int argc, char** argv, std::ostream& output,
                   std::ostream& errors);

int RunApplication(const std::vector<std::string>& args,
                   std::ostream& output, std::ostream& errors);

}  // namespace cli
