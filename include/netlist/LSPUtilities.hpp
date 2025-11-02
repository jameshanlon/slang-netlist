#pragma once

#include "slang/analysis/ValueDriver.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/LSPUtilities.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/text/FormatBuffer.h"

namespace slang::netlist {

struct LSPUtilities {

  /// Get a string representation of the LSP for a driver for a particular
  /// symbol.
  static auto getLSPName(const ast::ValueSymbol &symbol,
                         const analysis::ValueDriver &driver) -> std::string {
    FormatBuffer buf;
    ast::EvalContext evalContext(symbol);
    ast::LSPUtilities::stringifyLSP(*driver.prefixExpression, evalContext, buf);
    return buf.str();
  }
};

} // namespace slang::netlist
