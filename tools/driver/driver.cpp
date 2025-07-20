#include "slang/driver/Driver.h"
#include "slang/ast/Compilation.h"
#include "slang/text/FormatBuffer.h"
#include "slang/text/Json.h"
#include "slang/util/VersionInfo.h"

#include "netlist/NetlistDot.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/NetlistVisitor.hpp"

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

    NetlistGraph graph;
    NetlistVisitor visitor(*compilation, *analysisManager, graph);
    compilation->getRoot().visit(visitor);
    graph.processPendingRvalues();

    DEBUG_PRINT("Netlist has {} nodes and {} edges\n", graph.numNodes(),
                graph.numEdges());

    // Output a DOT file of the netlist.
    if (netlistDotFile) {
      FormatBuffer buffer;
      NetlistDot::render(graph, buffer);
      OS::writeFile(*netlistDotFile, buffer.str());
      return 0;
    }

    // No action performed.
    return 1;
  }
  SLANG_CATCH(const std::exception &e) {
    SLANG_REPORT_EXCEPTION(e, "{}\n");
    return 1;
  }

  return 0;
}
