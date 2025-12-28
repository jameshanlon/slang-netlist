#include "slang/driver/Driver.h"

#include "netlist/CombLoops.hpp"
#include "netlist/Debug.hpp"
#include "netlist/NetlistBuilder.hpp"
#include "netlist/NetlistDiagnostics.hpp"
#include "netlist/NetlistDot.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/PathFinder.hpp"
#include "netlist/ReportDrivers.hpp"
#include "netlist/ReportPorts.hpp"
#include "netlist/ReportVariables.hpp"
#include "netlist/Utilities.hpp"

#include "slang/ast/Compilation.h"
#include "slang/diagnostics/Diagnostics.h"
#include "slang/numeric/ConstantValue.h"
#include "slang/text/FormatBuffer.h"
#include "slang/text/Json.h"
#include "slang/util/Util.h"
#include "slang/util/VersionInfo.h"

#include "fmt/color.h"
#include "fmt/format.h"
#include <vector>

using namespace slang;
using namespace slang::ast;
using namespace slang::driver;
using namespace slang::netlist;

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
      if (sym == nullptr) {
        serializer.serialize(*sym);
      }
    }
  }
}

void reportNode(NetlistDiagnostics &diagnostics, NetlistNode const &node) {
  switch (node.kind) {
  case NodeKind::Port: {
    auto const &port = node.as<Port>();
    SLANG_ASSERT(port.symbol.internalSymbol);

    if (port.isInput()) {
      Diagnostic diagnostic(diag::InputPort,
                            port.symbol.internalSymbol->location);
      diagnostic << port.symbol.internalSymbol->name;
      diagnostics.issue(diagnostic);
    } else if (port.isOutput()) {
      Diagnostic diagnostic(diag::OutputPort,
                            port.symbol.internalSymbol->location);
      diagnostic << port.symbol.internalSymbol->name;
      diagnostics.issue(diagnostic);
    } else {
      SLANG_ASSERT(false && "unhandled port type");
    }
    break;
  }
  case NodeKind::Assignment: {
    auto const &assignment = node.as<Assignment>();
    Diagnostic diagnostic(diag::Assignment,
                          assignment.expr.sourceRange.start());
    diagnostics.issue(diagnostic);
    break;
  }
  case NodeKind::Conditional: {
    auto const &conditional = node.as<Conditional>();
    Diagnostic diagnostic(diag::Conditional,
                          conditional.stmt.sourceRange.start());
    diagnostics.issue(diagnostic);
    break;
  }
  case NodeKind::Case: {
    auto const &conditional = node.as<Case>();
    Diagnostic diagnostic(diag::Case, conditional.stmt.sourceRange.start());
    diagnostics.issue(diagnostic);
    break;
  }
  case NodeKind::Merge: {
    // Ignore merge nodes.
    break;
  }
  default:
    break;
  }
}

void reportEdge(NetlistDiagnostics &diagnostics, NetlistEdge &edge) {
  if (edge.symbol != nullptr) {
    Diagnostic diagnostic(diag::Value, edge.symbol->location);
    diagnostic << fmt::format("{}{}", edge.symbol->getHierarchicalPath(),
                              toString(edge.bounds));
    diagnostics.issue(diagnostic);
  }
}

/// Report a path in the netlist.
void reportPath(NetlistDiagnostics &diagnostics, const NetlistPath &path) {

  // Loop through the path and retrieve the edge between consecutive pairs of
  // nodes. Report each node and edge using slang's diagnostic engine.
  for (size_t i = 0; i < path.size() - 1; ++i) {
    auto const *nodeA = path[i];
    auto const *nodeB = path[i + 1];
    auto edgeIt = nodeA->findEdgeTo(*nodeB);
    SLANG_ASSERT(edgeIt != nodeA->end() &&
                 "edge between nodes not found in path");

    reportNode(diagnostics, *nodeA);
    reportEdge(diagnostics, **edgeIt);
  }

  reportNode(diagnostics, *path.back());
}

}; // namespace

