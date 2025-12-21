#include "Test.hpp"

TEST_CASE("New driver range that contains an existing one") {
  auto const &tree = (R"(
module m(input logic [3:0] a, output logic [3:0] b);
  logic [3:0] t;
  always_comb begin
    t[1:0] = a[1:0];
    t[3:0] = a[3:0];
  end
  assign b = t;
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
}

TEST_CASE("New driver range that left-overlaps an existing one (replace)") {
  auto const &tree = (R"(
module m(input logic [3:0] a, output logic [3:0] b);
  logic [3:0] t;
  always_comb begin
    t[3:2] = a[1:0];
    t[2:0] = a[2:0];
  end
  assign b = t;
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
}

TEST_CASE("New driver range that right-overlaps an existing one (replace)") {
  auto const &tree = (R"(
module m(input logic [3:0] a, output logic [3:0] b);
  logic [3:0] t;
  always_comb begin
    t[2:0] = a[2:0];
    t[3:2] = a[1:0];
  end
  assign b = t;
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
}

TEST_CASE("New driver range that left-overlaps an existing one (merge)") {
  auto const &tree = (R"(
module m(input logic [3:0] a, input logic c, output logic [3:0] b);
  logic [3:0] t;
  always_comb begin
    if (c)
      t[3:2] = a[1:0];
    else
      t[2:0] = a[2:0];
  end
  assign b = t;
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
}

TEST_CASE("New driver range that right-overlaps an existing one (merge)") {
  auto const &tree = (R"(
module m(input logic [3:0] a, input logic c, output logic [3:0] b);
  logic [3:0] t;
  always_comb begin
    if (c)
      t[2:0] = a[2:0];
    else
      t[3:2] = a[1:0];
  end
  assign b = t;
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
}
