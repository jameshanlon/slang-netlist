#include "slang/driver/Driver.h"
#include "slang/ast/Compilation.h"
#include "slang/text/FormatBuffer.h"
#include "slang/text/Json.h"
#include "slang/util/VersionInfo.h"

#include "netlist/NetlistDot.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/NetlistVisitor.hpp"
#include "netlist/PathFinder.hpp"

using namespace slang;
using namespace slang::ast;
using namespace slang::driver;
using namespace slang::netlist;

void printJson(Compilation &compilation, const std::string &fileName,
               const std::vector<std::string> &scopes) {
  JsonWriter writer;
  writer.setPrettyPrint(true);
  ASTSerializer serializer(compilation, writer);
  if (scopes.empty()) {
    serializer.serialize(compilation.getRoot());
  } else {
    for (auto &scopeName : scopes) {
      auto sym = compilation.getRoot().lookupName(scopeName);
      if (sym) {
        serializer.serialize(*sym);
      }
    }
  }
  OS::writeFile(fileName, writer.view());
}

void reportPath(Compilation &compilation, const NetlistPath &path) {
  DiagnosticEngine diagEngine(*compilation.getSourceManager());
  diagEngine.setMessage(diag::VariableReference, "variable {}");
  diagEngine.setSeverity(diag::VariableReference, DiagnosticSeverity::Note);
  auto textDiagClient = std::make_shared<TextDiagnosticClient>();
  textDiagClient->showColors(true);
  textDiagClient->showLocation(true);
  textDiagClient->showSourceLine(true);
  textDiagClient->showHierarchyInstance(ShowHierarchyPathOption::Always);
  diagEngine.addClient(textDiagClient);
  for (auto *node : path) {
    auto *SM = compilation.getSourceManager();
    auto &location = node->symbol.location;
    auto bufferID = location.buffer();
    if (node->kind != NodeKind::VariableReference) {
      continue;
    }
    const auto &varRefNode = node->as<NetlistVariableReference>();
    Diagnostic diagnostic(diag::VariableReference,
                          varRefNode.expression.sourceRange.start());
    diagnostic << varRefNode.expression.sourceRange;
    if (varRefNode.isLeftOperand()) {
      diagnostic << fmt::format("{} assigned to", varRefNode.getName());
    } else {
      diagnostic << fmt::format("{} read from", varRefNode.getName());
    }
    diagEngine.issue(diagnostic);
    OS::print(fmt::format("{}\n", textDiagClient->getString()));
    textDiagClient->clear();
  }
}

int main(int argc, char **argv) {
  OS::setupConsole();

  Driver driver;
  driver.addStandardArgs();

  std::optional<bool> showHelp;
  std::optional<bool> showVersion;
  std::optional<bool> quiet;
  std::optional<bool> debug;
  driver.cmdLine.add("-h,--help", showHelp, "Display available options");
  driver.cmdLine.add("--version", showVersion,
                     "Display version information and exit");
  driver.cmdLine.add("-q,--quiet", quiet, "Suppress non-essential output");
  driver.cmdLine.add("-d,--debug", debug, "Output debugging information");

  std::optional<std::string> astJsonFile;
  driver.cmdLine.add("--ast-json", astJsonFile,
                     "Dump the compiled AST in JSON format to the specified "
                     "file, or '-' for stdout",
                     "<file>", CommandLineFlags::FilePath);

  std::vector<std::string> astJsonScopes;
  driver.cmdLine.add(
      "--ast-json-scope", astJsonScopes,
      "When dumping AST to JSON, include only the scopes specified by the "
      "given hierarchical paths",
      "<path>");

  std::optional<std::string> netlistDotFile;
  driver.cmdLine.add(
      "--netlist-dot", netlistDotFile,
      "Dump the netlist in DOT format to the specified file, or '-' for stdout",
      "<file>", CommandLineFlags::FilePath);

  std::optional<std::string> fromPointName;
  driver.cmdLine.add("--from", fromPointName,
                     "Specify a start point from which to trace a path",
                     "<name>");

  std::optional<std::string> toPointName;
  driver.cmdLine.add("--to", toPointName,
                     "Specify a finish point to trace a path to", "<name>");

  if (!driver.parseCommandLine(argc, argv)) {
    return 1;
  }

  if (showHelp == true) {
    std::cout << fmt::format(
        "{}\n",
        driver.cmdLine.getHelpText("slang SystemVerilog netlist tool").c_str());
    return 0;
  }

  if (showVersion == true) {
    printf("slang version %d.%d.%d+%s\n", VersionInfo::getMajor(),
           VersionInfo::getMinor(), VersionInfo::getPatch(),
           std::string(VersionInfo::getHash()).c_str());
    return 0;
  }

  if (!driver.processOptions()) {
    return 2;
  }

  if (debug) {
    Config::getInstance().debugEnabled = true;
  }

  if (quiet) {
    Config::getInstance().quietEnabled = true;
  }

  SLANG_TRY {

    bool ok = driver.parseAllSources();
    auto compilation = driver.createCompilation();
    driver.reportCompilation(*compilation, true);
    ok |= driver.reportDiagnostics(true);

    if (astJsonFile) {
      printJson(*compilation, *astJsonFile, astJsonScopes);
      return 0;
    }

    auto analysisManager = driver.runAnalysis(*compilation);
    ok |= driver.reportDiagnostics(true);

    if (!ok) {
      return ok;
    }

    NetlistGraph netlist;
    NetlistVisitor visitor(*compilation, *analysisManager, netlist);
    compilation->getRoot().visit(visitor);
    graph.finalize();

    DEBUG_PRINT("Netlist has {} nodes and {} edges\n", netlist.numNodes(),
                netlist.numEdges());

    // Output a DOT file of the netlist.
    if (netlistDotFile) {
      FormatBuffer buffer;
      NetlistDot::render(netlist, buffer);
      OS::writeFile(*netlistDotFile, buffer.str());
      return 0;
    }

    // Find a point-to-point path in the netlist.
    if (fromPointName.has_value() && toPointName.has_value()) {
      if (!fromPointName.has_value()) {
        SLANG_THROW(std::runtime_error(
            "please specify a start point using --from <name>"));
      }
      if (!toPointName.has_value()) {
        SLANG_THROW(std::runtime_error(
            "please specify a finish point using --to <name>"));
      }
      auto fromPoint = netlist.lookup(*fromPointName);
      if (fromPoint == nullptr) {
        SLANG_THROW(std::runtime_error(
            fmt::format("could not find start point: {}", *fromPointName)));
      }
      auto toPoint = netlist.lookup(*toPointName);
      if (toPoint == nullptr) {
        SLANG_THROW(std::runtime_error(
            fmt::format("could not find finish point: {}", *toPointName)));
      }

      DEBUG_PRINT("Searching for path between: {} and {}\n", *fromPoint,
                  *toPoint);

      // Search for the path.
      PathFinder pathFinder(netlist);
      auto path = pathFinder.find(*fromPoint, *toPoint);

      if (!path.empty()) {
        // Report the path and exit.
        reportPath(*compilation, path);
        return 0;
      }
    }

    // No path found.
    SLANG_THROW(std::runtime_error(fmt::format("no path between {} and {}",
                                               *fromPointName, *toPointName)));
  }

  // No action performed.
  SLANG_THROW(std::runtime_error("no action specified"));
}
SLANG_CATCH(const std::exception &e) {
  SLANG_REPORT_EXCEPTION(e, "{}\n");
  return 1;
}

return 0;
}
