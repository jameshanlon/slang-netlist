#include "slang/driver/Driver.h"

#include "report/ReportDrivers.hpp"
#include "report/ReportPorts.hpp"
#include "report/ReportVariables.hpp"

#include "netlist/VisitAll.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTSerializer.h"
#include "slang/ast/Compilation.h"
#include "slang/text/FormatBuffer.h"
#include "slang/text/Json.h"
#include "slang/util/OS.h"
#include "slang/util/Util.h"
#include "slang/util/VersionInfo.h"

#include "fmt/format.h"

using namespace slang;
using namespace slang::ast;
using namespace slang::driver;
using namespace slang::report;

namespace {

auto generateJson(Compilation &compilation, JsonWriter &writer,
                  const std::vector<std::string> &scopes) {
  writer.setPrettyPrint(true);
  ASTSerializer serializer(compilation, writer);
  if (scopes.empty()) {
    serializer.serialize(compilation.getRoot());
  } else {
    for (auto const &scopeName : scopes) {
      auto const *sym = compilation.getRoot().lookupName(scopeName);
      if (sym != nullptr) {
        serializer.serialize(*sym);
      }
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

  std::vector<std::string> astJsonScopes;
  driver.cmdLine.add(
      "--ast-json-scope", astJsonScopes,
      "When dumping AST to JSON, include only the scopes specified by the "
      "given hierarchical path(s)",
      "<path>");

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

    // AST JSON serialisation needs to allocate constants during traversal,
    // so it must run before the compilation is frozen.
    if (astJsonFile) {
      JsonWriter writer;
      generateJson(*compilation, writer, astJsonScopes);
      OS::writeFile(*astJsonFile, writer.view());
      return 0;
    }

    netlist::VisitAll va;
    compilation->getRoot().visit(va);
    compilation->freeze();

    if (reportPorts) {
      FormatBuffer buf;
      ReportPorts visitor(*compilation);
      compilation->getRoot().visit(visitor);
      visitor.report(buf);
      OS::print(buf.str());
      return 0;
    }

    if (reportVariables) {
      FormatBuffer buf;
      ReportVariables visitor(*compilation);
      compilation->getRoot().visit(visitor);
      visitor.report(buf);
      OS::print(buf.str());
      return 0;
    }

    if (reportDrivers) {
      auto analysisManager = driver.runAnalysis(*compilation);
      if (!driver.reportDiagnostics(true)) {
        return 1;
      }
      FormatBuffer buf;
      ReportDrivers visitor(*compilation, *analysisManager);
      compilation->getRoot().visit(visitor);
      visitor.report(buf);
      OS::print(buf.str());
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
