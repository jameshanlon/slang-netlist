#include "slang/driver/Driver.h"

#include "report/ReportDrivers.hpp"
#include "report/ReportPorts.hpp"
#include "report/ReportVariables.hpp"

#include "common/Wildcard.hpp"
#include "netlist/VisitAll.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTSerializer.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Scope.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/text/FormatBuffer.h"
#include "slang/text/Json.h"
#include "slang/util/OS.h"
#include "slang/util/Util.h"
#include "slang/util/VersionInfo.h"

#include "fmt/format.h"

#include <unordered_set>

using namespace slang;
using namespace slang::ast;
using namespace slang::driver;
using namespace slang::report;

namespace {

auto generateJson(Compilation &compilation, JsonWriter &writer,
                  const std::vector<const ast::Symbol *> &scopeSymbols) {
  writer.setPrettyPrint(true);
  ASTSerializer serializer(compilation, writer);
  if (scopeSymbols.empty()) {
    serializer.serialize(compilation.getRoot());
  } else {
    for (auto const *sym : scopeSymbols) {
      serializer.serialize(*sym);
    }
  }
}

/// True if @p s contains any wildcard metacharacter recognised by
/// netlist::wildcardMatch.
auto hasGlobChar(std::string_view s) -> bool {
  return s.find_first_of("*?") != std::string_view::npos ||
         s.find("...") != std::string_view::npos;
}

/// Recursively walk @p scope, appending every member whose hierarchical
/// path matches @p pattern (deduped by symbol identity, preserving AST
/// traversal order).
void collectGlobMatches(const ast::Scope &scope, std::string const &pattern,
                        std::unordered_set<const ast::Symbol *> &seen,
                        std::vector<const ast::Symbol *> &out) {
  for (auto const &member : scope.members()) {
    auto path = member.getHierarchicalPath();
    if (!path.empty() &&
        netlist::wildcardMatch(path.c_str(), pattern.c_str())) {
      if (seen.insert(&member).second) {
        out.push_back(&member);
      }
    }
    if (auto const *inst = member.as_if<ast::InstanceSymbol>()) {
      if (!inst->body.flags.has(ast::InstanceFlags::Uninstantiated)) {
        collectGlobMatches(inst->body, pattern, seen, out);
      }
    } else if (auto const *childScope = member.as_if<ast::Scope>()) {
      collectGlobMatches(*childScope, pattern, seen, out);
    }
  }
}

} // namespace

