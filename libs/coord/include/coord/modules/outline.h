#pragma once

#include "coord/ast.h"
#include "coord/modules/access.h"
#include "coord/modules/descriptor.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace coord::outline {

enum class OutlineKind {
  Module,
  Class,
  Interface,
  Enum,
  Annotation,
  Field,
  Method,
  Function,
  Constructor,
  Destructor,
  EnumVariant,
  Const,
};

[[nodiscard]] inline const char* outlineKindName(OutlineKind kind) {
  switch (kind) {
    case OutlineKind::Module:
      return "module";
    case OutlineKind::Class:
      return "class";
    case OutlineKind::Interface:
      return "interface";
    case OutlineKind::Enum:
      return "enumerate";
    case OutlineKind::Annotation:
      return "annotation";
    case OutlineKind::Field:
      return "field";
    case OutlineKind::Method:
      return "method";
    case OutlineKind::Function:
      return "function";
    case OutlineKind::Constructor:
      return "constructor";
    case OutlineKind::Destructor:
      return "destructor";
    case OutlineKind::EnumVariant:
      return "enum_value";
    case OutlineKind::Const:
      return "const";
  }
  return "unknown";
}

struct OutlineAnnotation {
  std::string name;
  std::vector<std::string> arguments;
};

struct OutlineNode {
  OutlineKind kind = OutlineKind::Module;
  std::string name;
  Span span;
  const Decl* decl = nullptr;
  bool leaf = false;
  bool isProxy = false;
  std::string document;
  std::vector<OutlineAnnotation> annotations;
  std::map<std::string, std::string> properties;
  std::vector<OutlineNode> children;

  [[nodiscard]] std::string kindName() const {
    return outlineKindName(kind);
  }

  [[nodiscard]] bool hasAnnotation(std::string_view annotationName) const {
    return std::any_of(annotations.begin(), annotations.end(),
                       [&](const OutlineAnnotation& annotation) {
                         return annotation.name == annotationName;
                       });
  }

  [[nodiscard]] const OutlineAnnotation* annotation(
      std::string_view annotationName) const {
    for (const OutlineAnnotation& item : annotations) {
      if (item.name == annotationName) {
        return &item;
      }
    }
    return nullptr;
  }

  [[nodiscard]] const std::string* property(std::string_view propertyName) const {
    const auto it = properties.find(std::string(propertyName));
    return it == properties.end() ? nullptr : &it->second;
  }
};

