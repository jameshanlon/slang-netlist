#include "Test.hpp"
#include "slang/diagnostics/DiagnosticEngine.h"

std::string report(const Diagnostics &diags) {
  if (diags.empty())
    return "";

  return DiagnosticEngine::reportAll(SyntaxTree::getDefaultSourceManager(),
                                     diags);
}