auto main(int argc, char **argv) -> int {
  OS::setupConsole();

  Driver driver;
  driver.addStandardArgs();

  std::optional<bool> showHelp;
  driver.cmdLine.add("-h,--help", showHelp, "Display available options");

  std::optional<bool> showVersion;
  driver.cmdLine.add("--version", showVersion,
                     "Display version information and exit");

  std::optional<bool> noColours;
  driver.cmdLine.add("--no-colours", noColours,
                     "Disable colored output (default is enabled on terminals "
                     "that support it)");

  std::optional<bool> quiet;
  driver.cmdLine.add("-q,--quiet", quiet, "Suppress non-essential output");

  std::optional<bool> debug;
  driver.cmdLine.add("-d,--debug", debug, "Output debugging information");

  std::optional<bool> reportVariables;
  driver.cmdLine.add("--report-variables", reportVariables,
                     "Report all variables in the design to stdout");

  std::optional<bool> reportPorts;
  driver.cmdLine.add("--report-ports", reportPorts,
                     "Report all ports in the design to stdout");

  std::optional<bool> reportDrivers;
  driver.cmdLine.add("--report-drivers", reportDrivers,
                     "Report all drivers in the design stdout");

  std::optional<bool> reportRegisters;
  driver.cmdLine.add("--report-registers", reportRegisters,
                     "Report all registers in the design to stdout");

  std::optional<bool> combLoops;
  driver.cmdLine.add("--comb-loops", combLoops,
                     "Report any combinational loops in the design to stdout");

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

  std::optional<std::string> netlistDotFile;
  driver.cmdLine.add("--netlist-dot", netlistDotFile,
                     "Dump the netlist in DOT format to the specified file, "
                     "or '-' for stdout",
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
    printf("slang-netlist version %d.%d.%d+%s\n", VersionInfo::getMajor(),
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

    // Force construction of the whole AST.
    VisitAll va;
    compilation->getRoot().visit(va);

    // Freeze the compilation for subsequent multithreaded analysis.
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

    if (astJsonFile) {
      JsonWriter writer;
      generateJson(*compilation, writer, astJsonScopes);
      OS::writeFile(*astJsonFile, writer.view());
      return 0;
    }

    auto analysisManager = driver.runAnalysis(*compilation);
    ok |= driver.reportDiagnostics(true);

    if (!ok) {
      return (int)ok;
    }

    if (reportDrivers) {
      FormatBuffer buf;
      ReportDrivers visitor(*compilation, *analysisManager);
      compilation->getRoot().visit(visitor);
      visitor.report(buf);
      OS::print(buf.str());
      return 0;
    }

    NetlistGraph graph;
    NetlistBuilder builder(*compilation, *analysisManager, graph);
    compilation->getRoot().visit(builder);
    builder.finalize();

    DEBUG_PRINT("Netlist has {} nodes and {} edges\n", graph.numNodes(),
                graph.numEdges());

    if (reportRegisters) {
      auto header = Utilities::Row{"Name", "Location"};
      auto table = Utilities::Table{};

      for (auto &node : graph.filterNodes(NodeKind::State)) {
        auto const &stateNode = node->as<State>();
        auto loc =
            Utilities::locationStr(*compilation, stateNode.symbol.location);
        table.push_back(
            Utilities::Row{stateNode.symbol.getHierarchicalPath(), loc});
      }

      FormatBuffer buffer;
      Utilities::formatTable(buffer, header, table);
      OS::print(buffer.str());
      return 0;
    }

    // Report combinational loops.
    if (combLoops) {
      CombLoops combLoops(graph);
      auto cycles = combLoops.getAllLoops();
      if (cycles.empty()) {
        OS::print("No combinational loops found in the design.\n");
      } else {
        NetlistDiagnostics diagnostics(*compilation, !noColours);
        for (auto const &cycle : cycles) {
          OS::print("Combinational loop detected:\n\n");
          reportPath(diagnostics, cycle);
          OS::print(fmt::format("{}\n", diagnostics.getString()));
          diagnostics.clear();
        }
      }
      return 0;
    }

    // Output a DOT file of the netlist.
    if (netlistDotFile) {
      FormatBuffer buffer;
      NetlistDot::render(graph, buffer);
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
      auto *fromPoint = graph.lookup(*fromPointName);
      if (fromPoint == nullptr) {
        SLANG_THROW(std::runtime_error(
            fmt::format("could not find start point: {}", *fromPointName)));
      }
      auto *toPoint = graph.lookup(*toPointName);
      if (toPoint == nullptr) {
        SLANG_THROW(std::runtime_error(
            fmt::format("could not find finish point: {}", *toPointName)));
      }

      DEBUG_PRINT("Searching for path between: {} and {}\n", *fromPointName,
                  *toPointName);

      // Search for the path.
      PathFinder pathFinder(builder);
      auto path = pathFinder.find(*fromPoint, *toPoint);

      if (!path.empty()) {
        // Report the path and exit.
        NetlistDiagnostics diagnostics(*compilation, !noColours);
        reportPath(diagnostics, path);
        OS::print(fmt::format("{}\n", diagnostics.getString()));
        diagnostics.clear();
        return 0;
      }

      // No path found.
      SLANG_THROW(std::runtime_error(fmt::format(
          "no path between {} and {}", *fromPointName, *toPointName)));
    }

    // If we reach here, no action was specified.
    SLANG_THROW(std::runtime_error("no action specified"));
  }
  SLANG_CATCH(const std::exception &e) {
    SLANG_REPORT_EXCEPTION(e, "{}\n");
    return 1;
  }

  return 0;
}
