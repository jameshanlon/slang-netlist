#include "slang/driver/Driver.h"

#include "netlist/CombLoops.hpp"
#include "netlist/Debug.hpp"
#include "netlist/NetlistBuilder.hpp"
#include "netlist/NetlistDiagnostics.hpp"
#include "netlist/NetlistDot.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/NetlistSerializer.hpp"
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

/// Get the TextLocation for a node, if it has one.
auto getNodeLocation(NetlistNode const &node) -> std::optional<TextLocation> {
  switch (node.kind) {
  case NodeKind::Port:
    return node.as<Port>().location;
  case NodeKind::Assignment:
    return node.as<Assignment>().location;
  case NodeKind::Conditional:
    return node.as<Conditional>().location;
  case NodeKind::Case:
    return node.as<Case>().location;
  default:
    return std::nullopt;
  }
}

void reportNodeDiag(NetlistDiagnostics &diagnostics, NetlistNode const &node) {
  switch (node.kind) {
  case NodeKind::Port: {
    auto const &port = node.as<Port>();
    auto srcLoc = port.location.sourceLocation;
    if (port.isInput()) {
      Diagnostic diagnostic(diag::InputPort, srcLoc);
      diagnostic << port.name;
      diagnostics.issue(diagnostic);
    } else if (port.isOutput()) {
      Diagnostic diagnostic(diag::OutputPort, srcLoc);
      diagnostic << port.name;
      diagnostics.issue(diagnostic);
    } else {
      SLANG_UNREACHABLE;
    }
    break;
  }
  case NodeKind::Assignment: {
    auto const &assignment = node.as<Assignment>();
    Diagnostic diagnostic(diag::Assignment, assignment.location.sourceLocation);
    diagnostics.issue(diagnostic);
    break;
  }
  case NodeKind::Conditional: {
    auto const &conditional = node.as<Conditional>();
    Diagnostic diagnostic(diag::Conditional,
                          conditional.location.sourceLocation);
    diagnostics.issue(diagnostic);
    break;
  }
  case NodeKind::Case: {
    auto const &caseNode = node.as<Case>();
    Diagnostic diagnostic(diag::Case, caseNode.location.sourceLocation);
    diagnostics.issue(diagnostic);
    break;
  }
  case NodeKind::Merge:
    break;
  default:
    break;
  }
}

void reportEdgeDiag(NetlistDiagnostics &diagnostics, NetlistEdge &edge) {
  if (!edge.symbol.empty()) {
    Diagnostic diagnostic(diag::Value, edge.symbol.location.sourceLocation);
    diagnostic << fmt::format("{}{}", edge.symbol.hierarchicalPath,
                              toString(edge.bounds));
    diagnostics.issue(diagnostic);
  }
}

void reportNodeText(FormatBuffer &buffer, FileTable const &fileTable,
                    NetlistNode const &node) {
  switch (node.kind) {
  case NodeKind::Port: {
    auto const &port = node.as<Port>();
    auto loc = port.location.toString(fileTable);
    if (port.isInput()) {
      buffer.format("{}: note: input port {}\n", loc, port.name);
    } else if (port.isOutput()) {
      buffer.format("{}: note: output port {}\n", loc, port.name);
    } else {
      SLANG_UNREACHABLE;
    }
    break;
  }
  case NodeKind::Assignment: {
    auto const &assignment = node.as<Assignment>();
    buffer.format("{}: note: assignment\n",
                  assignment.location.toString(fileTable));
    break;
  }
  case NodeKind::Conditional: {
    auto const &conditional = node.as<Conditional>();
    buffer.format("{}: note: conditional statement\n",
                  conditional.location.toString(fileTable));
    break;
  }
  case NodeKind::Case: {
    auto const &caseNode = node.as<Case>();
    buffer.format("{}: note: case statement\n",
                  caseNode.location.toString(fileTable));
    break;
  }
  case NodeKind::Merge:
    break;
  default:
    break;
  }
}

void reportEdgeText(FormatBuffer &buffer, FileTable const &fileTable,
                    NetlistEdge &edge) {
  if (!edge.symbol.empty()) {
    buffer.format("{}: note: value {}{}\n",
                  edge.symbol.location.toString(fileTable),
                  edge.symbol.hierarchicalPath, toString(edge.bounds));
  }
}

