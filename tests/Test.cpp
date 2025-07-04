#include "Test.hpp"

#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/parsing/Parser.h"
#include "slang/parsing/Preprocessor.h"
#include "slang/text/SourceManager.h"

std::string report(const Diagnostics &diags) {
  if (diags.empty())
    return "";

  return DiagnosticEngine::reportAll(SyntaxTree::getDefaultSourceManager(),
                                     diags);
}