auto main(int argc, char **argv) -> int {
  OS::setupConsole();

  Driver driver;
  driver.addStandardArgs();

  std::optional<bool> showHelp;
  driver.cmdLine.add("-h,--help", showHelp, "Display available options");

  std::optional<bool> showVersion;
  driver.cmdLine.add("--version", showVersion,
                     "Display version information and exit");

  std::optional<bool> reportPorts;
  driver.cmdLine.add("--ports", reportPorts,
                     "Report all ports in the design to stdout");

  std::optional<bool> reportVariables;
  driver.cmdLine.add("--variables", reportVariables,
                     "Report all variables in the design to stdout");

  std::optional<bool> reportDrivers;
  driver.cmdLine.add("--drivers", reportDrivers,
                     "Report all drivers in the design to stdout");

  std::optional<std::string> astJsonFile;
  driver.cmdLine.add("--ast-json", astJsonFile,
                     "Dump the compiled AST in JSON format to the specified "
                     "file, or '-' for stdout",
                     "<file>", CommandLineFlags::FilePath);

  std::optional<std::string> format;
  driver.cmdLine.add("--format", format,
                     "Output format for --ports, --variables, and --drivers: "
                     "'table' (default) or 'json'",
                     "<table|json>");

  std::vector<std::string> scopes;
  driver.cmdLine.add("--scope", scopes,
                     "Restrict --ports, --variables, --drivers, and "
                     "--ast-json to the given hierarchical scope(s). Accepts "
                     "literal paths or glob patterns (*, **, ?, ...). May be "
                     "repeated.",
                     "<path>");

  std::vector<std::string> nameFilters;
  driver.cmdLine.add("--name", nameFilters,
                     "Restrict --ports, --variables, and --drivers to symbols "
                     "whose hierarchical path matches one of the given glob "
                     "patterns (same syntax as --scope: `*`/`?` stay within a "
                     "single segment, `**`/`...` cross boundaries). May be "
                     "repeated.",
                     "<pattern>");

  if (!driver.parseCommandLine(argc, argv)) {
    return 1;
  }

  if (showHelp == true) {
    std::cout << fmt::format(
        "{}\n",
        driver.cmdLine.getHelpText("slang SystemVerilog AST reporting tool")
            .c_str());
    return 0;
  }

  if (showVersion == true) {
    printf("slang-report version %d.%d.%d+%s\n", VersionInfo::getMajor(),
           VersionInfo::getMinor(), VersionInfo::getPatch(),
           std::string(VersionInfo::getHash()).c_str());
    return 0;
  }

  SLANG_TRY {
    if (!driver.processOptions()) {
      return 2;
    }

    if (!driver.parseAllSources()) {
      return 1;
    }

    auto compilation = driver.createCompilation();
    driver.reportCompilation(*compilation, true);

    if (!driver.reportDiagnostics(true)) {
      return 1;
    }

    // Scope names must be resolved before the compilation is frozen,
    // since slang's name lookup can allocate diagnostics on miss.
    // Glob patterns are expanded by walking the symbol tree; literal
    // names take the faster lookupName path.
    std::vector<const ast::Symbol *> scopeSymbols;
    std::unordered_set<const ast::Symbol *> seenSymbols;
    scopeSymbols.reserve(scopes.size());
    for (auto const &scopeName : scopes) {
      if (hasGlobChar(scopeName)) {
        auto const before = scopeSymbols.size();
        collectGlobMatches(compilation->getRoot(), scopeName, seenSymbols,
                           scopeSymbols);
        if (scopeSymbols.size() == before) {
          SLANG_THROW(std::runtime_error(
              fmt::format("scope '{}' matched no symbols", scopeName)));
        }
      } else {
        auto const *sym = compilation->getRoot().lookupName(scopeName);
        if (sym == nullptr) {
          SLANG_THROW(std::runtime_error(
              fmt::format("scope '{}' not found", scopeName)));
        }
        if (seenSymbols.insert(sym).second) {
          scopeSymbols.push_back(sym);
        }
      }
    }

    // AST JSON serialisation needs to allocate constants during traversal,
    // so it must run before the compilation is frozen.
    if (astJsonFile) {
      JsonWriter writer;
      generateJson(*compilation, writer, scopeSymbols);
      OS::writeFile(*astJsonFile, writer.view());
      return 0;
    }

    netlist::VisitAll va;
    compilation->getRoot().visit(va);

    compilation->freeze();

    enum class Format { Table, Json };
    auto outputFormat = Format::Table;
    if (format) {
      if (*format == "json") {
        outputFormat = Format::Json;
      } else if (*format == "table") {
        outputFormat = Format::Table;
      } else {
        SLANG_THROW(std::runtime_error(
            fmt::format("unknown --format value '{}'; expected 'table' or "
                        "'json'",
                        *format)));
      }
    }

    auto emit = [&](auto &visitor) {
      visitor.setNameFilters(nameFilters);
      if (scopeSymbols.empty()) {
        compilation->getRoot().visit(visitor);
      } else {
        for (auto const *sym : scopeSymbols) {
          sym->visit(visitor);
        }
      }
      if (outputFormat == Format::Json) {
        JsonWriter writer;
        writer.setPrettyPrint(true);
        visitor.report(writer);
        OS::print(writer.view());
        OS::print("\n");
      } else {
        FormatBuffer buf;
        visitor.report(buf);
        OS::print(buf.str());
      }
    };

    if (reportPorts) {
      ReportPorts visitor(*compilation);
      emit(visitor);
      return 0;
    }

    // Both --variables and --drivers need driver counts from analysis.
    if (reportVariables || reportDrivers) {
      auto analysisManager = driver.runAnalysis(*compilation);
      if (!driver.reportDiagnostics(true)) {
        return 1;
      }
      if (reportVariables) {
        ReportVariables visitor(*compilation, *analysisManager);
        emit(visitor);
      } else {
        ReportDrivers visitor(*compilation, *analysisManager);
        emit(visitor);
      }
      return 0;
    }

    SLANG_THROW(std::runtime_error(
        "no action specified; pass --ports, --variables, --drivers, or "
        "--ast-json"));
  }
  SLANG_CATCH(const std::exception &e) {
    SLANG_REPORT_EXCEPTION(e, "{}\n");
    return 1;
  }

  return 0;
}