/// Report a path using diagnostics (with source lines and carets)
/// when source locations are available, otherwise fall back to
/// plain text output.
auto reportPath(FileTable const &fileTable, NetlistDiagnostics *diagnostics,
                const NetlistPath &path) -> std::string {

  // Check if the first located node has a source location to
  // decide which reporting mode to use.
  bool useDiag = false;
  if (diagnostics) {
    for (auto const *node : path) {
      if (auto loc = getNodeLocation(*node)) {
        useDiag = loc->hasSourceLocation();
        break;
      }
    }
  }

  if (useDiag) {
    for (size_t i = 0; i < path.size() - 1; ++i) {
      auto const *nodeA = path[i];
      auto const *nodeB = path[i + 1];
      auto edgeIt = nodeA->findEdgeTo(*nodeB);
      SLANG_ASSERT(edgeIt != nodeA->end() &&
                   "edge between nodes not found in path");
      reportNodeDiag(*diagnostics, *nodeA);
      reportEdgeDiag(*diagnostics, **edgeIt);
    }
    reportNodeDiag(*diagnostics, *path.back());
    auto result = diagnostics->getString();
    diagnostics->clear();
    return std::string(result);
  }

  FormatBuffer buffer;
  for (size_t i = 0; i < path.size() - 1; ++i) {
    auto const *nodeA = path[i];
    auto const *nodeB = path[i + 1];
    auto edgeIt = nodeA->findEdgeTo(*nodeB);
    SLANG_ASSERT(edgeIt != nodeA->end() &&
                 "edge between nodes not found in path");
    reportNodeText(buffer, fileTable, *nodeA);
    reportEdgeText(buffer, fileTable, **edgeIt);
  }
  reportNodeText(buffer, fileTable, *path.back());
  return buffer.str();
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

  std::optional<std::string> saveNetlistFile;
  driver.cmdLine.add("--save-netlist", saveNetlistFile,
                     "Save the netlist to a JSON file", "<file>",
                     CommandLineFlags::FilePath);

  std::optional<std::string> loadNetlistFile;
  driver.cmdLine.add("--load-netlist", loadNetlistFile,
                     "Load a netlist from a JSON file (skips compilation)",
                     "<file>", CommandLineFlags::FilePath);

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

  if (debug) {
    Config::getInstance().debugEnabled = true;
  }

  if (quiet) {
    Config::getInstance().quietEnabled = true;
  }

  SLANG_TRY {

    NetlistGraph graph;
    std::unique_ptr<Compilation> compilation;
    std::unique_ptr<NetlistDiagnostics> diagnostics;

    if (loadNetlistFile) {
      // Load a previously-saved netlist (skips compilation).
      SmallVector<char> fileContent;
      auto ec = OS::readFile(*loadNetlistFile, fileContent);
      if (ec) {
        SLANG_THROW(std::runtime_error(
            fmt::format("could not read file: {}", *loadNetlistFile)));
      }
      NetlistSerializer::deserialize(
          std::string_view(fileContent.data(), fileContent.size()), graph);

      DEBUG_PRINT("Loaded netlist has {} nodes and {} edges\n",
                  graph.numNodes(), graph.numEdges());
    } else {
      // Build a netlist from source files.
      if (!driver.processOptions()) {
        return 2;
      }

      if (!driver.parseAllSources()) {
        return 1;
      }
      compilation = driver.createCompilation();
      driver.reportCompilation(*compilation, true);
      if (!driver.reportDiagnostics(true)) {
        return 1;
      }

      // Force construction of the whole AST.
      VisitAll va;
      compilation->getRoot().visit(va);

      // Freeze the compilation for subsequent multithreaded analysis.
      compilation->freeze();

      // These reports require the live AST and cannot work on a loaded
      // netlist.
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
      if (!driver.reportDiagnostics(true)) {
        return 1;
      }

      if (reportDrivers) {
        FormatBuffer buf;
        ReportDrivers visitor(*compilation, *analysisManager);
        compilation->getRoot().visit(visitor);
        visitor.report(buf);
        OS::print(buf.str());
        return 0;
      }

      NetlistBuilder builder(*compilation, *analysisManager, graph);
      builder.build(compilation->getRoot(), /*parallel=*/true,
                    driver.options.numThreads.value_or(0));
      builder.finalize();

      DEBUG_PRINT("Netlist has {} nodes and {} edges\n", graph.numNodes(),
                  graph.numEdges());

      if (saveNetlistFile) {
        auto json = NetlistSerializer::serialize(graph);
        OS::writeFile(*saveNetlistFile, json);
        return 0;
      }

      diagnostics =
          std::make_unique<NetlistDiagnostics>(*compilation, !noColours);
    }

    // --- Analysis commands that work on both built and loaded netlists ---

    if (reportRegisters) {
      auto header = Utilities::Row{"Name", "Location"};
      auto table = Utilities::Table{};

      for (auto const &node : graph.filterNodes(NodeKind::State)) {
        auto const &stateNode = node->as<State>();
        auto loc = stateNode.location.toString(graph.fileTable);
        table.push_back(Utilities::Row{stateNode.hierarchicalPath, loc});
      }

      FormatBuffer buffer;
      Utilities::formatTable(buffer, header, table);
      OS::print(buffer.str());
      return 0;
    }

    // Report combinational loops.
    if (combLoops) {
      CombLoops combLoopsAnalysis(graph);
      auto cycles = combLoopsAnalysis.getAllLoops();
      if (cycles.empty()) {
        OS::print("No combinational loops detected in the design.\n");
      } else {
        for (auto const &cycle : cycles) {
          OS::print("Combinational loop detected:\n\n");
          auto result = reportPath(graph.fileTable, diagnostics.get(), cycle);
          OS::print(fmt::format("{}\n", result));
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
      PathFinder pathFinder;
      auto path = pathFinder.find(*fromPoint, *toPoint);

      if (!path.empty()) {
        auto result = reportPath(graph.fileTable, diagnostics.get(), path);
        OS::print(fmt::format("{}\n", result));
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
