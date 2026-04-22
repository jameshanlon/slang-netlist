#include "Test.hpp"

TEST_CASE("Concat LHS/RHS: {a,b} = {c,d} has no cross edges", "[Concat]") {
  auto const *tree = R"(
module m(input c, d, output a, b);
  assign {a, b} = {c, d};
endmodule
)";
  NetlistTest test(tree, BuilderOptions{.resolveAssignBits = true});
  // a is the upper bit and should be driven only by c; b is the lower
  // bit and should be driven only by d.
  CHECK(test.pathExists("m.c", "m.a"));
  CHECK_FALSE(test.pathExists("m.c", "m.b"));
  CHECK(test.pathExists("m.d", "m.b"));
  CHECK_FALSE(test.pathExists("m.d", "m.a"));
}
