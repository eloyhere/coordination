#pragma once

#include "coord/modules/file.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace coord {

struct Dependency {
  std::string name;
  std::string version;
  std::filesystem::path path;
  std::vector<std::string> requirements;
};

class DependencyScanner {
public:
  [[nodiscard]] std::vector<Dependency> scan(
      const std::filesystem::path& manifest) const {
    std::vector<Dependency> dependencies;
    const std::optional<std::string> text = File(manifest).readAll();
    if (!text.has_value()) {
      return dependencies;
    }
    bool inDependencies = false;
    std::size_t offset = 0;
    while (offset < text->size()) {
      const std::size_t end = text->find('\n', offset);
      std::string line = text->substr(
          offset, end == std::string::npos ? std::string::npos : end - offset);
      offset = end == std::string::npos ? text->size() : end + 1;
      line = trim(line);
      if (line.empty() || line[0] == '#') {
        continue;
      }
      if (line.front() == '[' && line.back() == ']') {
        inDependencies = line == "[dependencies]";
        continue;
      }
      if (!inDependencies) {
        continue;
      }
      const std::size_t equals = line.find('=');
      if (equals == std::string::npos) {
        continue;
      }
      Dependency dependency;
      dependency.name = trim(line.substr(0, equals));
      std::string value = trim(line.substr(equals + 1));
      if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        dependency.version = value.substr(1, value.size() - 2);
      } else if (value.front() == '{') {
        dependency.version = extractField(value, "version");
        const std::string path = extractField(value, "path");
        if (!path.empty()) {
          dependency.path = manifest.parent_path() / path;
        }
        dependency.requirements = extractArray(value, "requires");
      }
      if (!dependency.name.empty()) {
        dependencies.push_back(std::move(dependency));
      }
    }
    return dependencies;
  }

private:
  [[nodiscard]] static std::string trim(std::string text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
      ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
      --end;
    }
    return text.substr(begin, end - begin);
  }

  [[nodiscard]] static std::vector<std::string> extractArray(
      const std::string& object, const std::string& field) {
    std::vector<std::string> values;
    const std::string key = field + " =";
    std::size_t position = object.find(key);
    if (position == std::string::npos) {
      return values;
    }
    position = object.find('[', position + key.size());
    if (position == std::string::npos) {
      return values;
    }
    while (position < object.size()) {
      position = object.find('"', position);
      if (position == std::string::npos) {
        break;
      }
      const std::size_t end = object.find('"', position + 1);
      if (end == std::string::npos) {
        break;
      }
      values.push_back(object.substr(position + 1, end - position - 1));
      position = end + 1;
      const std::size_t close = object.find(']', position);
      const std::size_t comma = object.find(',', position);
      if (close != std::string::npos &&
          (comma == std::string::npos || close < comma)) {
        break;
      }
    }
    return values;
  }

  [[nodiscard]] static std::string extractField(const std::string& object,
                                               const std::string& field) {
    const std::string key = field + " =";
    std::size_t position = object.find(key);
    if (position == std::string::npos) {
      return {};
    }
    position += key.size();
    while (position < object.size() &&
           std::isspace(static_cast<unsigned char>(object[position])) != 0) {
      ++position;
    }
    if (position >= object.size() || object[position] != '"') {
      return {};
    }
    const std::size_t end = object.find('"', position + 1);
    return end == std::string::npos
               ? std::string()
               : object.substr(position + 1, end - position - 1);
  }
};

class DependencyManager {
public:
  void add(Dependency dependency) { dependencies_.push_back(std::move(dependency)); }

  void scan(const std::filesystem::path& manifest) {
    for (Dependency& dependency : Scanner_.scan(manifest)) {
      add(std::move(dependency));
    }
  }

  [[nodiscard]] const Dependency* find(const std::string& name) const {
    for (const Dependency& dependency : dependencies_) {
      if (dependency.name == name) {
        return &dependency;
      }
    }
    return nullptr;
  }

  /// Returns dependencies in "dependency before dependent" order. `ok` is set
  /// to false when a cycle or an unknown requirement is found.
  [[nodiscard]] std::vector<std::string> topologicalOrder(bool& ok) const {
    ok = true;
    std::map<std::string, const Dependency*> index;
    for (const Dependency& dependency : dependencies_) {
      index[dependency.name] = &dependency;
    }
    std::map<std::string, int> state;  // 0 unvisited, 1 visiting, 2 done
    std::vector<std::string> order;
    std::function<void(const std::string&)> visit =
        [&](const std::string& name) {
          const auto it = state.find(name);
          if (it != state.end()) {
            if (it->second == 1) {
              ok = false;
              return;
            }
            if (it->second == 2) {
              return;
            }
          }
          state[name] = 1;
          const auto found = index.find(name);
          if (found != index.end()) {
            for (const std::string& requirement :
                 found->second->requirements) {
              if (index.find(requirement) == index.end()) {
                ok = false;
              } else {
                visit(requirement);
              }
            }
          }
          state[name] = 2;
          order.push_back(name);
        };
    for (const Dependency& dependency : dependencies_) {
      visit(dependency.name);
    }
    return order;
  }

private:
  DependencyScanner Scanner_;
  std::vector<Dependency> dependencies_;
};

}  // namespace coord
