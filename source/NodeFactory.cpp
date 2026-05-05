#include "NodeFactory.hpp"

#include "BitSliceList.hpp"
#include "NetlistBuilder.hpp"

namespace slang::netlist {

auto NodeFactory::createAssignment(ast::AssignmentExpression const &expr)
    -> NetlistNode & {
  auto node = std::make_unique<Assignment>(
      builder.toTextLocation(expr.sourceRange.start()));
  return builder.graph.addNode(std::move(node));
}

auto NodeFactory::createConditional(ast::ConditionalStatement const &stmt)
    -> NetlistNode & {
  auto node = std::make_unique<Conditional>(
      builder.toTextLocation(stmt.sourceRange.start()));
  return builder.graph.addNode(std::move(node));
}

auto NodeFactory::createCase(ast::CaseStatement const &stmt) -> NetlistNode & {
  auto node =
      std::make_unique<Case>(builder.toTextLocation(stmt.sourceRange.start()));
  return builder.graph.addNode(std::move(node));
}

auto NodeFactory::createConstant(ConstantValue value, uint64_t width,
                                 TextLocation location) -> NetlistNode & {
  auto node = std::make_unique<Constant>(std::move(value), width, location);
  return builder.graph.addNode(std::move(node));
}

auto NodeFactory::createConstantForSegment(BitSliceSource const &src,
                                           Segment const &seg,
                                           TextLocation fallbackLoc)
    -> NetlistNode & {
  SLANG_ASSERT(src.kind == BitSliceSource::Kind::Constant);
  auto offset = seg.concatLo - src.srcLo;
  auto segWidth = seg.width();
  ConstantValue sliced = src.constantValue;
  if (sliced.isInteger()) {
    auto const &svInt = sliced.integer();
    if (svInt.getBitWidth() != segWidth) {
      sliced =
          ConstantValue(svInt.slice(static_cast<int32_t>(offset + segWidth - 1),
                                    static_cast<int32_t>(offset)));
    }
  }
  auto loc = src.constantExpr != nullptr
                 ? builder.toTextLocation(src.constantExpr->sourceRange.start())
                 : fallbackLoc;
  return createConstant(std::move(sliced), segWidth, loc);
}

auto NodeFactory::createPort(ast::PortSymbol const &symbol,
                             DriverBitRange bounds) -> NetlistNode & {
  SLANG_ASSERT(symbol.internalSymbol != nullptr);
  auto ref = builder.toSymbolRef(*symbol.internalSymbol);
  auto &node = builder.graph.addNode(std::make_unique<Port>(
      std::move(ref.name), std::move(ref.hierarchicalPath), ref.location,
      symbol.direction, bounds));
  builder.variables.insert(symbol, bounds, node);
  return node;
}

auto NodeFactory::createVariable(ast::VariableSymbol const &symbol,
                                 DriverBitRange bounds) -> NetlistNode & {
  auto ref = builder.toSymbolRef(symbol);
  auto &node = builder.graph.addNode(std::make_unique<Variable>(
      std::move(ref.name), std::move(ref.hierarchicalPath), ref.location,
      bounds));
  builder.variables.insert(symbol, bounds, node);
  return node;
}

auto NodeFactory::createState(ast::ValueSymbol const &symbol,
                              DriverBitRange bounds) -> NetlistNode & {
  auto symRef = builder.toSymbolRef(symbol);
  auto node = std::make_unique<State>(std::move(symRef.name),
                                      std::move(symRef.hierarchicalPath),
                                      symRef.location, bounds);
  auto &ref = builder.graph.addNode(std::move(node));
  builder.variables.insert(symbol, bounds, ref);
  return ref;
}

} // namespace slang::netlist
