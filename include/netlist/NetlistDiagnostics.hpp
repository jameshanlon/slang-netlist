#pragma once

#include "slang/ast/Compilation.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/diagnostics/Diagnostics.h"
#include "slang/diagnostics/TextDiagnosticClient.h"

#define NETLIST_DIAGNOSTICS \
  X(SymbolReference, 0, "symbol {}") \
  X(InputPort, 1, "input port {}") \
  X(OutputPort, 2, "output port {}") \
  X(Assignment, 3, "assignment") \
  X(Conditional, 4, "conditional statement") \
  X(Case, 5, "case statement")

namespace slang::diag {

#define X(name, code, text) \
  inline constexpr DiagCode name(DiagSubsystem::Netlist, code);
NETLIST_DIAGNOSTICS
#undef X

} // namespace slang::diag

namespace slang::netlist {

struct NetlistDiagnostics {

  DiagnosticEngine engine;
  std::shared_ptr<TextDiagnosticClient> client;

  NetlistDiagnostics(ast::Compilation const &compilation)
      : engine(*compilation.getSourceManager()),
        client(std::make_shared<TextDiagnosticClient>()) {

#define X(name, code, text) \
  engine.setMessage(diag::name, text); \
  engine.setSeverity(diag::name, DiagnosticSeverity::Note);
    NETLIST_DIAGNOSTICS
#undef X

    engine.addClient(client);

    // Client configuration.
    client->showColors(true);
    client->showLocation(true);
    client->showSourceLine(true);
    client->showHierarchyInstance(ShowHierarchyPathOption::Always);
  }

  auto issue(Diagnostic &diagnostic) { engine.issue(diagnostic); }

  auto getString() const { return client->getString(); }

  auto clear() { client->clear(); }
};

} // namespace slang::netlist
