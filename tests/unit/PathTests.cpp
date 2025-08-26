#include "Test.hpp"

TEST_CASE("Path through passthrough module") {
  auto &tree = (R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)");
  NetlistTest test(tree);
  PathFinder pathFinder(test.netlist);
  auto *start = test.netlist.lookup("m.a");
  auto *end = test.netlist.lookup("m.b");
  CHECK(pathFinder.find(*start, *end).size() == 3);
}

TEST_CASE("Merge two control paths assigning to different parts of a vector") {
  auto &tree = (R"(
module m(input logic a,
         input logic b,
         input logic c,
         output logic x,
         output logic y);
  logic [1:0] t;
  always_comb
    if (a) begin
      t[0] = b;
    end else begin
      t[1] = c;
    end
  assign x =  t[0];
  assign y =  t[1];
endmodule
  )");
  NetlistTest test(tree);
  PathFinder pathFinder(test.netlist);
  CHECK(
      !pathFinder.find(*test.netlist.lookup("m.b"), *test.netlist.lookup("m.x"))
           .empty());
  CHECK(
      !pathFinder.find(*test.netlist.lookup("m.c"), *test.netlist.lookup("m.y"))
           .empty());
}

TEST_CASE("Merge two control paths assigning to the same part of a vector") {
  auto &tree = (R"(
module m(input logic a,
         input logic b,
         input logic c,
         output logic x);
  logic [1:0] t;
  always_comb
    if (a) begin
      t[1] = b;
    end else begin
      t[1] = c;
    end
  assign x =  t[1];
endmodule
  )");
  NetlistTest test(tree);
  PathFinder pathFinder(test.netlist);
  CHECK(
      !pathFinder.find(*test.netlist.lookup("m.b"), *test.netlist.lookup("m.x"))
           .empty());
  CHECK(
      !pathFinder.find(*test.netlist.lookup("m.c"), *test.netlist.lookup("m.x"))
           .empty());
}

TEST_CASE("Merge two control paths assigning to overlapping of a vector") {
  auto &tree = (R"(
module m(input logic a,
         input logic b,
         input logic c,
         input logic d,
         output logic x,
         output logic y,
         output logic z);
  logic [2:0] t;
  always_comb
    if (a) begin
      t[0] = d;
      t[1] = b;
    end else begin
      t[1] = c;
      t[2] = d;
    end
  assign x =  t[0];
  assign y =  t[1];
  assign z =  t[2];
endmodule
  )");
  NetlistTest test(tree);
  PathFinder pathFinder(test.netlist);
  CHECK(
      !pathFinder.find(*test.netlist.lookup("m.a"), *test.netlist.lookup("m.x"))
           .empty());
  CHECK(
      !pathFinder.find(*test.netlist.lookup("m.b"), *test.netlist.lookup("m.y"))
           .empty());
  CHECK(
      !pathFinder.find(*test.netlist.lookup("m.c"), *test.netlist.lookup("m.y"))
           .empty());
  CHECK(
      !pathFinder.find(*test.netlist.lookup("m.d"), *test.netlist.lookup("m.z"))
           .empty());
}
