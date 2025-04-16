#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"
#include "slang/util/VersionInfo.h"
#include "slang/analysis/AbstractFlowAnalysis.h"
#include "slang/util/IntervalMap.h"
#include "slang/util/BumpAllocator.h"

using namespace slang;
using namespace slang::analysis;
using namespace slang::ast;
using namespace slang::driver;


template<typename T>
concept IsSelectExpr =
    IsAnyOf<T, ElementSelectExpression, RangeSelectExpression, MemberAccessExpression,
            HierarchicalValueExpression, NamedValueExpression>;

// Map assigned ranges to graph nodes.
using SymbolNodeMap = IntervalMap<uint64_t, const Expression*>;


// A helper class that finds the longest static prefix of select expressions.
template<typename TOwner>
struct LSPVisitor {
    TOwner& owner;
    const Expression* currentLSP = nullptr;

    explicit LSPVisitor(TOwner& owner) : owner(owner) {}

    void clear() { currentLSP = nullptr; }

    void handle(const ElementSelectExpression& expr) {
        if (expr.isConstantSelect(owner.getEvalContext())) {
            if (!currentLSP)
                currentLSP = &expr;
        }
        else {
            currentLSP = nullptr;
        }

        owner.visit(expr.value());

        [[maybe_unused]] auto guard = owner.saveLValueFlag();
        owner.visit(expr.selector());
    }

    void handle(const RangeSelectExpression& expr) {
        if (expr.isConstantSelect(owner.getEvalContext())) {
            if (!currentLSP)
                currentLSP = &expr;
        }
        else {
            currentLSP = nullptr;
        }

        owner.visit(expr.value());

        [[maybe_unused]] auto guard = owner.saveLValueFlag();
        owner.visit(expr.left());
        owner.visit(expr.right());
    }

    void handle(const MemberAccessExpression& expr) {
        // If this is a selection of a class or covergroup member,
        // the lsp depends only on the selected member and not on
        // the handle itself. Otherwise, the opposite is true.
        auto& valueType = expr.value().type->getCanonicalType();
        if (valueType.isClass() || valueType.isCovergroup() || valueType.isVoid()) {
            auto lsp = std::exchange(currentLSP, nullptr);
            if (!lsp)
                lsp = &expr;

            if (VariableSymbol::isKind(expr.member.kind))
                owner.noteReference(expr.member.as<VariableSymbol>(), *lsp);

            // Make sure the value gets visited but not as an lvalue anymore.
            [[maybe_unused]] auto guard = owner.saveLValueFlag();
            owner.visit(expr.value());
        }
        else {
            if (!currentLSP)
                currentLSP = &expr;

            owner.visit(expr.value());
        }
    }

    void handle(const HierarchicalValueExpression& expr) {
        auto lsp = std::exchange(currentLSP, nullptr);
        if (!lsp)
            lsp = &expr;

        owner.noteReference(expr.symbol, *lsp);
    }

    void handle(const NamedValueExpression& expr) {
        auto lsp = std::exchange(currentLSP, nullptr);
        if (!lsp)
            lsp = &expr;

        owner.noteReference(expr.symbol, *lsp);
        fmt::print("NamedValueExpression\n");
    }
};


struct SLANG_EXPORT NetlistState {
    
  /// Each tracked variable has its assigned intervals stored here.
  SmallVector<SymbolNodeMap, 2> assigned;

  /// Whether the control flow that arrived at this point is reachable.
  bool reachable = true;

  NetlistState() = default;
  NetlistState(NetlistState&& other) = default;
  NetlistState& operator=(NetlistState&& other) = default;
};


struct NetlistAnalysis : public AbstractFlowAnalysis<NetlistAnalysis, NetlistState> {
    
  friend class AbstractFlowAnalysis;

  template<typename TOwner>
  friend struct LSPVisitor;

  BumpAllocator allocator;
  SymbolNodeMap::allocator_type bitMapAllocator;
    
  // Maps visited symbols to slots in assigned vectors.
  SmallMap<const ValueSymbol*, uint32_t, 4> symbolToSlot;

  // Tracks the assigned ranges of each variable across the entire procedure,
  // even if not all branches assign to it.
  struct LValueSymbol {
    not_null<const ValueSymbol*> symbol;
    SymbolNodeMap assigned;

    LValueSymbol(const ValueSymbol& symbol) : symbol(&symbol) {}
  };
  SmallVector<LValueSymbol> lvalues;

  // All of the nets and variables that have been read in the procedure.
  SmallMap<const ValueSymbol*, SymbolNodeMap, 4> rvalues;

  // The currently active longest static prefix expression, if there is one.
  LSPVisitor<NetlistAnalysis> lspVisitor;
  bool isLValue = false;
  
  
  NetlistAnalysis(const Symbol& symbol) : AbstractFlowAnalysis(symbol, {}), bitMapAllocator(allocator), lspVisitor(*this) {}

  [[nodiscard]] auto saveLValueFlag() {
      auto guard = ScopeGuard([this, savedLVal = isLValue] { isLValue = savedLVal; });
      isLValue = false;
      return guard;
  }

  void noteReference(const ValueSymbol& symbol, const Expression& lsp) {
    // TODO
  }

  // **** AST Handlers ****

  template<typename T>
      requires(std::is_base_of_v<Expression, T> && !IsSelectExpr<T>)
  void handle(const T& expr) {
      lspVisitor.clear();
      visitExpr(expr);
  }

  template<typename T>
      requires(IsSelectExpr<T>)
  void handle(const T& expr) {
      lspVisitor.handle(expr);
  }

  void handle(const AssignmentExpression& expr) {
    // Note that this method mirrors the logic in the base class
    // handler but we need to track the LValue status of the lhs.
    SLANG_ASSERT(!isLValue);
    isLValue = true;
    visit(expr.left());
    isLValue = false;

    if (!expr.isLValueArg()) {
        visit(expr.right());
    }
  }
    
  // **** State Management ****

  void joinState(NetlistState & result, const NetlistState& other) { 
    if (result.reachable == other.reachable) {
        if (result.assigned.size() > other.assigned.size()) {
            result.assigned.resize(other.assigned.size());
        }

        for (size_t i = 0; i < result.assigned.size(); i++) {
            result.assigned[i] = result.assigned[i].intersection(other.assigned[i],
                                                                 bitMapAllocator);
        }
    } else if (!result.reachable) {
        result = copyState(other);
    }
  }
  
  void meetState(NetlistState& result, const NetlistState& other)  { 
    if (!other.reachable) {
        result.reachable = false;
        return;
    }

    // Union the assigned state across each variable.
    if (result.assigned.size() < other.assigned.size()) {
        result.assigned.resize(other.assigned.size());
    }

    for (size_t i = 0; i < other.assigned.size(); i++) {
        for (auto it = other.assigned[i].begin(); it != other.assigned[i].end(); ++it) {
            result.assigned[i].unionWith(it.bounds(), *it, bitMapAllocator);
        }
    }
  }

  NetlistState copyState(const NetlistState& source) { 
    NetlistState result;
    result.reachable = source.reachable;
    result.assigned.reserve(source.assigned.size());
    for (size_t i = 0; i < source.assigned.size(); i++) {
        result.assigned.emplace_back(source.assigned[i].clone(bitMapAllocator));
    }
    return result;
  }

  NetlistState unreachableState() const { 
    NetlistState result;
    result.reachable = false;
    return result;
}

  NetlistState topState() const { return {}; }
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
