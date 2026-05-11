#include "slang/driver/Driver.h"

#include "report/ReportDrivers.hpp"
#include "report/ReportPorts.hpp"
#include "report/ReportVariables.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"
#include "slang/text/FormatBuffer.h"
#include "slang/util/OS.h"
#include "slang/util/Util.h"
#include "slang/util/VersionInfo.h"

#include "fmt/format.h"

using namespace slang;
using namespace slang::ast;
using namespace slang::driver;
using namespace slang::netlist;

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
    compilation->freeze();

    if (!driver.reportDiagnostics(true)) {
      return 1;
    }

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
        "no action specified; pass --ports, --variables, or --drivers"));
  }
  SLANG_CATCH(const std::exception &e) {
    SLANG_REPORT_EXCEPTION(e, "{}\n");
    return 1;
  }

  return 0;
}
