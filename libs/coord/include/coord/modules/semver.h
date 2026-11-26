#pragma once

// Semantic versions and Cargo-style version requirements.
//
//   Version:      1.2.3-alpha.1+build.5
//   Requirements: *, 1, 1.2, ^1.2.3, ~1.2, >=1.0 <2.0, =1.2.3, 1.2.*
//
// Requirements use the common caret/tilde rules:
//   ^1.2.3  >=1.2.3 <2.0.0      ~1.2.3  >=1.2.3 <1.3.0
//   ^0.2.3  >=0.2.3 <0.3.0      ~1.2    >=1.2.0 <1.3.0
//   ^0.0.3  >=0.0.3 <0.0.4      ~1      >=1.0.0 <2.0.0
//
// Pre-release versions only match a requirement that names a pre-release on the
// same major.minor.patch triple (npm/Cargo behaviour).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coord::semver {

namespace detail {
inline void setError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

inline bool isDigit(char character) {
  return character >= '0' && character <= '9';
}

inline bool isIdentifierCharacter(char character) {
  return (character >= '0' && character <= '9') ||
         (character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z') || character == '-';
}

[[nodiscard]] inline bool isNumeric(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  for (const char character : text) {
    if (!isDigit(character)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline std::uint64_t toNumber(std::string_view text) {
  std::uint64_t value = 0;
  for (const char character : text) {
    value = value * 10 + static_cast<std::uint64_t>(character - '0');
  }
  return value;
}
}  // namespace detail

struct Version {
  std::uint64_t major = 0;
  std::uint64_t minor = 0;
  std::uint64_t patch = 0;
  std::string prerelease;
  std::string build;

  [[nodiscard]] bool isPrerelease() const { return !prerelease.empty(); }

  [[nodiscard]] std::string toString() const {
    std::string text = std::to_string(major) + "." + std::to_string(minor) +
                       "." + std::to_string(patch);
    if (!prerelease.empty()) {
      text += "-";
      text += prerelease;
    }
    if (!build.empty()) {
      text += "+";
      text += build;
    }
    return text;
  }

  [[nodiscard]] static std::optional<Version> parse(std::string_view text,
                                                    std::string* error = nullptr) {
    std::string_view rest = text;
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
      rest.remove_prefix(1);
    }
    while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t')) {
      rest.remove_suffix(1);
    }
    if (rest.empty()) {
      detail::setError(error, "empty version");
      return std::nullopt;
    }
    if (rest.front() == 'v' || rest.front() == 'V') {
      rest.remove_prefix(1);
    }
    Version version;
    std::size_t position = 0;
    if (!parseNumber(rest, position, version.major)) {
      detail::setError(error, "invalid version '" + std::string(text) + "'");
      return std::nullopt;
    }
    if (position < rest.size() && rest[position] == '.') {
      ++position;
      if (!parseNumber(rest, position, version.minor)) {
        detail::setError(error, "invalid version '" + std::string(text) + "'");
        return std::nullopt;
      }
      if (position < rest.size() && rest[position] == '.') {
        ++position;
        if (!parseNumber(rest, position, version.patch)) {
          detail::setError(error,
                           "invalid version '" + std::string(text) + "'");
          return std::nullopt;
        }
      }
    }
    if (position < rest.size() && rest[position] == '-') {
      ++position;
      const std::size_t begin = position;
      while (position < rest.size() && rest[position] != '+') {
        ++position;
      }
      version.prerelease = std::string(rest.substr(begin, position - begin));
      if (!validIdentifiers(version.prerelease, true)) {
        detail::setError(error, "invalid pre-release in '" + std::string(text) +
                                    "'");
        return std::nullopt;
      }
    }
    if (position < rest.size() && rest[position] == '+') {
      ++position;
      const std::size_t begin = position;
      while (position < rest.size()) {
        ++position;
      }
      version.build = std::string(rest.substr(begin, position - begin));
      if (!validIdentifiers(version.build, false)) {
        detail::setError(error,
                         "invalid build metadata in '" + std::string(text) +
                             "'");
        return std::nullopt;
      }
    }
    if (position != rest.size()) {
      detail::setError(error, "invalid version '" + std::string(text) + "'");
      return std::nullopt;
    }
    return version;
  }

 private:
  static bool parseNumber(std::string_view text, std::size_t& position,
                          std::uint64_t& value) {
    if (position >= text.size() || !detail::isDigit(text[position])) {
      return false;
    }
    std::uint64_t result = 0;
    while (position < text.size() && detail::isDigit(text[position])) {
      result = result * 10 + static_cast<std::uint64_t>(text[position] - '0');
      ++position;
    }
    value = result;
    return true;
  }

  static bool validIdentifiers(std::string_view text, bool numericRule) {
    if (text.empty()) {
      return true;
    }
    std::size_t begin = 0;
    while (begin <= text.size()) {
      const std::size_t end = text.find('.', begin);
      const std::string_view identifier =
          text.substr(begin, end == std::string_view::npos
                                 ? std::string_view::npos
                                 : end - begin);
      if (identifier.empty()) {
        return false;
      }
      bool numeric = true;
      for (const char character : identifier) {
        if (!detail::isIdentifierCharacter(character)) {
          return false;
        }
        if (!detail::isDigit(character)) {
          numeric = false;
        }
      }
      if (numericRule && numeric && identifier.size() > 1 &&
          identifier.front() == '0') {
        return false;  // leading zeroes are not allowed in numeric identifiers
      }
      if (end == std::string_view::npos) {
        break;
      }
      begin = end + 1;
    }
    return true;
  }
};

[[nodiscard]] inline int compare(const Version& left, const Version& right) {
  if (left.major != right.major) {
    return left.major < right.major ? -1 : 1;
  }
  if (left.minor != right.minor) {
    return left.minor < right.minor ? -1 : 1;
  }
  if (left.patch != right.patch) {
    return left.patch < right.patch ? -1 : 1;
  }
  if (left.prerelease.empty() && right.prerelease.empty()) {
    return 0;
  }
  if (left.prerelease.empty()) {
    return 1;  // a release outranks any pre-release
  }
  if (right.prerelease.empty()) {
    return -1;
  }
  std::size_t leftBegin = 0;
  std::size_t rightBegin = 0;
  for (;;) {
    const std::size_t leftEnd = left.prerelease.find('.', leftBegin);
    const std::size_t rightEnd = right.prerelease.find('.', rightBegin);
    const std::string_view leftIdentifier = std::string_view(left.prerelease)
                                                .substr(leftBegin,
                                                        leftEnd == std::string::npos
                                                            ? std::string::npos
                                                            : leftEnd - leftBegin);
    const std::string_view rightIdentifier =
        std::string_view(right.prerelease)
            .substr(rightBegin, rightEnd == std::string::npos
                                    ? std::string::npos
                                    : rightEnd - rightBegin);
    const bool leftNumeric = detail::isNumeric(leftIdentifier);
    const bool rightNumeric = detail::isNumeric(rightIdentifier);
    if (leftNumeric && rightNumeric) {
      const std::uint64_t leftValue = detail::toNumber(leftIdentifier);
      const std::uint64_t rightValue = detail::toNumber(rightIdentifier);
      if (leftValue != rightValue) {
        return leftValue < rightValue ? -1 : 1;
      }
    } else if (leftNumeric != rightNumeric) {
      return leftNumeric ? -1 : 1;  // numeric identifiers sort lower
    } else if (leftIdentifier != rightIdentifier) {
      return leftIdentifier < rightIdentifier ? -1 : 1;
    }
    const bool leftDone = leftEnd == std::string::npos;
    const bool rightDone = rightEnd == std::string::npos;
    if (leftDone && rightDone) {
      return 0;
    }
    if (leftDone) {
      return -1;  // fewer identifiers sort lower
    }
    if (rightDone) {
      return 1;
    }
    leftBegin = leftEnd + 1;
    rightBegin = rightEnd + 1;
  }
}

[[nodiscard]] inline bool operator==(const Version& left, const Version& right) {
  return compare(left, right) == 0;
}
[[nodiscard]] inline bool operator!=(const Version& left, const Version& right) {
  return compare(left, right) != 0;
}
[[nodiscard]] inline bool operator<(const Version& left, const Version& right) {
  return compare(left, right) < 0;
}
[[nodiscard]] inline bool operator<=(const Version& left, const Version& right) {
  return compare(left, right) <= 0;
}
[[nodiscard]] inline bool operator>(const Version& left, const Version& right) {
  return compare(left, right) > 0;
}
[[nodiscard]] inline bool operator>=(const Version& left, const Version& right) {
  return compare(left, right) >= 0;
}

struct Comparator {
  enum class Op { Equal, Greater, GreaterEqual, Less, LessEqual };

  Op op = Op::Equal;
  Version version;

  [[nodiscard]] bool matches(const Version& candidate) const {
    const int order = compare(candidate, version);
    switch (op) {
      case Op::Equal:
        return order == 0;
      case Op::Greater:
        return order > 0;
      case Op::GreaterEqual:
        return order >= 0;
      case Op::Less:
        return order < 0;
      case Op::LessEqual:
        return order <= 0;
    }
    return false;
  }

  [[nodiscard]] std::string toString() const {
    const char* symbol = "=";
    switch (op) {
      case Op::Equal:
        symbol = "=";
        break;
      case Op::Greater:
        symbol = ">";
        break;
      case Op::GreaterEqual:
        symbol = ">=";
        break;
      case Op::Less:
        symbol = "<";
        break;
      case Op::LessEqual:
        symbol = "<=";
        break;
    }
    return std::string(symbol) + version.toString();
  }
};

class Requirement {
 public:
  Requirement() = default;

  [[nodiscard]] static Requirement any() { return Requirement(); }

  [[nodiscard]] static std::optional<Requirement> parse(
      std::string_view text, std::string* error = nullptr) {
    Requirement requirement;
    std::size_t position = 0;
    bool sawToken = false;
    while (position < text.size()) {
      while (position < text.size() &&
             (text[position] == ' ' || text[position] == '\t' ||
              text[position] == ',')) {
        ++position;
      }
      if (position >= text.size()) {
        break;
      }
      sawToken = true;
      const std::size_t operBegin = position;
      while (position < text.size() &&
             (text[position] == '^' || text[position] == '~' ||
              text[position] == '>' || text[position] == '<' ||
              text[position] == '=')) {
        ++position;
      }
      const std::string_view oper = text.substr(operBegin, position - operBegin);
      while (position < text.size() &&
             (text[position] == ' ' || text[position] == '\t')) {
        ++position;
      }
      const std::size_t versionBegin = position;
      while (position < text.size() && text[position] != ',' &&
             text[position] != ' ' && text[position] != '\t') {
        ++position;
      }
      const std::string_view token = text.substr(versionBegin,
                                                 position - versionBegin);
      if (token.empty()) {
        detail::setError(error, "missing version after '" + std::string(oper) +
                                    "' in requirement '" + std::string(text) +
                                    "'");
        return std::nullopt;
      }
      if (!appendToken(requirement, oper, token, text, error)) {
        return std::nullopt;
      }
    }
    if (!sawToken) {
      return Requirement::any();
    }
    return requirement;
  }

  [[nodiscard]] bool matches(const Version& candidate) const {
    if (candidate.isPrerelease() && !allowsPrerelease(candidate)) {
      return false;
    }
    for (const Comparator& comparator : comparators_) {
      if (!comparator.matches(candidate)) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool isAny() const { return comparators_.empty(); }

  [[nodiscard]] const std::vector<Comparator>& comparators() const {
    return comparators_;
  }

  [[nodiscard]] std::string toString() const {
    if (comparators_.empty()) {
      return "*";
    }
    std::string text;
    for (std::size_t i = 0; i < comparators_.size(); ++i) {
      if (i != 0) {
        text += ", ";
      }
      text += comparators_[i].toString();
    }
    return text;
  }

 private:
  struct Partial {
    bool hasMajor = false;
    bool hasMinor = false;
    bool hasPatch = false;
    bool wildcard = false;
    std::uint64_t major = 0;
    std::uint64_t minor = 0;
    std::uint64_t patch = 0;
    std::string prerelease;
  };

  [[nodiscard]] bool allowsPrerelease(const Version& candidate) const {
    for (const Comparator& comparator : comparators_) {
      if (!comparator.version.prerelease.empty() &&
          comparator.version.major == candidate.major &&
          comparator.version.minor == candidate.minor &&
          comparator.version.patch == candidate.patch) {
        return true;
      }
    }
    return false;
  }

  static bool parsePartial(std::string_view token, Partial& partial,
                           std::string* error) {
    std::string_view rest = token;
    if (rest.size() >= 2 && rest.front() == 'v' &&
        detail::isDigit(rest[1])) {
      rest.remove_prefix(1);
    }
    if (rest.empty()) {
      detail::setError(error, "empty version in requirement");
      return false;
    }
    if (rest == "*" || rest == "x" || rest == "X") {
      partial.wildcard = true;
      return true;
    }
    std::size_t position = 0;
    for (int component = 0; component < 3; ++component) {
      if (position >= rest.size()) {
        break;
      }
      if (rest[position] == '*' || rest[position] == 'x' ||
          rest[position] == 'X') {
        partial.wildcard = true;
        ++position;
        break;
      }
      if (!detail::isDigit(rest[position])) {
        detail::setError(error, "invalid requirement version '" +
                                    std::string(token) + "'");
        return false;
      }
      std::uint64_t value = 0;
      while (position < rest.size() && detail::isDigit(rest[position])) {
        value = value * 10 + static_cast<std::uint64_t>(rest[position] - '0');
        ++position;
      }
      if (component == 0) {
        partial.major = value;
        partial.hasMajor = true;
      } else if (component == 1) {
        partial.minor = value;
        partial.hasMinor = true;
      } else {
        partial.patch = value;
        partial.hasPatch = true;
      }
      if (position >= rest.size() || rest[position] != '.') {
        break;
      }
      ++position;
      if (position >= rest.size()) {
        partial.wildcard = true;
        break;
      }
    }
    if (position < rest.size() && rest[position] == '-') {
      ++position;
      const std::size_t begin = position;
      while (position < rest.size() && rest[position] != '+') {
        ++position;
      }
      partial.prerelease = std::string(rest.substr(begin, position - begin));
      if (!Version::parse(
              std::to_string(partial.major) + "." +
                  std::to_string(partial.minor) + "." +
                  std::to_string(partial.patch) + "-" + partial.prerelease,
              error)
               .has_value()) {
        return false;
      }
    }
    if (position < rest.size() && rest[position] == '+') {
      // Build metadata never affects matching, so accept the remainder.
      position = rest.size();
    }
    if (position != rest.size()) {
      detail::setError(error, "invalid requirement version '" +
                                  std::string(token) + "'");
      return false;
    }
    if (!partial.hasMajor) {
      detail::setError(error, "invalid requirement version '" +
                                  std::string(token) + "'");
      return false;
    }
    return true;
  }

  static Version fill(const Partial& partial) {
    Version version;
    version.major = partial.major;
    version.minor = partial.hasMinor ? partial.minor : 0;
    version.patch = partial.hasPatch ? partial.patch : 0;
    version.prerelease = partial.prerelease;
    return version;
  }

  static void addExactOrRange(Requirement& requirement, const Partial& partial) {
    if (!partial.wildcard && partial.hasMajor && partial.hasMinor &&
        partial.hasPatch) {
      requirement.comparators_.push_back(
          Comparator{Comparator::Op::Equal, fill(partial)});
      return;
    }
    if (!partial.hasMajor) {
      return;
    }
    Version lower = fill(partial);
    Version upper = lower;
    if (!partial.hasMinor) {
      lower.minor = 0;
      lower.patch = 0;
      upper.major += 1;
      upper.minor = 0;
      upper.patch = 0;
    } else {
      lower.patch = 0;
      upper.minor += 1;
      upper.patch = 0;
    }
    upper.prerelease.clear();
    requirement.comparators_.push_back(
        Comparator{Comparator::Op::GreaterEqual, lower});
    requirement.comparators_.push_back(
        Comparator{Comparator::Op::Less, upper});
  }

  static bool appendToken(Requirement& requirement, std::string_view oper,
                          std::string_view token, std::string_view whole,
                          std::string* error) {
    Partial partial;
    if (!parsePartial(token, partial, error)) {
      return false;
    }
    if (partial.wildcard && !partial.hasMajor) {
      return true;  // bare "*" matches everything
    }
    if (oper.empty() && partial.wildcard) {
      // `1.2.*` is a range up to the next component, unlike `^1.2` which is
      // caret-compatible.
      addExactOrRange(requirement, partial);
      return true;
    }
    if (oper.empty() || oper == "^") {
      addCaret(requirement, partial);
      return true;
    }
    if (oper == "~") {
      addTilde(requirement, partial);
      return true;
    }
    if (oper == "=" || oper == "==") {
      addExactOrRange(requirement, partial);
      return true;
    }
    Comparator::Op op = Comparator::Op::Equal;
    if (oper == ">") {
      op = Comparator::Op::Greater;
    } else if (oper == ">=") {
      op = Comparator::Op::GreaterEqual;
    } else if (oper == "<") {
      op = Comparator::Op::Less;
    } else if (oper == "<=") {
      op = Comparator::Op::LessEqual;
    } else {
      detail::setError(error, "unknown operator '" + std::string(oper) +
                                  "' in requirement '" + std::string(whole) +
                                  "'");
      return false;
    }
    requirement.comparators_.push_back(Comparator{op, fill(partial)});
    return true;
  }

  static void addCaret(Requirement& requirement, const Partial& partial) {
    Version lower = fill(partial);
    Version upper = lower;
    upper.prerelease.clear();
    if (lower.major != 0) {
      upper.major = lower.major + 1;
      upper.minor = 0;
      upper.patch = 0;
    } else if (!partial.hasMinor) {
      upper.major = 1;
      upper.minor = 0;
      upper.patch = 0;
    } else if (lower.minor != 0 || !partial.hasPatch) {
      upper.major = 0;
      upper.minor = lower.minor + 1;
      upper.patch = 0;
    } else {
      upper.major = 0;
      upper.minor = 0;
      upper.patch = lower.patch + 1;
    }
    requirement.comparators_.push_back(
        Comparator{Comparator::Op::GreaterEqual, lower});
    requirement.comparators_.push_back(
        Comparator{Comparator::Op::Less, upper});
  }

  static void addTilde(Requirement& requirement, const Partial& partial) {
    Version lower = fill(partial);
    Version upper = lower;
    upper.prerelease.clear();
    if (!partial.hasMinor) {
      upper.major = lower.major + 1;
      upper.minor = 0;
      upper.patch = 0;
    } else {
      upper.minor = lower.minor + 1;
      upper.patch = 0;
    }
    requirement.comparators_.push_back(
        Comparator{Comparator::Op::GreaterEqual, lower});
    requirement.comparators_.push_back(
        Comparator{Comparator::Op::Less, upper});
  }

  std::vector<Comparator> comparators_;
};

[[nodiscard]] inline bool satisfies(const Version& version,
                                    const Requirement& requirement) {
  return requirement.matches(version);
}

}  // namespace coord::semver
