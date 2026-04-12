#pragma once

#include "netlist/Debug.hpp"
#include "netlist/DirectedGraph.hpp"
#include "netlist/NetlistEdge.hpp"
#include "netlist/NetlistNode.hpp"
#include "netlist/TextLocation.hpp"

#include <algorithm>
#include <ranges>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace slang {
namespace ast {
class Compilation;
} // namespace ast
namespace analysis {
class AnalysisManager;
} // namespace analysis
} // namespace slang

namespace slang::netlist {

/// Represent the netlist connectivity of an elaborated design.
class NetlistGraph : public DirectedGraph<NetlistNode, NetlistEdge> {
public:
  FileTable fileTable;

  /// Build the netlist from an elaborated compilation.
  ///
  /// Caller is responsible for having run `VisitAll`, frozen the compilation,
  /// and run the analysis manager prior to this call. \p numThreads specifies
  /// the thread pool size when \p parallel is true; 0 means use hardware
  /// concurrency.
  void build(ast::Compilation &compilation,
             analysis::AnalysisManager &analysisManager, bool parallel = true,
             unsigned numThreads = 0);

  /// Lookup a node in the graph by its hierarchical name.
  ///
  /// @param name The hierarchical name of the node.
  /// @return A pointer to the node if found, or nullptr if not found.
  [[nodiscard]] auto lookup(std::string_view name) const -> NetlistNode *;

  /// Lookup nodes by hierarchical name and bit range.
  ///
  /// Returns all Port, Variable, and State nodes whose hierarchical path
  /// matches @p name and whose bounds overlap with @p bounds.
  [[nodiscard]] auto lookup(std::string_view name, DriverBitRange bounds) const
      -> std::vector<NetlistNode *>;

  /// Return the set of driver nodes for the symbol with the given hierarchical
  /// @p name over the bit range @p bounds.
  ///
  /// A driver is any node that is the source of an edge annotated with a
  /// matching symbol reference whose bounds overlap @p bounds. Each driver is
  /// reported at most once.
  [[nodiscard]] auto getDrivers(std::string_view name,
                                DriverBitRange bounds) const
      -> std::vector<NetlistNode *>;

  /// Return all nodes reachable from @p node via combinational edges in the
  /// forward (fan-out) direction.  The traversal stops at State nodes.
  [[nodiscard]] auto getCombFanOut(NetlistNode &node) const
      -> std::vector<NetlistNode *>;

  /// Return all nodes that can reach @p node via combinational edges in the
  /// backward (fan-in) direction.  The traversal stops at State nodes.
  [[nodiscard]] auto getCombFanIn(NetlistNode &node) const
      -> std::vector<NetlistNode *>;

  /// Find named nodes whose hierarchical path matches the wildcard @p pattern.
  /// Supports '*' (zero or more characters) and '?' (one character).
  [[nodiscard]] auto findNodes(std::string_view pattern) const
      -> std::vector<NetlistNode *>;

  /// Find named nodes whose hierarchical path matches the regex @p pattern.
  [[nodiscard]] auto findNodesRegex(std::string_view pattern) const
      -> std::vector<NetlistNode *>;

  /// Return a view of all nodes of the specified kind.
  ///
  /// @param kind The kind of nodes to filter.
  /// @return A view of nodes matching the specified kind.
  [[nodiscard]]
  auto filterNodes(NodeKind kind) const {
    return nodes |
           std::views::filter([kind](std::unique_ptr<NetlistNode> const &p) {
             return p->kind == kind;
           });
  }

  /// Add an edge between two nodes.
  auto addEdge(NetlistNode &sourceNode, NetlistNode &targetNode)
      -> NetlistEdge & {
    return sourceNode.addEdge(targetNode);
  }

private:
  mutable bool indexBuilt = false;
  mutable std::unordered_map<std::string, std::vector<NetlistNode *>> nodeIndex;
  void buildIndex() const;
};

} // namespace slang::netlist
