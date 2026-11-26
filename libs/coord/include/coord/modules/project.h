#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace coord {

struct Project {
  std::string name;
  std::filesystem::path root;
  std::filesystem::path manifest;
};

class ProjectScanner {
public:
  [[nodiscard]] std::optional<Project> scan(
      const std::filesystem::path& start) const {
    std::error_code error;
    std::filesystem::path current = std::filesystem::absolute(start, error);
    if (error) {
      return std::nullopt;
    }
    if (std::filesystem::is_regular_file(current, error)) {
      current = current.parent_path();
    }
    while (!current.empty()) {
      const std::filesystem::path manifest = current / "coordination.toml";
      if (std::filesystem::exists(manifest, error)) {
        return Project{current.filename().string(), current, manifest};
      }
      const std::filesystem::path parent = current.parent_path();
      if (parent == current) {
        break;
      }
      current = parent;
    }
    return std::nullopt;
  }
};

class ProjectManager {
public:
  bool open(const std::filesystem::path& start) {
    project_ = scanner_.scan(start);
    return project_.has_value();
  }

  [[nodiscard]] const Project* current() const {
    return project_ ? &*project_ : nullptr;
  }

private:
  ProjectScanner scanner_;
  std::optional<Project> project_;
};

}  // namespace coord
