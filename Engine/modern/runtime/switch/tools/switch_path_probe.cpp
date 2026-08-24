#include "Playground/Switch/SwitchPath.h"

#include <iostream>

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "encode-lines") {
    std::string logical;
    while (std::getline(std::cin, logical)) {
      const auto encoded =
          playground::switch_runtime::encodeStoragePath(logical);
      if (encoded.empty())
        return 1;
      std::cout << encoded.generic_string() << '\n';
    }
    return 0;
  }
  if (argc != 3) {
    std::cerr << "usage: switch-path-probe encode|list path\n"
                 "       switch-path-probe encode-lines\n";
    return 64;
  }
  const std::string operation = argv[1];
  if (operation == "encode") {
    const auto encoded =
        playground::switch_runtime::encodeStoragePath(argv[2]);
    if (encoded.empty())
      return 1;
    std::cout << encoded.generic_string() << '\n';
    return 0;
  }
  if (operation == "list") {
    std::vector<std::string> names;
    std::string error;
    if (!playground::switch_runtime::listStorageDirectory(argv[2], names,
                                                           error)) {
      std::cerr << error << '\n';
      return 1;
    }
    for (const auto &name : names)
      std::cout << name << '\n';
    return 0;
  }
  return 64;
}
