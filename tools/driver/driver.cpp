#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"
#include "slang/text/FormatBuffer.h"
#include "slang/util/VersionInfo.h"

#include "netlist/NetlistGraph.hpp"
#include "netlist/ProceduralAnalysis.hpp"

using namespace slang;
using namespace slang::ast;
using namespace slang::driver;
using namespace slang::netlist;

struct NetlistVisitor : public ASTVisitor<NetlistVisitor, false, true> {
  Compilation &compilation;
  NetlistGraph &graph;

public:
  explicit NetlistVisitor(ast::Compilation &compilation, NetlistGraph &graph)
      : compilation(compilation), graph(graph) {}

  void handle(const ast::ProceduralBlockSymbol &symbol) {
    fmt::print("ProceduralBlock\n");
    ProceduralAnalysis dfa(symbol, graph);
    dfa.run(symbol.as<ProceduralBlockSymbol>().getBody());
  }
};

void printDOT(const NetlistGraph &netlist, const std::string &fileName) {
  slang::FormatBuffer buffer;
  buffer.append("digraph {\n");
  buffer.append("  node [shape=record];\n");
  for (auto &node : netlist) {
    switch (node->kind) {
    case NodeKind::PortDeclaration: {
      auto &portDecl = node->as<PortDeclaration>();
      //buffer.format("  N{} [label=\"Port declaration\\n{}\"]\n", node->ID,
      //              portDecl.hierarchicalPath);
      break;
    }
    case NodeKind::VariableDeclaration: {
      auto &varDecl = node->as<VariableDeclaration>();
      //buffer.format(
      //    "  N{} [label=\"Variable declaration\\n {}\"]\n",
      //    node->ID, varDecl.hierarchicalPath);
      break;
    }
    case NodeKind::VariableAlias: {
      auto &varAlias = node->as<VariableAlias>();
      //buffer.format("  N{} [label=\"Variable alias\\n{}\"]\n", node->ID,
      //              varAlias.hierarchicalPath);
      break;
    }
    case NodeKind::VariableReference: {
      auto &varRef = node->as<VariableReference>();
      //if (!varRef.isLeftOperand()) {
      //  buffer.format("  N{} [label=\"{}\\n\"]\n", node->ID, varRef.toString());
      //} else if (node->edgeKind == EdgeKind::None) {
      //  buffer.format("  N{} [label=\"{}\\n[Assigned to]\"]\n", node->ID,
      //                varRef.toString());
      //} else {
      //  buffer.format("  N{} [label=\"{}\\n[Assigned to @({})]\"]\n", node->ID,
      //                varRef.toString(),
      //                toString(node->edgeKind));
      //}
      buffer.format("  N{} [label=\"VarRef {}\"]\n", node->ID, varRef.symbol.name);
      break;
    }
    case NodeKind::Assignment: {
      auto &assignment = node->as<Assignment>();
      buffer.format("  N{} [label=\"Assignment\"]\n", node->ID);
      break;
    }
    case NodeKind::Conditional: {
      auto &conditional = node->as<Conditional>();
      buffer.format("  N{} [label=\"Conditional\"]\n", node->ID);
      break;
    }
    case NodeKind::Case: {
      auto &conditional = node->as<Case>();
      buffer.format("  N{} [label=\"Case\"]\n", node->ID);
      break;
    }
    case NodeKind::Join: {
      auto &conditional = node->as<Join>();
      buffer.format("  N{} [label=\"Join\"]\n", node->ID);
      break;
    }
    default:
      SLANG_UNREACHABLE;
    }
  }
  for (auto &node : netlist) {
    for (auto &edge : node->getOutEdges()) {
      if (!edge->disabled) {
        buffer.format("  N{} -> N{}\n", node->ID, edge->getTargetNode().ID);
      }
    }
  }
  buffer.append("}\n");
  OS::writeFile(fileName, buffer.str());
}

int main(int argc, char **argv) {
  Driver driver;
  driver.addStandardArgs();

  std::optional<bool> showHelp;
  std::optional<bool> showVersion;
  std::optional<std::string> netlistDotFile;
  driver.cmdLine.add("-h,--help", showHelp, "Display available options");
  driver.cmdLine.add("--version", showVersion,
                     "Display version information and exit");

  driver.cmdLine.add(
      "--netlist-dot", netlistDotFile,
      "Dump the netlist in DOT format to the specified file, or '-' for stdout",
      "<file>", CommandLineFlags::FilePath);

  if (!driver.parseCommandLine(argc, argv)) {
    return 1;
  }

  if (showHelp == true) {
    printf("%s\n",
           driver.cmdLine.getHelpText("slang SystemVerilog compiler").c_str());
    return 0;
  }

  if (showVersion == true) {
    printf("slang version %d.%d.%d+%s\n", VersionInfo::getMajor(),
           VersionInfo::getMinor(), VersionInfo::getPatch(),
           std::string(VersionInfo::getHash()).c_str());
    return 0;
  }

  if (!driver.processOptions())
    return 2;

  bool ok = driver.parseAllSources();
  auto compilation = driver.createCompilation();
  driver.reportCompilation(*compilation, true);
  driver.runAnalysis(*compilation);
  ok |= driver.reportDiagnostics(true);

  NetlistGraph graph;

  NetlistVisitor visitor(*compilation, graph);
  compilation->getRoot().visit(visitor);

  // Output a DOT file of the netlist.
  if (netlistDotFile) {
    printDOT(graph, *netlistDotFile);
    return 0;
  }

  return ok ? 0 : 3;
}
