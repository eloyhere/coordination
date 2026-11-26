#pragma once

#include "coord/modules/outline.h"

#include <string>
#include <vector>

namespace coord::hint {

enum class HintKind {
  Symbol,
  Signature,
  Property,
  Annotation,
};

[[nodiscard]] inline const char* hintKindName(HintKind kind) {
  switch (kind) {
    case HintKind::Symbol:
      return "symbol";
    case HintKind::Signature:
      return "signature";
    case HintKind::Property:
      return "property";
    case HintKind::Annotation:
      return "annotation";
  }
  return "symbol";
}

struct Hint {
  HintKind kind = HintKind::Symbol;
  std::string label;
  std::string detail;
  Span span;
};

/// IDE hints derived from the outline tree. `all()` is the symbol list used by
/// completion; `at(offset)` is the contextual hint for the node under the
/// cursor (signature, properties, annotations, or child symbols).
class HintEngine {
 public:
  explicit HintEngine(const outline::Outline& outline) : outline_(outline) {}

  [[nodiscard]] std::vector<Hint> all() const {
    std::vector<Hint> hints;
    for (const outline::OutlineNode* node : outline_.leaves()) {
      Hint hint;
      hint.kind = HintKind::Symbol;
      hint.label = node->name;
      hint.detail = node->kindName();
      const std::string* signature = node->property("signature");
      if (signature != nullptr && !signature->empty()) {
        hint.detail += "  " + *signature;
      }
      hint.span = node->span;
      hints.push_back(std::move(hint));
    }
    return hints;
  }

  [[nodiscard]] std::vector<Hint> at(std::uint32_t offset) const {
    const outline::OutlineNode* node = outline_.at(offset);
    std::vector<Hint> hints;
    if (node == nullptr) {
      return hints;
    }

    if (!node->leaf) {
      for (const outline::OutlineNode& child : node->children) {
        Hint hint;
        hint.kind = HintKind::Symbol;
        hint.label = child.name;
        hint.detail = child.kindName();
        hint.span = child.span;
        hints.push_back(std::move(hint));
      }
      return hints;
    }

    Hint signature;
    signature.kind = HintKind::Signature;
    signature.label = node->name;
    const std::string* text = node->property("signature");
    signature.detail = text != nullptr ? *text : node->kindName();
    signature.span = node->span;
    hints.push_back(std::move(signature));

    for (const auto& [key, value] : node->properties) {
      Hint property;
      property.kind = HintKind::Property;
      property.label = key;
      property.detail = value;
      property.span = node->span;
      hints.push_back(std::move(property));
    }

    for (const outline::OutlineAnnotation& annotation : node->annotations) {
      Hint item;
      item.kind = HintKind::Annotation;
      item.label = "@" + annotation.name;
      for (const std::string& argument : annotation.arguments) {
        if (!item.detail.empty()) {
          item.detail += ", ";
        }
        item.detail += argument;
      }
      item.span = node->span;
      hints.push_back(std::move(item));
    }
    return hints;
  }

 private:
  const outline::Outline& outline_;
};

}  // namespace coord::hint
