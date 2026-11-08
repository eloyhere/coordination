#pragma once

// 语法节点总入口：一次包含 syntax/ 下的全部节点与打印工具。
// 只需要节点类型、不需要打印时可以去掉 SyntaxPrinter.h。

#include "ExpressionNodes.h"
#include "StatementNodes.h"
#include "SyntaxKind.h"
#include "SyntaxNode.h"
#include "SyntaxPrinter.h"
#include "SyntaxSpan.h"
#include "SyntaxToken.h"
