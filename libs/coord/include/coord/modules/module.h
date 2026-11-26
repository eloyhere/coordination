#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace coord {

/// One `.coo` file. Imports are filled by a later parser pass; the scanner only
/// establishes the physical module graph.
struct Module {
  std::string name;
  std::filesystem::path path;
  std::vector<std::string> imports;
};

class ModuleScanner {
public:
  [[nodiscard]] std::vector<Module> scan(
      const std::filesystem::path& root) const {
    std::vector<Module> modules;
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
      return modules;
    }
    for (std::filesystem::recursive_directory_iterator it(
             root, std::filesystem::directory_options::skip_permission_denied,
             error),
         end;
         !error && it != end; it.increment(error)) {
      if (it->is_directory(error)) {
        const std::string name = it->path().filename().string();
        if (name == "out" || name == ".git" || name == ".vs" ||
            name == "build" || name.rfind("cmake-build-", 0) == 0) {
          it.disable_recursion_pending();
        }
        continue;
      }
      if (it->path().extension() != ".coo") {
        continue;
      }
      std::filesystem::path relative =
          std::filesystem::relative(it->path(), root, error);
      if (error) {
        relative = it->path().filename();
      }
      std::string name = relative.replace_extension().generic_string();
      std::replace(name.begin(), name.end(), '/', '.');
      modules.push_back(Module{name, it->path(), {}});
    }
    std::sort(modules.begin(), modules.end(),
              [](const Module& lhs, const Module& rhs) {
                return lhs.name < rhs.name;
              });
    return modules;
  }
};

class ModuleManager {
public:
  void add(Module module) { modules_.push_back(std::move(module)); }

  void scan(const std::filesystem::path& root) {
    for (Module& module : scanner_.scan(root)) {
      add(std::move(module));
    }
  }

  [[nodiscard]] const Module* find(const std::string& name) const {
    for (const Module& module : modules_) {
      if (module.name == name) {
        return &module;
      }
    }
    return nullptr;
  }

  [[nodiscard]] const std::vector<Module>& modules() const { return modules_; }

private:
  ModuleScanner scanner_;
  std::vector<Module> modules_;
};

}  // namespace coord
