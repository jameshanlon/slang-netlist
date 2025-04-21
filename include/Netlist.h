#pragma once

#include "DirectedGraph.hpp"

namespace slang::netlist {

struct NetlistNode {
};

struct NetlistEdge {
};

using NetlistGraph = DirectedGraph<NetlistNode, NetlistEdge>;

} // namespace slang::netlist
