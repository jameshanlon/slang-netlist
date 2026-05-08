#pragma once

#include <algorithm>
#include <cassert>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include "slang/util/FlatMap.h"

namespace slang::netlist {

/// A class to represent a directed edge in a graph.
template <class NodeType, class EdgeType> class DirectedEdge {
public:
  DirectedEdge(NodeType &sourceNode, NodeType &targetNode)
      : sourceNode(sourceNode), targetNode(targetNode) {}

  auto operator=(const DirectedEdge<NodeType, EdgeType> &edge)
      -> DirectedEdge<NodeType, EdgeType> & = default;

  /// Static polymorphism: delegate implementation (via isEqualTo) to the
  /// derived class. Add friend operator to resolve ambiguity between operand
  /// ordering with C++20.
  friend auto operator==(const EdgeType &A, const EdgeType &B) noexcept
      -> bool {
    return A.getDerived().isEqualTo(B);
  }
  auto operator==(const EdgeType &E) const -> bool {
    return getDerived().isEqualTo(E);
  }

  /// Return the source node of this edge.
  auto getSourceNode() const -> NodeType & { return sourceNode; }

  /// Return the target node of this edge.
  auto getTargetNode() const -> NodeType & { return targetNode; }

protected:
  // As the default implementation use address comparison for equality.
  auto isEqualTo(const EdgeType &edge) const -> bool { return this == &edge; }

  // Cast the 'this' pointer to the derived type and return a reference.
  auto getDerived() -> EdgeType & { return *static_cast<EdgeType *>(this); }
  auto getDerived() const -> const EdgeType & {
    return *static_cast<const EdgeType *>(this);
  }

  NodeType &sourceNode;
  NodeType &targetNode;
};

/// A class to represent a node in a directed graph.
///
/// Edges are owned by their source node via std::unique_ptr in @c outEdges.
/// The target node stores a raw pointer in @c inEdges; lifetime is bounded
/// by the source's outEdges entry.
template <class NodeType, class EdgeType> class Node {
public:
  using OutEdgePtrType = std::unique_ptr<EdgeType>;
  using OutEdgeListType = std::vector<OutEdgePtrType>;
  using InEdgeListType = std::vector<EdgeType *>;
  using iterator = typename OutEdgeListType::iterator;
  using const_iterator = typename OutEdgeListType::const_iterator;
  using in_iterator = typename InEdgeListType::iterator;
  using const_in_iterator = typename InEdgeListType::const_iterator;
  using edge_descriptor = EdgeType *;

  Node() = default;
  virtual ~Node() = default;

  // Non-copyable/non-movable: edgeMutex is not movable.
  Node(const Node &) = delete;
  Node(Node &&) = delete;
  auto operator=(const Node &) -> Node & = delete;
  auto operator=(Node &&) -> Node & = delete;

  // Iterator methods for outgoing edges.
  auto begin() const -> const_iterator { return outEdges.begin(); }
  auto end() const -> const_iterator { return outEdges.end(); }
  auto begin() -> iterator { return outEdges.begin(); }
  auto end() -> iterator { return outEdges.end(); }

  // Iterator methods for incoming edges.
  auto inBegin() -> in_iterator { return inEdges.begin(); }
  auto inEnd() -> in_iterator { return inEdges.end(); }
  auto inBegin() const -> const_in_iterator { return inEdges.begin(); }
  auto inEnd() const -> const_in_iterator { return inEdges.end(); }

  /// Static polymorphism: delegate implementation (via isEqualTo) to the
  /// derived class. Add friend operator to resolve ambiguity between operand
  /// ordering with C++20.
  friend auto operator==(NodeType const &A, NodeType const &B) noexcept
      -> bool {
    return A.getDerived().isEqualTo(B);
  }

  auto operator==(const NodeType &N) const -> bool {
    return getDerived().isEqualTo(N);
  }

  /// Return an iterator to the edge connecting the source node.
  auto findEdgeFrom(const NodeType &sourceNode) -> in_iterator {
    return std::ranges::find_if(inEdges, [&](EdgeType *e) {
      return &e->getSourceNode() == &sourceNode;
    });
  }

  /// Return an iterator to the edge connecting the source node.
  auto findEdgeFrom(const NodeType &sourceNode) const -> const_in_iterator {
    return std::ranges::find_if(inEdges, [&](EdgeType const *e) {
      return &e->getSourceNode() == &sourceNode;
    });
  }

  /// Return an iterator to the edge connecting the target node.
  auto findEdgeTo(const NodeType &targetNode) -> iterator {
    return std::ranges::find_if(outEdges, [&](OutEdgePtrType const &e) {
      return &e->getTargetNode() == &targetNode;
    });
  }

  /// Return an iterator to the edge connecting the target node.
  auto findEdgeTo(const NodeType &targetNode) const -> const_iterator {
    return std::ranges::find_if(outEdges, [&](OutEdgePtrType const &e) {
      return &e->getTargetNode() == &targetNode;
    });
  }

  /// Add an edge between this node and a target node, only if it does not
  /// already exist. Return a reference to the newly-created edge.
  ///
  /// O(1) amortized: outEdgeIndex memoizes the first edge to each target,
  /// avoiding the linear outEdges scan that becomes quadratic on
  /// high-fan-out nodes (e.g. shared clocks driving every instance after
  /// non-canonical-instance resolution). The index is allocated lazily
  /// once @c outEdges grows past @c outEdgeIndexThreshold; below that, a
  /// linear scan over the few outEdges is faster and avoids the map's
  /// empty-control-byte overhead per node.
  ///
  /// Thread safety: safe to call concurrently. Lock ordering: source
  /// edgeMutex before target edgeMutex (self-edges use a single lock).
  auto addEdge(NodeType &targetNode) -> EdgeType & {
    bool isSelfEdge = (&getDerived() == &targetNode);
    std::lock_guard<std::mutex> lock(edgeMutex);
    if (auto *existing = lookupOutEdge(targetNode); existing != nullptr) {
      return *existing;
    }
    auto edge = std::make_unique<EdgeType>(getDerived(), targetNode);
    auto *edgePtr = edge.get();
    outEdges.emplace_back(std::move(edge));
    insertOutEdgeIndex(&targetNode, edgePtr);
    if (isSelfEdge) {
      inEdges.push_back(edgePtr);
    } else {
      std::lock_guard<std::mutex> lock2(targetNode.edgeMutex);
      targetNode.inEdges.push_back(edgePtr);
    }
    return *edgePtr;
  }

  /// Unconditionally add a new edge between this node and a target node,
  /// even if one already exists (creating a parallel edge). The
  /// outEdgeIndex is left untouched: it points at the *first* edge to the
  /// target.
  ///
  /// Thread safety: safe to call concurrently. Lock ordering: source
  /// edgeMutex before target edgeMutex (self-edges use a single lock).
  auto addNewEdge(NodeType &targetNode) -> EdgeType & {
    bool isSelfEdge = (&getDerived() == &targetNode);
    auto edge = std::make_unique<EdgeType>(getDerived(), targetNode);
    auto *edgePtr = edge.get();
    if (isSelfEdge) {
      std::lock_guard<std::mutex> lock(edgeMutex);
      outEdges.emplace_back(std::move(edge));
      // If no entry exists yet (caller went straight to addNewEdge), seed
      // it so a later addEdge dedupes against this edge instead of
      // creating a third parallel one.
      tryInsertOutEdgeIndex(&targetNode, edgePtr);
      inEdges.push_back(edgePtr);
    } else {
      {
        std::lock_guard<std::mutex> lock(edgeMutex);
        outEdges.emplace_back(std::move(edge));
        tryInsertOutEdgeIndex(&targetNode, edgePtr);
      }
      {
        std::lock_guard<std::mutex> lock(targetNode.edgeMutex);
        targetNode.inEdges.push_back(edgePtr);
      }
    }
    return *edgePtr;
  }

  /// Remove an edge between this node and a target node.
  /// Return true if the edge existed and was removed, and false otherwise.
  auto removeEdge(NodeType &targetNode) -> bool {
    auto edgeIt = findEdgeTo(targetNode);
    if (edgeIt != outEdges.end()) {
      auto success = targetNode.removeInEdge(getDerived());
      assert(success && "No corresponding in edge reference");
      outEdges.erase(edgeIt);
      // Re-point or drop the index entry: a parallel edge to the same
      // target may still survive in outEdges.
      if (outEdgeIndex != nullptr) {
        auto survivor = std::ranges::find_if(outEdges, [&](auto &e) {
          return &e->getTargetNode() == &targetNode;
        });
        if (survivor == outEdges.end()) {
          outEdgeIndex->erase(&targetNode);
        } else {
          (*outEdgeIndex)[&targetNode] = survivor->get();
        }
      }
      return success;
    }
    return false;
  }

  /// Remove all edges to/from this node.
  void clearAllEdges() {
    // Remove outgoing edges.
    for (auto &edge : outEdges) {
      edge->getTargetNode().removeInEdge(getDerived());
    }
    outEdges.clear();
    outEdgeIndex.reset();
    // Remove incoming edges, creating a temporary list to avoid
    // invalidating the iterator.
    std::vector<NodeType *> sourceNodes;
    for (auto &edge : inEdges) {
      sourceNodes.push_back(&edge->getSourceNode());
    }
    for (auto *sourceNode : sourceNodes) {
      sourceNode->removeEdge(getDerived());
    }
    assert(inEdges.empty());
  }

  /// Populate a result vector of edges from this node to the specified target
  /// node. Return true if at least one edge was found.
  auto getEdgesTo(const NodeType &targetNode, std::vector<EdgeType *> &result)
      -> bool {
    assert(result.empty() && "Expected the results parameter to be empty");
    for (auto &edge : outEdges) {
      if (edge->getTargetNode() == targetNode) {
        result.push_back(edge.get());
      }
    }
    return !result.empty();
  }

  /// Return the list of outgoing edges from this node.
  auto getInEdges() const -> const InEdgeListType & { return inEdges; }
  auto getOutEdges() const -> const OutEdgeListType & { return outEdges; }

  /// Return the total number of edges incoming to this node.
  auto inDegree() const -> size_t { return inEdges.size(); }

  /// Return the total number of edges outgoing from this node.
  auto outDegree() const -> size_t { return outEdges.size(); }

protected:
  /// Per-node mutex protecting inEdges and outEdges.
  /// Lock ordering: when locking two nodes, always lock the source node
  /// (the one whose outEdges is modified) before the target node.
  mutable std::mutex edgeMutex;

  InEdgeListType inEdges;
  OutEdgeListType outEdges;

  /// Index from target-node pointer to the first edge in outEdges with
  /// that target. Allocated lazily once @c outEdges grows past
  /// @c outEdgeIndexThreshold so low-fanout nodes pay no per-node map
  /// overhead. Above the threshold the map keeps addEdge O(1) amortized
  /// regardless of out-degree. Protected by edgeMutex.
  using OutEdgeIndex = flat_hash_map<NodeType const *, EdgeType *>;
  std::unique_ptr<OutEdgeIndex> outEdgeIndex;

  /// Out-degree at which we switch from linear scans of @c outEdges to
  /// the lazily-allocated @c outEdgeIndex map.
  static constexpr size_t outEdgeIndexThreshold = 16;

  // As the default implementation use address comparison for equality.
  auto isEqualTo(const NodeType &node) const -> bool { return this == &node; }

  // Cast the 'this' pointer to the derived type and return a reference.
  auto getDerived() -> NodeType & { return *static_cast<NodeType *>(this); }
  auto getDerived() const -> const NodeType & {
    return *static_cast<const NodeType *>(this);
  }

private:
  /// Remove the reference to an incoming edge from a source node to this
  /// node. This method should only be called as part of removing an output
  /// edge. Return true if the edge existed and was removed, and false
  /// otherwise.
  auto removeInEdge(NodeType &sourceNode) -> bool {
    auto edgeIt = findEdgeFrom(sourceNode);
    if (edgeIt != inEdges.end()) {
      inEdges.erase(edgeIt);
      return true;
    }
    return false;
  }

  /// Look up the first edge to @p targetNode, via the index when allocated
  /// or a linear scan over @c outEdges otherwise. Returns nullptr if no
  /// edge to @p targetNode exists. Caller must hold @c edgeMutex.
  auto lookupOutEdge(NodeType const &targetNode) -> EdgeType * {
    if (outEdgeIndex != nullptr) {
      auto it = outEdgeIndex->find(&targetNode);
      return it != outEdgeIndex->end() ? it->second : nullptr;
    }
    auto it = std::ranges::find_if(outEdges, [&](OutEdgePtrType const &e) {
      return &e->getTargetNode() == &targetNode;
    });
    return it != outEdges.end() ? it->get() : nullptr;
  }

  /// Materialise @c outEdgeIndex from @c outEdges, recording the first
  /// edge to each target. Caller must hold @c edgeMutex.
  void buildOutEdgeIndex() {
    outEdgeIndex = std::make_unique<OutEdgeIndex>();
    outEdgeIndex->reserve(outEdges.size());
    for (auto &e : outEdges) {
      outEdgeIndex->try_emplace(&e->getTargetNode(), e.get());
    }
  }

  /// Insert a fresh (target, edge) entry into @c outEdgeIndex, allocating
  /// the index when crossing @c outEdgeIndexThreshold. Caller must hold
  /// @c edgeMutex and have already pushed the edge onto @c outEdges.
  void insertOutEdgeIndex(NodeType const *targetNode, EdgeType *edgePtr) {
    if (outEdgeIndex != nullptr) {
      outEdgeIndex->emplace(targetNode, edgePtr);
    } else if (outEdges.size() > outEdgeIndexThreshold) {
      buildOutEdgeIndex();
    }
  }

  /// Index variant for addNewEdge: only seed the entry if the target is
  /// not already mapped, so the index keeps pointing at the first edge.
  void tryInsertOutEdgeIndex(NodeType const *targetNode, EdgeType *edgePtr) {
    if (outEdgeIndex != nullptr) {
      outEdgeIndex->try_emplace(targetNode, edgePtr);
    } else if (outEdges.size() > outEdgeIndexThreshold) {
      buildOutEdgeIndex();
    }
  }
};

/// A directed graph.
/// Nodes and edges are stored in an adjacency list data structure, where the
/// DirectedGraph contains a vector of nodes, and each node contains a vector
/// of directed edges to other nodes. Multi-edges are not permitted.
template <class NodeType, class EdgeType> class DirectedGraph {
public:
  using NodePtrType = std::unique_ptr<NodeType>;
  using NodeListType = std::vector<NodePtrType>;
  using iterator = typename NodeListType::iterator;
  using const_iterator = typename NodeListType::const_iterator;
  using node_descriptor = size_t;
  using edge_descriptor = EdgeType *;
  using DirectedGraphType = DirectedGraph<NodeType, EdgeType>;

  static const size_t null_node = std::numeric_limits<size_t>::max();

  DirectedGraph() = default;

  auto begin() const -> const_iterator { return nodes.begin(); }
  auto end() const -> const_iterator { return nodes.end(); }
  auto begin() -> iterator { return nodes.begin(); }
  auto end() -> iterator { return nodes.end(); }

  auto findNode(const NodeType &nodeToFind) const -> node_descriptor {
    auto it =
        std::ranges::find_if(nodes, [&nodeToFind](const NodePtrType &node) {
          return const_cast<const NodeType &>(*node) == nodeToFind;
        });
    if (it != nodes.end()) {
      return it - nodes.begin();
    }
    return null_node;
  }

  /// Given a node descriptor, return the node by reference.
  auto getNode(node_descriptor node) const -> NodeType & {
    assert(node < nodes.size() && "Node does not exist");
    return *nodes[node];
  }

  /// Add a node to the graph and return a reference to it.
  ///
  /// Thread safety: safe to call concurrently from multiple threads.
  auto addNode() -> NodeType & {
    std::lock_guard<std::mutex> lock(nodesMutex);
    nodes.push_back(std::make_unique<NodeType>());
    return *(nodes.back().get());
  }

  /// Add an existing node to the graph and return a reference to it.
  ///
  /// Thread safety: safe to call concurrently from multiple threads.
  auto addNode(std::unique_ptr<NodeType> node) -> NodeType & {
    std::lock_guard<std::mutex> lock(nodesMutex);
    nodes.push_back(std::move(node));
    return *(nodes.back().get());
  }

  /// Remove the specified node from the graph, including all edges that are
  /// incident upon this node, and all edges that are outgoing from this node.
  /// Return true if the node exists and was removed and false if it didn't
  /// exist.
  auto removeNode(NodeType &nodeToRemove) -> bool {
    auto nodeToRemoveDesc = findNode(nodeToRemove);
    if (nodeToRemoveDesc >= nodes.size()) {
      // The node is not in the graph.
      return false;
    }
    // Remove all edges to and from the node for removal.
    nodeToRemove.clearAllEdges();
    // Remove the node itself.
    nodes.erase(std::ranges::next(nodes.begin(), nodeToRemoveDesc));
    return true;
  }

  /// Add an edge between two existing nodes in the graph.
  auto addEdge(NodeType &sourceNode, NodeType &targetNode) -> EdgeType & {
    assert(findNode(sourceNode) < nodes.size() && "Source node does not exist");
    assert(findNode(targetNode) < nodes.size() && "Target node does not exist");
    return sourceNode.addEdge(targetNode);
  }

  /// Unconditionally add a new edge between two existing nodes,
  /// even if one already exists (creating a parallel edge).
  auto addNewEdge(NodeType &sourceNode, NodeType &targetNode) -> EdgeType & {
    assert(findNode(sourceNode) < nodes.size() && "Source node does not exist");
    assert(findNode(targetNode) < nodes.size() && "Target node does not exist");
    return sourceNode.addNewEdge(targetNode);
  }

  /// Remove an edge between the two specified vertices. Return true if the
  /// edge exists and was removed, and false if it didn't exist.
  auto removeEdge(NodeType &sourceNode, NodeType &targetNode) -> bool {
    assert(findNode(sourceNode) < nodes.size() && "Source node does not exist");
    assert(findNode(targetNode) < nodes.size() && "Target node does not exist");
    return sourceNode.removeEdge(targetNode);
  }

  /// Return the number of edges outgoing from the specified node.
  auto outDegree(const NodeType &node) const -> size_t {
    assert(findNode(node) < nodes.size() && "Node does not exist");
    return node.outDegree();
  }

  /// Return the number of edges incident to the specified node.
  auto inDegree(const NodeType &node) const -> size_t {
    assert(findNode(node) < nodes.size() && "Node does not exist");
    return node.inDegree();
  }

  /// Return the size of the graph.
  auto numNodes() const -> size_t { return nodes.size(); }

  /// Return the number of edges in the graph.
  auto numEdges() const -> size_t {
    size_t count = 0;
    for (auto &node : nodes) {
      count += node->outDegree();
    }
    return count;
  }

protected:
  /// Mutex protecting the nodes vector. Only addNode() acquires this;
  /// iteration and read-only access are safe after build() completes.
  mutable std::mutex nodesMutex;

  NodeListType nodes;
};

} // namespace slang::netlist
