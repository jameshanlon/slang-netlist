#pragma once

#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"

#include "DirectedGraph.hpp"

namespace slang::netlist {

enum class NodeKind {
    None = 0,
    PortDeclaration,
    VariableDeclaration,
    VariableReference,
    VariableAlias,
    Assignment,
    Conditional,
    Join,
};

class NetlistNode;
class NetlistEdge;

/// Represent a node in the netlist, corresponding to a variable or an
/// operation.
class NetlistNode : public Node<NetlistNode, NetlistEdge> {
public:
    NetlistNode(NodeKind kind) :
        ID(0/*++nextID*/), kind(kind) {};

    ~NetlistNode() override = default;

    template<typename T>
    T& as() {
        SLANG_ASSERT(T::isKind(kind));
        return *(static_cast<T*>(this));
    }

    template<typename T>
    const T& as() const {
        SLANG_ASSERT(T::isKind(kind));
        return const_cast<T&>(this->as<T>());
    }

public:
    size_t ID;
    NodeKind kind;

//private:
//    static size_t nextID;
};

/// A class representing a dependency between two variables in the netlist.
class NetlistEdge : public DirectedEdge<NetlistNode, NetlistEdge> {
public:
    NetlistEdge(NetlistNode& sourceNode, NetlistNode& targetNode) :
        DirectedEdge(sourceNode, targetNode) {}

    void disable() { disabled = true; }

public:
    bool disabled{false};
};

class PortDeclaration : public NetlistNode {
public:
    PortDeclaration(ast::Symbol const& symbol) :
        NetlistNode(NodeKind::PortDeclaration), symbol(symbol) {}

    static bool isKind(NodeKind otherKind) { return otherKind == NodeKind::PortDeclaration; }

    ast::Symbol const &symbol;
};

class VariableDeclaration : public NetlistNode {
public:
    VariableDeclaration(ast::Symbol const& symbol) :
        NetlistNode(NodeKind::VariableDeclaration), symbol(symbol) {}

    static bool isKind(NodeKind otherKind) { return otherKind == NodeKind::VariableDeclaration; }
    
    ast::Symbol const &symbol;
};

class VariableAlias : public NetlistNode {
public:
    VariableAlias(ast::Symbol const& symbol) :
        NetlistNode(NodeKind::VariableAlias), symbol(symbol) {}

    static bool isKind(NodeKind otherKind) { return otherKind == NodeKind::VariableAlias; }
    
    ast::Symbol const &symbol;
};

class VariableReference : public NetlistNode {
public:
    VariableReference(ast::Symbol const& symbol, ast::Expression const& expr) :
        NetlistNode(NodeKind::VariableReference), symbol(symbol), expression(expr) {}

    static bool isKind(NodeKind otherKind) { return otherKind == NodeKind::VariableReference; }
    
    ast::Symbol const &symbol;
    ast::Expression const &expression;
};

class Assignment : public NetlistNode {
public:
    Assignment() :
        NetlistNode(NodeKind::Assignment) {}

    static bool isKind(NodeKind otherKind) { return otherKind == NodeKind::Assignment; }
};

class Conditional : public NetlistNode {
public:
    Conditional() :
        NetlistNode(NodeKind::Conditional) {}

    static bool isKind(NodeKind otherKind) { return otherKind == NodeKind::Conditional; }
};

class Join : public NetlistNode {
public:
    Join() :
        NetlistNode(NodeKind::Join) {}

    static bool isKind(NodeKind otherKind) { return otherKind == NodeKind::Join; }
};

using NetlistGraph = DirectedGraph<NetlistNode, NetlistEdge>;

} // namespace slang::netlist
