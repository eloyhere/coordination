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

}  // namespace

std::string standardAnnotationsSource(std::string* error) {
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
    const std::filesystem::path file = directory / "annotations.coo";
    std::string text = readText(file);
    if (!text.empty()) {
      return text;
    }
    if (error != nullptr) {
      *error = "cannot read standard annotation library '" +
               platform::pathToUtf8(file) + "'";
    }
  }
  return {};
}

}  // namespace coord
