#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"
#include "slang/util/VersionInfo.h"
#include "slang/analysis/AbstractFlowAnalysis.h"
#include "slang/util/IntervalMap.h"
#include "slang/util/BumpAllocator.h"
#include "slang/text/FormatBuffer.h"

#include "DirectedGraph.hpp"

using namespace slang;
using namespace slang::analysis;
using namespace slang::ast;
using namespace slang::driver;

struct NetlistNode {
};

struct NetlistEdge {
};

using NetlistGraph = DirectedGraph<NetlistNode, NetlistEdge>;

template<typename T>
concept IsSelectExpr =
    IsAnyOf<T, ElementSelectExpression, RangeSelectExpression, MemberAccessExpression,
            HierarchicalValueExpression, NamedValueExpression>;

// Map assigned ranges to graph nodes.
using SymbolBitMap = IntervalMap<uint64_t, std::monostate, 3>;
using SymbolLSPMap = IntervalMap<uint64_t, const Expression*, 5>;



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
    }
};


struct SLANG_EXPORT NetlistState {
    
  /// Each tracked variable has its assigned intervals stored here.
  SmallVector<SymbolBitMap, 2> assigned;

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
  SymbolBitMap::allocator_type bitMapAllocator;
  SymbolLSPMap::allocator_type lspMapAllocator;
    
  // Maps visited symbols to slots in assigned vectors.
  SmallMap<const ValueSymbol*, uint32_t, 4> symbolToSlot;

  // Tracks the assigned ranges of each variable across the entire procedure,
  // even if not all branches assign to it.
  struct LValueSymbol {
    not_null<const ValueSymbol*> symbol;
    SymbolLSPMap assigned;

    LValueSymbol(const ValueSymbol& symbol) : symbol(&symbol) {}
  };
  SmallVector<LValueSymbol> lvalues;

  // All of the nets and variables that have been read in the procedure.
  SmallMap<const ValueSymbol*, SymbolBitMap, 4> rvalues;

  // The currently active longest static prefix expression, if there is one.
  LSPVisitor<NetlistAnalysis> lspVisitor;
  bool isLValue = false;
  
  
  NetlistAnalysis(const Symbol& symbol) : AbstractFlowAnalysis(symbol, {}), bitMapAllocator(allocator), lspMapAllocator(allocator), lspVisitor(*this) {}

  [[nodiscard]] auto saveLValueFlag() {
      auto guard = ScopeGuard([this, savedLVal = isLValue] { isLValue = savedLVal; });
      isLValue = false;
      return guard;
  }

