#include "slang/driver/Driver.h"

#include "fmt/color.h"
#include "fmt/format.h"
#include "netlist/NetlistDiagnostics.hpp"
#include "netlist/NetlistDot.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/NetlistVisitor.hpp"
#include "netlist/PathFinder.hpp"
#include "netlist/SymbolVisitor.hpp"

#include "slang/ast/Compilation.h"
#include "slang/text/FormatBuffer.h"
#include "slang/text/Json.h"
#include "slang/util/Util.h"
#include "slang/util/VersionInfo.h"

using namespace slang;
using namespace slang::ast;
using namespace slang::driver;
using namespace slang::netlist;

template <> class fmt::formatter<ConstantRange> {
public:
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
  template <typename Context>
  constexpr auto format(ConstantRange const &range, Context &ctx) const {
    return format_to(ctx.out(), "[{}:{}]", range.upper(), range.lower());
  }
};

auto generateJson(Compilation &compilation, JsonWriter &writer,
                  const std::vector<std::string> &scopes) {
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
}

void reportNode(NetlistDiagnostics &diagnostics, NetlistNode const &node) {
  switch (node.kind) {
  case NodeKind::Port: {
    auto &port = node.as<Port>();
    SLANG_ASSERT(port.internalSymbol);

    if (port.isInput()) {
      Diagnostic diagnostic(diag::InputPort, port.internalSymbol->location);
      diagnostic << port.internalSymbol->name;
      diagnostics.issue(diagnostic);
    } else if (port.isOutput()) {
      Diagnostic diagnostic(diag::OutputPort, port.internalSymbol->location);
      diagnostic << port.internalSymbol->name;
      diagnostics.issue(diagnostic);
    } else {
      SLANG_ASSERT(false && "unhandled port type");
    }
    break;
  }
  case NodeKind::Assignment: {
    auto &assignment = node.as<Assignment>();
    Diagnostic diagnostic(diag::Assignment,
                          assignment.expr.sourceRange.start());
    diagnostics.issue(diagnostic);
    break;
  }
  case NodeKind::Conditional: {
    auto &conditional = node.as<Conditional>();
    Diagnostic diagnostic(diag::Conditional,
                          conditional.stmt.sourceRange.start());
    diagnostics.issue(diagnostic);
    break;
  }
  case NodeKind::Case: {
    auto &conditional = node.as<Case>();
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
  if (edge.symbol) {
    Diagnostic diagnostic(diag::Value, edge.symbol->location);
    diagnostic << fmt::format("{}{}", edge.symbol->getHierarchicalPath(),
                              ConstantRange(edge.bounds));
    diagnostics.issue(diagnostic);
  }
}

/// Report a path in the netlist.
void reportPath(NetlistDiagnostics &diagnostics, const NetlistPath &path) {

  // Loop through the path and retrieve the edge between consective pairs of
  // nodes. Report each node and edge using slang's diagnostic engine.
  for (size_t i = 0; i < path.size() - 1; ++i) {
    auto *nodeA = path[i];
    auto *nodeB = path[i + 1];
    auto edgeIt = nodeA->findEdgeTo(*nodeB);
    SLANG_ASSERT(edgeIt != nodeA->end() &&
                 "edge between nodes not found in path");

    reportNode(diagnostics, *nodeA);
    reportEdge(diagnostics, **edgeIt);
  }

  reportNode(diagnostics, *path.back());
}

int main(int argc, char **argv) {
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

  std::optional<std::string> reportSymbols;
  driver.cmdLine.add("--report-symbols", reportSymbols,
                     "Report all symbols in the compilation to the specified "
                     "file or '-' for stdout",
                     "<file>", CommandLineFlags::FilePath);

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

    if (reportSymbols) {
      FormatBuffer buf;
      SymbolVisitor visitor(*compilation, buf);
      compilation->getRoot().visit(visitor);
      OS::writeFile(*reportSymbols, buf.str());
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
      return ok;
    }

    NetlistGraph netlist;
    NetlistVisitor visitor(*compilation, *analysisManager, netlist);
    compilation->getRoot().visit(visitor);
    netlist.finalize();

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
      auto *fromPoint = netlist.lookup(*fromPointName);
      if (fromPoint == nullptr) {
        SLANG_THROW(std::runtime_error(
            fmt::format("could not find start point: {}", *fromPointName)));
      }
      auto *toPoint = netlist.lookup(*toPointName);
      if (toPoint == nullptr) {
        SLANG_THROW(std::runtime_error(
            fmt::format("could not find finish point: {}", *toPointName)));
      }

      DEBUG_PRINT("Searching for path between: {} and {}\n", *fromPointName,
                  *toPointName);

      // Search for the path.
      PathFinder pathFinder(netlist);
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
