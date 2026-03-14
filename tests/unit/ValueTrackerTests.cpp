#include "Test.hpp"

TEST_CASE("Double-free when overwriting split driver intervals",
          "[ValueTracker]") {
  // When an existing [0:7] entry is split by a narrower assignment (t[3]=b),
  // both halves [0:2] and [4:7] reference the same DriverListHandle.  A
  // subsequent full-width replacement (t=c) iterated over both and freed that
  // shared handle twice, causing undefined behaviour.
  auto const &tree = R"(
module m (input logic [7:0] a, b, c, output logic [7:0] out);
  logic [7:0] t;
  always_comb begin
    t = a;        // creates [0:7] entry
    t[3] = b[0]; // splits [0:7]: [0:2] and [4:7] share the same handle
    t = c;        // replaces [0:7]: iterates both shared-handle entries
  end
  assign out = t;
endmodule
)";
  NetlistTest test(tree);
  // The final t=c overwrites all previous drivers; only c reaches out.
  CHECK(test.pathExists("m.c", "m.out"));
  CHECK_FALSE(test.pathExists("m.a", "m.out"));
}

TEST_CASE("Inverted bounds when new range contains existing with same "
          "upper edge (merge)",
          "[ValueTracker]") {
  // When merging if/else branches where one drives t[7:4] and the other t[7:0],
  // the "new bounds contains existing" merge path incremented bounds.left to 8
  // after consuming the [4:7] entry, which exceeded bounds.right (7) and caused
  // an IntervalMap insertion with an inverted key.
  auto const &tree = R"(
module m (input logic [7:0] a, b, input logic c, output logic [7:0] out);
  logic [7:0] t;
  always_comb begin
    if (c)
      t[7:4] = a[3:0];
    else
      t[7:0] = b;
  end
  assign out = t;
endmodule
)";
  NetlistTest test(tree);
  // Bits [7:4] can be driven from either branch; bits [3:0] only from else.
  CHECK(test.getDrivers("m.t", {7, 4}).size() == 2);
  CHECK(test.getDrivers("m.t", {3, 0}).size() == 1);
  CHECK(test.pathExists("m.a", "m.out"));
  CHECK(test.pathExists("m.b", "m.out"));
}

TEST_CASE("Range fully consumed after contains-left overlap (replace)",
          "[ValueTracker]") {
  // When replacing drivers, the "new contains existing" path adjusts
  // bounds.left past the existing entry. If the consumed entry aligns
  // exactly with the right edge, bounds.left > bounds.right and we
  // should return early.
  auto const &tree = R"(
module m(input logic [3:0] a, b, output logic [3:0] out);
  logic [3:0] t;
  always_comb begin
    t[3:2] = a[1:0];
    t[3:0] = b;
  end
  assign out = t;
endmodule
)";
  NetlistTest test(tree);
  // After t[3:0] = b, only b drives all of t.
  CHECK(test.getDrivers("m.t", {3, 0}).size() == 1);
  CHECK(test.pathExists("m.b", "m.out"));
  CHECK_FALSE(test.pathExists("m.a", "m.out"));
}

TEST_CASE("Range fully consumed after left-overlap (replace)",
          "[ValueTracker]") {
  // The left-overlap replace path adjusts bounds.left. If the consumed
  // entry's upper edge equals bounds.right, we hit the early return
  // early return.
  auto const &tree = R"(
module m(input logic [3:0] a, b, output logic [3:0] out);
  logic [3:0] t;
  always_comb begin
    t[3:0] = a;
    t[3:1] = b[2:0];
  end
  assign out = t;
endmodule
)";
  NetlistTest test(tree);
  // Bit 0 driven by a, bits [3:1] driven by b.
  CHECK(test.getDrivers("m.t", {0, 0}).size() == 1);
  CHECK(test.getDrivers("m.t", {3, 1}).size() == 1);
}

TEST_CASE("Inverted bounds when new range left-overlaps existing with "
          "same upper edge (merge)",
          "[ValueTracker]") {
  // When merging if/else branches where one drives t[7:0] and the other t[7:3],
  // the "left-overlap" path incremented bounds.left to 8 after consuming the
  // existing [0:7] entry, which exceeded bounds.right (7) and caused an
  // IntervalMap insertion with an inverted key.
  auto const &tree = R"(
module m (input logic [7:0] a, b, input logic c, output logic [7:0] out);
  logic [7:0] t;
  always_comb begin
    if (c)
      t[7:0] = a;
    else
      t[7:3] = b[4:0];
  end
  assign out = t;
endmodule
)";
  NetlistTest test(tree);
  // Bits [7:3] can be driven from either branch; bits [2:0] only from if.
  CHECK(test.getDrivers("m.t", {7, 3}).size() == 2);
  CHECK(test.getDrivers("m.t", {2, 0}).size() == 1);
  CHECK(test.pathExists("m.a", "m.out"));
  CHECK(test.pathExists("m.b", "m.out"));
}
