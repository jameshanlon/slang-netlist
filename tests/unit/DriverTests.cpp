#include "Test.hpp"

TEST_CASE("Driver range that contains an existing one", "[Drivers]") {
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
  NetlistTest test(tree);
  CHECK(test.getDrivers("m.t", {3, 3}).size() == 1);
  CHECK(test.getDrivers("m.t", {1, 0}).size() == 1);
}

TEST_CASE("Driver range that left-overlaps an existing one (replace)",
          "[Drivers]") {
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
  NetlistTest test(tree);
  CHECK(test.getDrivers("m.t", {3, 3}).size() == 1);
  CHECK(test.getDrivers("m.t", {2, 2}).size() == 1);
  CHECK(test.getDrivers("m.t", {1, 0}).size() == 1);
}

TEST_CASE("Driver range that right-overlaps an existing one (replace)",
          "[Drivers]") {
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
  NetlistTest test(tree);
  CHECK(test.getDrivers("m.t", {3, 3}).size() == 1);
  CHECK(test.getDrivers("m.t", {2, 2}).size() == 1);
  CHECK(test.getDrivers("m.t", {1, 0}).size() == 1);
}

TEST_CASE("Driver range that left-overlaps an existing one (merge)",
          "[Drivers]") {
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
  NetlistTest test(tree);
  CHECK(test.getDrivers("m.t", {3, 3}).size() == 1);
  CHECK(test.getDrivers("m.t", {2, 2}).size() == 2);
  CHECK(test.getDrivers("m.t", {1, 0}).size() == 1);
}

TEST_CASE("Driver range that right-overlaps an existing one (merge)",
          "[Drivers]") {
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
  NetlistTest test(tree);
  CHECK(test.getDrivers("m.t", {3, 3}).size() == 1);
  CHECK(test.getDrivers("m.t", {2, 2}).size() == 2);
  CHECK(test.getDrivers("m.t", {1, 0}).size() == 1);
}

TEST_CASE("Driver query on directly-written output port", "[Drivers]") {
  // The output port is written directly (no separate internal variable) and
  // the interval map ends up splitting one driver's range because of the
  // overlap. The graph-based getDrivers must still return the correct driver
  // set per bit — exercising both the hookupOutputPort fallback (when an
  // exact-bounds port lookup misses) and the setVariable hull collapse.
  auto const &tree = (R"(
module m(input logic cond, input logic [7:0] a, b, output logic [7:0] x);
  always_comb begin
    if (cond)
      x[7:4] = a[3:0];
    else
      x[7:0] = b;
  end
endmodule
)");
  NetlistTest test(tree);
  // Bits [7:4] are driven in both branches (a in if, b in else).
  CHECK(test.getDrivers("m.x", {7, 4}).size() == 2);
  // Bits [3:0] are driven only by b (in the else branch).
  CHECK(test.getDrivers("m.x", {3, 0}).size() == 1);
}

TEST_CASE("Four-way driver overlap (merge)", "[Drivers]") {
  auto const &tree = (R"(
module m(input logic [3:0] a, input logic [1:0] c, output logic [3:0] b);
  logic [3:0] t;
  always_comb begin
    case (c)
    0: t[1:0] = a[1:0];
    1: t[3:2] = a[1:0];
    2: t[2:1] = a[1:0];
    3: t[1] = a[0];
    endcase
  end
  assign b = t;
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.getDrivers("m.t", {3, 3}).size() == 1);
  CHECK(test.getDrivers("m.t", {2, 2}).size() == 2);
  CHECK(test.getDrivers("m.t", {1, 1}).size() == 3);
  CHECK(test.getDrivers("m.t", {0, 0}).size() == 1);
}

// A signal whose two halves are driven by distinct assignments.
static constexpr auto splitDriversSV = R"(
module m(input logic [3:0] a, input logic [3:0] b, output logic [3:0] y);
  assign y[1:0] = a[1:0];
  assign y[3:2] = b[1:0];
endmodule
)";

TEST_CASE("Bit-drivers report distinct sources per bit range", "[BitDrivers]") {
  NetlistTest test(splitDriversSV);
  auto drivers = test.getBitDrivers("m.y", {3, 0});
  // One entry per contiguous slice, sorted by ascending bit position.
  REQUIRE(drivers.size() == 2);
  CHECK(drivers[0].bounds.lower() == 0);
  CHECK(drivers[0].bounds.upper() == 1);
  CHECK(drivers[1].bounds.lower() == 2);
  CHECK(drivers[1].bounds.upper() == 3);
  // The two slices are driven by different assignment nodes.
  CHECK(drivers[0].driver != drivers[1].driver);
}

TEST_CASE("Bit-drivers clip to the queried range", "[BitDrivers]") {
  NetlistTest test(splitDriversSV);
  // Querying a single bit inside the [3:2] slice clips the reported range.
  auto drivers = test.getBitDrivers("m.y", {2, 2});
  REQUIRE(drivers.size() == 1);
  CHECK(drivers[0].bounds.lower() == 2);
  CHECK(drivers[0].bounds.upper() == 2);
}

TEST_CASE("Bit-drivers of an undriven signal are empty", "[BitDrivers]") {
  auto const &tree = (R"(
module m(input logic [3:0] a, output logic [3:0] y);
  logic [3:0] u;
  assign y = a;
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.getBitDrivers("m.u", {3, 0}).empty());
}
