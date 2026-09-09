#include "Test.hpp"

TEST_CASE("Net declaration assignment connects source to sink", "[NetDecl]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  wire w = a;
  assign b = w;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
}

TEST_CASE("Net declaration assignment inside a generate block",
          "[NetDecl]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  for (genvar i = 0; i < 1; i++) begin : g
    wire w = a;
    assign b = w;
  end
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
}

TEST_CASE("Net declaration assignment drives an instance input port",
          "[NetDecl]") {
  auto const &tree = R"(
module sub(input logic [3:0] x, output logic y);
  assign y = ^x;
endmodule

module m(input logic [3:0] a, output logic y);
  wire [3:0] w = a;
  sub u(.x(w), .y(y));
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.u.x"));
  CHECK(test.pathExists("m.a", "m.y"));
}

TEST_CASE("Net declaration assignment matches an explicit continuous assign",
          "[NetDecl]") {
  auto const &tree = R"(
module m(input logic a, output logic b, output logic c);
  wire w0 = a;
  wire w1;
  assign w1 = a;
  assign b = w0;
  assign c = w1;
endmodule
)";
  const NetlistTest test(tree);
  auto viaDecl = test.findPath("m.a", "m.b");
  auto viaAssign = test.findPath("m.a", "m.c");
  CHECK_FALSE(viaDecl.empty());
  CHECK(viaDecl.size() == viaAssign.size());
}

TEST_CASE("Net declaration assignment of a concatenation is bit accurate",
          "[NetDecl]") {
  auto const &tree = R"(
module m(input logic [1:0] a, input logic [1:0] b, output logic [3:0] o);
  wire [3:0] w = {a, b};
  assign o = w;
endmodule
)";
  NetlistTest test(tree);
  // The low half of the concatenation comes from b, the high half from a.
  CHECK(test.pathExists("m.b", "m.o"));
  CHECK(test.pathExists("m.a", "m.o"));
  CHECK_FALSE(test.getDrivers("m.w", {0, 1}).empty());
  CHECK_FALSE(test.getDrivers("m.w", {2, 3}).empty());
}

TEST_CASE("Net declaration assignment with no initialiser is unchanged",
          "[NetDecl]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  wire w;
  assign w = a;
  assign b = w;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
}
