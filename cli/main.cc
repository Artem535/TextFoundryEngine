#include <iostream>

#include "app.h"

int main(int argc, char** argv) {
  return cli::RunApplication(argc, argv, std::cout, std::cerr);
}