  void noteReference(const ValueSymbol& symbol, const Expression& lsp) {
    fmt::print("Note reference: {}\n", symbol.name);
    
    // This feels icky but we don't count a symbol as being referenced in the procedure
    // if it's only used inside an unreachable flow path. The alternative would just
    // frustrate users, but the reason it's icky is because whether a path is reachable
    // is based on whatever level of heuristics we're willing to implement rather than
    // some well defined set of rules in the LRM.
    auto& currState = getState();
    if (!currState.reachable)
        return;

    auto bounds = ValueDriver::getBounds(lsp, getEvalContext(), symbol.getType());
    if (!bounds) {
        // This probably cannot be hit given that we early out elsewhere for
        // invalid expressions.
        return;
    }

    if (isLValue) {
        auto [it, inserted] = symbolToSlot.try_emplace(&symbol, (uint32_t)lvalues.size());
        if (inserted) {
            lvalues.emplace_back(symbol);
            SLANG_ASSERT(lvalues.size() == symbolToSlot.size());
        }

        auto index = it->second;
        if (index >= currState.assigned.size())
            currState.assigned.resize(index + 1);

        currState.assigned[index].unionWith(*bounds, {}, bitMapAllocator);

        auto& lspMap = lvalues[index].assigned;
        for (auto lspIt = lspMap.find(*bounds); lspIt != lspMap.end();) {
            // If we find an existing entry that completely contains
            // the new bounds we can just keep that one and ignore the
            // new one. Otherwise we will insert a new entry.
            auto itBounds = lspIt.bounds();
            if (itBounds.first <= bounds->first && itBounds.second >= bounds->second)
                return;

            // If the new bounds completely contain the existing entry, we can remove it.
            if (bounds->first < itBounds.first && bounds->second > itBounds.second) {
                lspMap.erase(lspIt, lspMapAllocator);
                lspIt = lspMap.find(*bounds);
            }
            else {
                ++lspIt;
            }
        }
        lspMap.insert(*bounds, &lsp, lspMapAllocator);
    }
    else {
        rvalues[&symbol].unionWith(*bounds, {}, bitMapAllocator);
    }
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
    fmt::print("AssignmentExpression\n");
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
  
  void handle(const ConditionalStatement& stmt) {
    fmt::print("ConditionalStatement\n");
    visitStmt(stmt);
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
  NetlistGraph &graph;

public:
    explicit NetlistVisitor(ast::Compilation& compilation, NetlistGraph &graph) :
        compilation(compilation), graph(graph) {}
    
    void handle(const ast::ProceduralBlockSymbol& symbol) {
      fmt::print("ProceduralBlock\n"); 
      NetlistAnalysis dfa(symbol);
      dfa.run(symbol.as<ProceduralBlockSymbol>().getBody());
    }
};

void printDOT(const NetlistGraph& netlist, const std::string& fileName) {
    slang::FormatBuffer buffer;
    buffer.append("digraph {\n");
    buffer.append("  node [shape=record];\n");
//    for (auto& node : netlist) {
//        switch (node->kind) {
//            case NodeKind::PortDeclaration: {
//                auto& portDecl = node->as<NetlistPortDeclaration>();
//                buffer.format("  N{} [label=\"Port declaration\\n{}\"]\n", node->ID,
//                              portDecl.hierarchicalPath);
//                break;
//            }
//            case NodeKind::VariableDeclaration: {
//                auto& varDecl = node->as<NetlistVariableDeclaration>();
//                buffer.format("  N{} [label=\"Variable declaration\\n{}\"]\n", node->ID,
//                              varDecl.hierarchicalPath);
//                break;
//            }
//            case NodeKind::VariableAlias: {
//                auto& varAlias = node->as<NetlistVariableAlias>();
//                buffer.format("  N{} [label=\"Variable alias\\n{}\"]\n", node->ID,
//                              varAlias.hierarchicalPath);
//                break;
//            }
//            case NodeKind::VariableReference: {
//                auto& varRef = node->as<NetlistVariableReference>();
//                if (!varRef.isLeftOperand())
//                    buffer.format("  N{} [label=\"{}\\n\"]\n", node->ID, varRef.toString());
//                else if (node->edgeKind == EdgeKind::None)
//                    buffer.format("  N{} [label=\"{}\\n[Assigned to]\"]\n", node->ID,
//                                  varRef.toString());
//                else
//                    buffer.format("  N{} [label=\"{}\\n[Assigned to @({})]\"]\n", node->ID,
//                                  varRef.toString(), toString(node->edgeKind));
//                break;
//            }
//            default:
//                SLANG_UNREACHABLE;
//        }
//    }
//    for (auto& node : netlist) {
//        for (auto& edge : node->getEdges()) {
//            if (!edge->disabled) {
//                buffer.format("  N{} -> N{}\n", node->ID, edge->getTargetNode().ID);
//            }
//        }
//    }
    buffer.append("}\n");
    OS::writeFile(fileName, buffer.str());
}


int main(int argc, char** argv) {
    Driver driver;
    driver.addStandardArgs();

    std::optional<bool> showHelp;
    std::optional<bool> showVersion;
    std::optional<std::string> netlistDotFile;
    driver.cmdLine.add("-h,--help", showHelp, "Display available options");
    driver.cmdLine.add("--version", showVersion, "Display version information and exit");
    
    driver.cmdLine.add("--netlist-dot", netlistDotFile,
                       "Dump the netlist in DOT format to the specified file, or '-' for stdout",
                       "<file>", CommandLineFlags::FilePath);

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
