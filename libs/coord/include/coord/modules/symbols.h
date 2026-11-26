#pragma once

#include "coord/modules/access.h"

#include <string>
#include <vector>

namespace coord::model {

/// Tooling-facing symbol descriptors. They are intentionally decoupled from the
/// typed `Type` graph in sema.h so module/package indexes can be built without
/// running full semantic analysis.
struct ClassSymbol {
  std::string name;
  std::string base;
  std::vector<std::string> interfaces;
  Access access = Access::Default;
  bool isFinal = false;
  bool isInterface = false;
};

struct MethodSymbol {
  std::string name;
  std::string returnType;
  std::vector<std::string> parameters;
  Access access = Access::Default;
  bool isVirtual = false;
  bool isOverride = false;
  bool isFinal = false;
  bool isConst = false;
};

struct FunctionSymbol {
  std::string name;
  std::string returnType;
  std::vector<std::string> parameters;
  bool isGeneric = false;
};

struct VariableSymbol {
  std::string name;
  std::string type;
  Access access = Access::Default;
  bool isConst = false;
  bool isField = false;
};

}  // namespace coord::model
