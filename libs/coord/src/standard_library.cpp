#include "coord/standard_library.h"

#include "coord/modules/platform.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace coord {
namespace {

std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::string readStandardFile(const char* fileName, std::string* error) {
  std::vector<std::filesystem::path> candidates;
  if (const std::optional<std::string> configured =
          platform::environment("COORD_STDLIB_DIR");
      configured.has_value() && !configured->empty()) {
    candidates.push_back(platform::pathFromUtf8(*configured));
  }
#ifdef COORD_STDLIB_DIR
  candidates.emplace_back(COORD_STDLIB_DIR);
#endif
  candidates.emplace_back("lib/standard");
  for (const auto& directory : candidates) {
    const std::filesystem::path file = directory / fileName;
    std::string text = readText(file);
    if (!text.empty()) {
      return text;
    }
    if (error != nullptr) {
      *error = "cannot read standard library file '" +
               platform::pathToUtf8(file) + "'";
    }
  }
  return {};
}

}  // namespace

std::string standardAnnotationsSource(std::string* error) {
  return readStandardFile("annotations.coo", error);
}

std::string standardUtilitySource(std::string* error) {
  return readStandardFile("utility.coo", error);
}

std::string standardLambdaSource(std::string* error) {
  return readStandardFile("lambda.coo", error);
}

}  // namespace coord
