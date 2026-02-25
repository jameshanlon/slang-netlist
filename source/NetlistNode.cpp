#include "netlist/NetlistNode.hpp"

#include <atomic>
#include <cstddef>

std::atomic<size_t> slang::netlist::NetlistNode::nextID{1};
