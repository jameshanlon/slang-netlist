#include "Test.hpp"

#include "slang/diagnostics/DiagnosticEngine.h"

auto report(const Diagnostics &diags) -> std::string {
  if (diags.empty()) {
    return "";
  }

  return DiagnosticEngine::reportAll(SyntaxTree::getDefaultSourceManager(),
                                     diags);
}