[[nodiscard]] inline OutlineNode buildDescriptorNode(
    const descriptor::Declaration& source) {
  OutlineNode node;
  node.span = source.span;
  node.decl = source.origin;
  if (source.origin != nullptr) {
    node.document = source.origin->doc;
  }
  node.name = source.name;
  node.isProxy = source.isProxy;
  if (source.kind == "class") {
    node.kind = OutlineKind::Class;
  } else if (source.kind == "interface") {
    node.kind = OutlineKind::Interface;
  } else if (source.kind == "enumerate") {
    node.kind = OutlineKind::Enum;
  } else if (source.kind == "annotation") {
    node.kind = OutlineKind::Annotation;
  } else if (source.kind == "function") {
    node.kind = OutlineKind::Function;
    node.leaf = true;
  } else if (source.kind == "const") {
    node.kind = OutlineKind::Const;
    node.leaf = true;
  }

  for (std::size_t i = 0; i < source.annotations.size(); ++i) {
    OutlineAnnotation annotation;
    annotation.name = source.annotations[i];
    if (i < source.annotationValues.size() &&
        !source.annotationValues[i].empty()) {
      annotation.arguments.push_back(source.annotationValues[i]);
    }
    node.annotations.push_back(std::move(annotation));
  }

  node.properties["kind"] = node.kindName();
  node.properties["name"] = node.name;
  node.properties["access"] =
      source.origin != nullptr ? accessName(source.origin->access) : "default";
  node.properties["static"] =
      source.origin != nullptr && source.origin->isStatic ? "true" : "false";
  node.properties["virtual"] =
      source.origin != nullptr && source.origin->isVirtual ? "true" : "false";
  node.properties["override"] =
      source.origin != nullptr && source.origin->isOverride ? "true" : "false";
  node.properties["final"] = source.isFinal ? "true" : "false";
  node.properties["proxy"] = source.isProxy ? "true" : "false";
  node.properties["signature"] = source.signature;

  std::string annotationText;
  for (const OutlineAnnotation& annotation : node.annotations) {
    if (!annotationText.empty()) {
      annotationText += ", ";
    }
    annotationText += annotation.name;
  }
  node.properties["annotations"] = std::move(annotationText);

  const auto addLeaf = [&](const descriptor::Method& method,
                           OutlineKind kind) {
    OutlineNode child;
    child.kind = kind;
    child.name = method.name;
    child.span = method.span;
    child.decl = method.origin;
    if (method.origin != nullptr) {
      child.document = method.origin->doc;
    }
    child.leaf = true;
    child.properties["kind"] = child.kindName();
    child.properties["name"] = method.name;
    child.properties["access"] = method.access;
    child.properties["static"] = method.isStatic ? "true" : "false";
    child.properties["virtual"] = method.isVirtual ? "true" : "false";
    child.properties["override"] = method.isOverride ? "true" : "false";
    child.properties["final"] = method.isFinal ? "true" : "false";
    child.properties["signature"] = method.signature;
    for (std::size_t i = 0; i < method.annotations.size(); ++i) {
      OutlineAnnotation annotation;
      annotation.name = method.annotations[i];
      if (i < method.annotationValues.size() &&
          !method.annotationValues[i].empty()) {
        annotation.arguments.push_back(method.annotationValues[i]);
      }
      child.annotations.push_back(std::move(annotation));
    }
    node.children.push_back(std::move(child));
  };

  for (const descriptor::Variant& variant : source.variants) {
    OutlineNode child;
    child.kind = OutlineKind::EnumVariant;
    child.name = variant.name;
    child.span = variant.span;
    child.decl = nullptr;
    child.leaf = true;
    child.properties["kind"] = child.kindName();
    child.properties["name"] = variant.name;
    child.properties["value"] = std::to_string(variant.value);
    for (std::size_t i = 0; i < variant.annotations.size(); ++i) {
      OutlineAnnotation annotation;
      annotation.name = variant.annotations[i];
      if (i < variant.annotationValues.size() &&
          !variant.annotationValues[i].empty()) {
        annotation.arguments.push_back(variant.annotationValues[i]);
      }
      child.annotations.push_back(std::move(annotation));
    }
    node.children.push_back(std::move(child));
  }

  for (const descriptor::Field& field : source.fields) {
    OutlineNode child;
    child.kind = OutlineKind::Field;
    child.name = field.name;
    child.span = field.span;
    child.decl = field.origin;
    if (field.origin != nullptr) {
      child.document = field.origin->doc;
    }
    child.leaf = true;
    child.properties["kind"] = child.kindName();
    child.properties["name"] = field.name;
    child.properties["access"] = field.access;
    child.properties["signature"] = field.signature;
    for (std::size_t i = 0; i < field.annotations.size(); ++i) {
      OutlineAnnotation annotation;
      annotation.name = field.annotations[i];
      if (i < field.annotationValues.size() &&
          !field.annotationValues[i].empty()) {
        annotation.arguments.push_back(field.annotationValues[i]);
      }
      child.annotations.push_back(std::move(annotation));
    }
    node.children.push_back(std::move(child));
  }

  for (const descriptor::Method& method : source.constructors) {
    addLeaf(method, OutlineKind::Constructor);
  }
  for (const descriptor::Method& method : source.destructors) {
    addLeaf(method, OutlineKind::Destructor);
  }
  for (const descriptor::Method& method : source.methods) {
    addLeaf(method, OutlineKind::Method);
  }

  for (const OutlineNode& child : node.children) {
    if (child.span.valid() && child.span.begin < node.span.begin) {
      node.span.begin = child.span.begin;
    }
    if (child.span.valid() && child.span.end > node.span.end) {
      node.span.end = child.span.end;
    }
  }
  return node;
}

struct Outline {
  OutlineNode root;

  [[nodiscard]] static Outline build(const ModuleAst& module) {
    Outline outline;
    outline.root.kind = OutlineKind::Module;
    outline.root.name = "<module>";
    for (std::size_t i = 0; i < module.modulePath.size(); ++i) {
      if (i != 0) {
        outline.root.name += ".";
      }
      outline.root.name += module.modulePath[i];
    }
    outline.root.properties["kind"] = "module";
    outline.root.properties["name"] = outline.root.name;
    const std::vector<descriptor::Declaration> descriptors =
        descriptor::Builder::build(module);
    for (const descriptor::Declaration& declaration : descriptors) {
      outline.root.children.push_back(buildDescriptorNode(declaration));
    }
    return outline;
  }

  [[nodiscard]] std::vector<const OutlineNode*> leaves() const {
    std::vector<const OutlineNode*> result;
    collectLeaves(root, result);
    return result;
  }

  [[nodiscard]] const OutlineNode* at(std::uint32_t offset) const {
    return findAt(root, offset);
  }

 private:
  static void collectLeaves(const OutlineNode& node,
                            std::vector<const OutlineNode*>& out) {
    if (node.leaf) {
      out.push_back(&node);
    }
    for (const OutlineNode& child : node.children) {
      collectLeaves(child, out);
    }
  }

  static const OutlineNode* findAt(const OutlineNode& node,
                                   std::uint32_t offset) {
    for (const OutlineNode& child : node.children) {
      if (child.span.valid() && offset >= child.span.begin &&
          offset < child.span.end) {
        return findAt(child, offset);
      }
    }
    return &node;
  }
};

}  // namespace coord::outline
