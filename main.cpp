#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"
#include "slang/util/VersionInfo.h"
#include "slang/analysis/AbstractFlowAnalysis.h"

using namespace slang;
using namespace slang::analysis;
using namespace slang::ast;
using namespace slang::driver;

struct SLANG_EXPORT NetlistState {
};

struct NetlistAnalysis : public AbstractFlowAnalysis<NetlistAnalysis, int> {
 
  NetlistAnalysis(const Symbol& symbol) : AbstractFlowAnalysis(symbol, {}) {}
    
  void handle(const NamedValueExpression& expr) {
      getState() += 1;
      fmt::print("NamedValueExpression\n");
      visitExpr(expr);
  }

  void joinState(int& state, const int& other) const { state += other; }
  void meetState(int& state, const int& other) const { state = std::min(state, other); }
  int copyState(const int& state) const { return state; }
  int unreachableState() const { return 0; }
  int topState() const { return 0; }

  int getCurrentState() const { return getState(); }
};


struct NetlistVisitor : public ASTVisitor<NetlistVisitor, false, true> {
  Compilation &compilation;

public:
    explicit NetlistVisitor(ast::Compilation& compilation) :
        compilation(compilation) {}
    
    void handle(const ast::ProceduralBlockSymbol& symbol) {
      fmt::print("ProceduralBlock\n"); 
      NetlistAnalysis dfa(symbol);
      dfa.run(symbol.as<ProceduralBlockSymbol>().getBody());
    }
};


int main(int argc, char** argv) {
    Driver driver;
    driver.addStandardArgs();

    std::optional<bool> showHelp;
    std::optional<bool> showVersion;
    driver.cmdLine.add("-h,--help", showHelp, "Display available options");
    driver.cmdLine.add("--version", showVersion, "Display version information and exit");

    if (!driver.parseCommandLine(argc, argv))
        return 1;

    if (showHelp == true) {
        printf("%s\n", driver.cmdLine.getHelpText("slang SystemVerilog compiler").c_str());
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

    NetlistVisitor visitor(*compilation);
    compilation->getRoot().visit(visitor);

    return ok ? 0 : 3;
}
