#pragma once

#include "coord/ast.h"
#include "coord/support.h"

#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace coord::annotation {

enum class Kind {
  Meta,
  Processor,
  Intrinsic,
};

enum class Retention {
  Compile,
  Runtime,
  Invocation,
};

struct Descriptor {
  std::string name;
  Kind kind = Kind::Processor;
  bool isMeta = false;
  bool repeatable = false;
  bool inherit = false;
  bool documented = false;
  bool targetAll = true;
  Retention retention = Retention::Compile;
  std::string processorMethod;
  std::set<std::string> targets;
  const Decl* declaration = nullptr;
};

using MetaProcessor = std::function<void(const AnnotationUse&,
                                         Descriptor&, DiagnosticEngine&)>;

[[nodiscard]] inline bool isTargetName(std::string_view name) {
  return name == "class" || name == "interface" || name == "annotation" ||
         name == "function" || name == "method" || name == "field" ||
         name == "variable" || name == "parameter" || name == "module" ||
         name == "enumerate" || name == "enum_value" || name == "type" ||
         name == "constructor" || name == "block";
}

/// C++-implemented meta-annotation processors. Only these six names are meta:
/// Target, Repeatable, Inherit, Meta, Retention, Documented.
class Registry {
 public:
  Registry() {
    processors_["Target"] = [](const AnnotationUse& use, Descriptor& descriptor,
                               DiagnosticEngine& diagnostics) {
      if (use.args.empty()) {
        diagnostics.error(use.span, "E2044",
                          "@Target requires at least one target name");
        return;
      }
      descriptor.targetAll = false;
      for (const ExprPtr& arg : use.args) {
        if (arg == nullptr || arg->kind != ExprKind::Name) {
          diagnostics.error(use.span, "E2044",
                            "@Target arguments must be target names");
          continue;
        }
        if (!isTargetName(arg->name)) {
          diagnostics.error(arg->span, "E2044",
                            "unknown annotation target '" + arg->name + "'");
          continue;
        }
        descriptor.targets.insert(arg->name);
      }
    };

    processors_["Repeatable"] = [](const AnnotationUse&, Descriptor& descriptor,
                                   DiagnosticEngine&) {
      descriptor.repeatable = true;
    };
    processors_["Inherit"] = [](const AnnotationUse&, Descriptor& descriptor,
                                DiagnosticEngine&) {
      descriptor.inherit = true;
    };
    processors_["Documented"] = [](const AnnotationUse&, Descriptor& descriptor,
                                   DiagnosticEngine&) {
      descriptor.documented = true;
    };
    processors_["Meta"] = [](const AnnotationUse&, Descriptor& descriptor,
                             DiagnosticEngine& diagnostics) {
      descriptor.isMeta = true;
      (void)diagnostics;
    };
    processors_["Retention"] = [](const AnnotationUse& use,
                                  Descriptor& descriptor,
                                  DiagnosticEngine& diagnostics) {
      if (use.args.empty()) {
        descriptor.retention = Retention::Runtime;
        return;
      }
      if (use.args.size() == 1 && use.args[0] != nullptr &&
          use.args[0]->kind == ExprKind::Name) {
        if (use.args[0]->name == "runtime") {
          descriptor.retention = Retention::Runtime;
          return;
        }
        if (use.args[0]->name == "invocation") {
          descriptor.retention = Retention::Invocation;
          return;
        }
      }
      diagnostics.error(
          use.span, "E2021",
          "@Retention on an annotation must be runtime or invocation");
    };

    for (const char* name :
         {"Target", "Repeatable", "Inherit", "Meta", "Retention",
          "Documented"}) {
      metaNames_.insert(name);
      Descriptor descriptor;
      descriptor.name = name;
      descriptor.kind = Kind::Meta;
      descriptor.isMeta = true;
      builtin_[name] = std::move(descriptor);
    }
  }

  [[nodiscard]] bool isMeta(std::string_view name) const {
    return metaNames_.find(std::string(name)) != metaNames_.end();
  }

  [[nodiscard]] static bool isIntrinsicName(std::string_view name) {
    return name == "Result" || name == "Expect" || name == "Unsigned" ||
           name == "Signed" || name == "Length" || name == "Native" ||
           name == "Unique" || name == "Share" || name == "Optional" ||
           name == "Proxy" || name == "Invocation";
  }

  [[nodiscard]] const Descriptor* find(std::string_view name) const {
    const auto builtin = builtin_.find(std::string(name));
    if (builtin != builtin_.end()) {
      return &builtin->second;
    }
    const auto user = descriptors_.find(std::string(name));
    return user == descriptors_.end() ? nullptr : &user->second;
  }

  /// Build (or rebuild) the descriptor for an annotation declaration.
  const Descriptor* build(const Decl& decl, DiagnosticEngine& diagnostics) {
    Descriptor descriptor;
    descriptor.name = decl.name;
    descriptor.kind = isIntrinsicName(decl.name) ? Kind::Intrinsic
                                                 : Kind::Processor;
    descriptor.declaration = &decl;

    for (const AnnotationUse& use : decl.annotations) {
      if (!isMeta(use.name)) {
        continue;
      }
      const auto processor = processors_.find(use.name);
      if (processor != processors_.end()) {
        processor->second(use, descriptor, diagnostics);
      }
    }

    if (descriptor.isMeta) {
      diagnostics.error(decl.span, "E2043",
                        "users cannot define meta-annotations");
    }

    auto [it, inserted] = descriptors_.insert_or_assign(decl.name, descriptor);
    (void)inserted;
    return &it->second;
  }

 private:
  std::unordered_map<std::string, MetaProcessor> processors_;
  std::unordered_map<std::string, Descriptor> builtin_;
  std::unordered_map<std::string, Descriptor> descriptors_;
  std::unordered_set<std::string> metaNames_;
};

}  // namespace coord::annotation
