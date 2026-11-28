#include "coord/ast.h"

#include <ostream>
#include <sstream>

namespace coord {

Stmt::~Stmt() = default;
Decl::~Decl() = default;
Pattern::~Pattern() = default;

namespace {

const char* typeExprKindName(TypeExprKind kind) {
  switch (kind) {
    case TypeExprKind::Named:
      return "Named";
    case TypeExprKind::Pointer:
      return "Pointer";
    case TypeExprKind::Ref:
      return "Ref";
    case TypeExprKind::Const:
      return "Const";
    case TypeExprKind::Union:
      return "Union";
    case TypeExprKind::Intersect:
      return "Intersect";
    case TypeExprKind::Function:
      return "Function";
    case TypeExprKind::Array:
      return "Array";
  }
  return "?";
}

void indent(std::ostream& out, int depth) {
  for (int i = 0; i < depth; ++i) {
    out << "  ";
  }
}

void dumpExpr(const Expr* expr, std::ostream& out, int depth);

void dumpParams(const std::vector<Param>& params, std::ostream& out, int depth) {
  for (const Param& param : params) {
    indent(out, depth);
    out << "Param " << param.name << ": " << describeType(param.type.get())
        << '\n';
    if (param.defaultValue) {
      dumpExpr(param.defaultValue.get(), out, depth + 1);
    }
  }
}

void dumpStmt(const Stmt* stmt, std::ostream& out, int depth) {
  if (stmt == nullptr) {
    return;
  }
  indent(out, depth);
  switch (stmt->kind) {
    case StmtKind::Block:
      out << "Block\n";
      for (const auto& child : stmt->statements) {
        dumpStmt(child.get(), out, depth + 1);
      }
      return;
    case StmtKind::Var:
      out << "Var " << (stmt->isConst ? "const " : "")
          << describeType(stmt->type.get()) << ' ' << stmt->name;
      if (stmt->directInit) {
        out << " (direct-init)";
      }
      out << '\n';
      if (stmt->expr) {
        dumpExpr(stmt->expr.get(), out, depth + 1);
      }
      return;
    case StmtKind::Expr:
      out << "ExprStmt\n";
      dumpExpr(stmt->expr.get(), out, depth + 1);
      return;
    case StmtKind::If:
      out << "If\n";
      dumpExpr(stmt->condition.get(), out, depth + 1);
      dumpStmt(stmt->thenBranch.get(), out, depth + 1);
      dumpStmt(stmt->elseBranch.get(), out, depth + 1);
      return;
    case StmtKind::While:
      out << "While\n";
      dumpExpr(stmt->condition.get(), out, depth + 1);
      dumpStmt(stmt->thenBranch.get(), out, depth + 1);
      return;
    case StmtKind::Return:
      out << "Return\n";
      if (stmt->expr) {
        dumpExpr(stmt->expr.get(), out, depth + 1);
      }
      return;
    case StmtKind::Break:
      out << "Break\n";
      return;
    case StmtKind::Continue:
      out << "Continue\n";
      return;
    case StmtKind::Destroy:
      out << "Destroy\n";
      dumpExpr(stmt->expr.get(), out, depth + 1);
      return;
  }
}

void dumpExpr(const Expr* expr, std::ostream& out, int depth) {
  if (expr == nullptr) {
    return;
  }
  indent(out, depth);
  switch (expr->kind) {
    case ExprKind::IntLit:
      out << "IntLit " << expr->intValue << '\n';
      return;
    case ExprKind::DecimalLit:
      out << "DecimalLit " << expr->decimalValue << '\n';
      return;
    case ExprKind::CharLit:
      out << "CharLit U+" << std::hex
          << static_cast<std::uint32_t>(expr->charValue) << std::dec << '\n';
      return;
    case ExprKind::BoolLit:
      out << "BoolLit " << (expr->boolValue ? "true" : "false") << '\n';
      return;
    case ExprKind::NullLit:
      out << "NullLit\n";
      return;
    case ExprKind::StringLit:
      out << "StringLit \"" << expr->stringValue << "\"\n";
      return;
    case ExprKind::Name:
      out << "Name " << expr->name << '\n';
      return;
    case ExprKind::This:
      out << "This\n";
      return;
    case ExprKind::Super:
      out << "Super\n";
      return;
    case ExprKind::Unary:
      out << "Unary '" << expr->op << "'\n";
      dumpExpr(expr->lhs.get(), out, depth + 1);
      return;
    case ExprKind::Binary:
      out << "Binary '" << expr->op << "'\n";
      dumpExpr(expr->lhs.get(), out, depth + 1);
      dumpExpr(expr->rhs.get(), out, depth + 1);
      return;
    case ExprKind::Call:
      out << "Call\n";
      dumpExpr(expr->lhs.get(), out, depth + 1);
      for (const auto& arg : expr->args) {
        dumpExpr(arg.get(), out, depth + 1);
      }
      return;
    case ExprKind::Member:
      out << "Member ." << expr->name << '\n';
      dumpExpr(expr->lhs.get(), out, depth + 1);
      return;
    case ExprKind::Index:
      out << "Index\n";
      dumpExpr(expr->lhs.get(), out, depth + 1);
      dumpExpr(expr->rhs.get(), out, depth + 1);
      return;
    case ExprKind::Assign:
      out << "Assign '" << expr->op << "'\n";
      dumpExpr(expr->lhs.get(), out, depth + 1);
      dumpExpr(expr->rhs.get(), out, depth + 1);
      return;
    case ExprKind::IfExpr:
      out << "IfExpr\n";
      if (expr->ifExpr != nullptr) {
        dumpExpr(expr->ifExpr->condition.get(), out, depth + 1);
        dumpExpr(expr->ifExpr->thenValue.get(), out, depth + 1);
        dumpExpr(expr->ifExpr->elseValue.get(), out, depth + 1);
      }
      return;
    case ExprKind::MethodRef:
      out << "MethodRef '" << expr->name << "'\n";
      dumpExpr(expr->lhs.get(), out, depth + 1);
      return;
  }
}

void dumpDecl(const Decl* decl, std::ostream& out, int depth) {
  indent(out, depth);
  out << describeDeclKind(decl->kind);
  if (!decl->name.empty()) {
    out << ' ' << decl->name;
  }
  if (decl->exported) {
    out << " (export)";
  }
  if (decl->isVirtual) {
    out << " (virtual)";
  }
  if (decl->isStatic) {
    out << " (static)";
  }
  out << '\n';

  for (const AnnotationUse& use : decl->annotations) {
    indent(out, depth + 1);
    out << "Annotation @" << use.name << '\n';
    for (const auto& arg : use.args) {
      dumpExpr(arg.get(), out, depth + 2);
    }
  }

  for (const GenericParam& generic : decl->generics) {
    indent(out, depth + 1);
    out << "Generic " << generic.name;
    if (generic.bound) {
      out << " extends " << describeType(generic.bound.get());
    }
    out << '\n';
  }

  if (decl->base) {
    indent(out, depth + 1);
    out << "Base " << describeType(decl->base.get()) << '\n';
  }
  for (const auto& iface : decl->interfaces) {
    indent(out, depth + 1);
    out << "Implements " << describeType(iface.get()) << '\n';
  }

  if (decl->kind == DeclKind::Field || decl->kind == DeclKind::Method ||
      decl->kind == DeclKind::Function) {
    indent(out, depth + 1);
    out << "Type " << describeType(decl->type.get()) << '\n';
  }

  dumpParams(decl->params, out, depth + 1);

  if (decl->kind == DeclKind::Enum) {
    indent(out, depth + 1);
    out << "Variants " << decl->variants.size() << '\n';
    for (const EnumVariant& variant : decl->variants) {
      indent(out, depth + 2);
      out << variant.name;
      if (variant.value) {
        out << " =\n";
        dumpExpr(variant.value.get(), out, depth + 3);
      } else {
        out << " = " << variant.resolvedValue << '\n';
      }
    }
  }

  if (decl->kind == DeclKind::Class || decl->kind == DeclKind::Interface ||
      decl->kind == DeclKind::Annotation || decl->kind == DeclKind::Enum) {
    indent(out, depth + 1);
    out << "Members " << decl->members.size() << '\n';
    for (const auto& member : decl->members) {
      dumpDecl(member.get(), out, depth + 2);
    }
  }

  if (decl->synthesizedDestructor) {
    indent(out, depth + 1);
    out << "SynthesizedDestructor order:";
    for (const Decl* field : decl->destroyOrder) {
      out << ' ' << field->name;
    }
    out << '\n';
  }

  if (decl->init) {
    indent(out, depth + 1);
    out << "Initializer\n";
    dumpExpr(decl->init.get(), out, depth + 2);
  }

  if (decl->body) {
    dumpStmt(decl->body.get(), out, depth + 1);
  }
}

}  // namespace

std::string describeType(const TypeExpr* type) {
  if (type == nullptr) {
    return "<none>";
  }
  switch (type->kind) {
    case TypeExprKind::Named: {
      std::string result = type->name;
      if (!type->args.empty()) {
        result += '<';
        for (std::size_t i = 0; i < type->args.size(); ++i) {
          if (i != 0) {
            result += ", ";
          }
          result += describeType(type->args[i].get());
        }
        result += '>';
      }
      return result;
    }
    case TypeExprKind::Pointer:
      return describeType(type->inner.get()) + "*";
    case TypeExprKind::Ref:
      return describeType(type->inner.get()) + "&";
    case TypeExprKind::Const:
      return "const " + describeType(type->inner.get());
    case TypeExprKind::Union: {
      std::string result;
      for (std::size_t i = 0; i < type->args.size(); ++i) {
        if (i != 0) {
          result += " | ";
        }
        result += describeType(type->args[i].get());
      }
      return result;
    }
    case TypeExprKind::Intersect: {
      std::string result;
      for (std::size_t i = 0; i < type->args.size(); ++i) {
        if (i != 0) {
          result += " & ";
        }
        result += describeType(type->args[i].get());
      }
      return result;
    }
    case TypeExprKind::Function:
      return "(...) -> " + describeType(type->returnType.get());
    case TypeExprKind::Array:
      return describeType(type->inner.get()) + "[N]";
  }
  return "<invalid>";
}

std::string describeDeclKind(DeclKind kind) {
  switch (kind) {
    case DeclKind::Class:
      return "Class";
    case DeclKind::Interface:
      return "Interface";
    case DeclKind::Enum:
      return "Enum";
    case DeclKind::Field:
      return "Field";
    case DeclKind::Method:
      return "Method";
    case DeclKind::Constructor:
      return "Constructor";
    case DeclKind::Destructor:
      return "Destructor";
    case DeclKind::Function:
      return "Function";
    case DeclKind::Annotation:
      return "Annotation";
    case DeclKind::Const:
      return "Const";
  }
  return "Decl";
}

std::string describeExprKind(ExprKind kind) {
  switch (kind) {
    case ExprKind::IntLit:
      return "IntLit";
    case ExprKind::DecimalLit:
      return "DecimalLit";
    case ExprKind::CharLit:
      return "CharLit";
    case ExprKind::BoolLit:
      return "BoolLit";
    case ExprKind::NullLit:
      return "NullLit";
    case ExprKind::StringLit:
      return "StringLit";
    case ExprKind::Name:
      return "Name";
    case ExprKind::This:
      return "This";
    case ExprKind::Super:
      return "Super";
    case ExprKind::Unary:
      return "Unary";
    case ExprKind::Binary:
      return "Binary";
    case ExprKind::Call:
      return "Call";
    case ExprKind::Member:
      return "Member";
    case ExprKind::Index:
      return "Index";
    case ExprKind::Assign:
      return "Assign";
  }
  return "Expr";
}

void dumpAst(const ModuleAst& module, std::ostream& out) {
  out << "Module";
  for (const std::string& part : module.modulePath) {
    out << ' ' << part;
  }
  out << '\n';
  for (const ImportDecl& import : module.imports) {
    out << "  Import";
    for (const std::string& part : import.path) {
      out << ' ' << part;
    }
    if (import.hasAlias) {
      out << " as " << import.alias;
    }
    out << '\n';
  }
  for (const auto& decl : module.decls) {
    dumpDecl(decl.get(), out, 1);
  }
}

}  // namespace coord
