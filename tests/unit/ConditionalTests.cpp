#include "Test.hpp"

#include <set>
#include <unordered_map>

TEST_CASE("If statement with else branch assigning constants",
          "[Conditionals]") {
  auto const &tree = (R"(
module m(input logic a, output logic b);
  always_comb begin
    if (a) begin
      b = 1;
    end else begin
      b = 0;
    end
  end
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Conditional"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="Merge"]
  N1 -> N3 [label="a[0]"]
  N3 -> N4
  N3 -> N5
  N4 -> N6
  N4 -> N2 [label="b[0]"]
  N5 -> N6
  N5 -> N2 [label="b[0]"]
}
)");
}

TEST_CASE("If statement with else branch assigning variables",
          "[Conditionals]") {
  auto const &tree = (R"(
module m(input logic a, input logic b, input logic c, output logic d);
  always_comb
    if (a) begin
      d = b;
    end else begin
      d = c;
    end
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.d"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK(test.pathExists("m.c", "m.d"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="In port b"]
  N3 [label="In port c"]
  N4 [label="Out port d"]
  N5 [label="Conditional"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Merge"]
  N1 -> N5 [label="a[0]"]
  N2 -> N6 [label="b[0]"]
  N3 -> N7 [label="c[0]"]
  N5 -> N6
  N5 -> N7
  N6 -> N8
  N6 -> N4 [label="d[0]"]
  N7 -> N8
  N7 -> N4 [label="d[0]"]
}
)");
}

TEST_CASE("Ternary operator in continuous assignment", "[Conditionals]") {
  auto const &tree = (R"(
module m(input logic a, input logic b, input logic ctrl, output logic c);
  assign c = ctrl ? a : b;
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.c"));
  CHECK(test.pathExists("m.ctrl", "m.c"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="In port b"]
  N3 [label="In port ctrl"]
  N4 [label="Out port c"]
  N5 [label="Assignment"]
  N1 -> N5 [label="a[0]"]
  N2 -> N5 [label="b[0]"]
  N3 -> N5 [label="ctrl[0]"]
  N5 -> N4 [label="c[0]"]
}
)");
}

TEST_CASE("If statement assignments carry branch predicates",
          "[Conditionals]") {
  auto const &tree = (R"(
module m(input logic a, input logic b, input logic c, output logic d);
  always_comb begin
    if (a)
      d = b;
    else
      d = c;
  end
endmodule
)");
  const NetlistTest test(tree);

  std::vector<BranchPredicate> predicates;
  for (auto const &node : test.graph.filterNodes(NodeKind::Assignment)) {
    auto const &assignment = node->as<Assignment>();
    if (assignment.branchPredicate) {
      predicates.push_back(*assignment.branchPredicate);
    }
  }
  REQUIRE(predicates.size() == 2);
  std::sort(predicates.begin(), predicates.end(),
            [](auto const &left, auto const &right) {
              return left.kind < right.kind;
            });
  CHECK(predicates[0].kind == "falsy");
  CHECK(predicates[0].signal == "m.a");
  CHECK(predicates[0].bitWidth == 1);
  CHECK(predicates[1].kind == "truthy");
  CHECK(predicates[1].signal == "m.a");
  CHECK(predicates[1].bitWidth == 1);
}

TEST_CASE("Repeated instances keep branch predicates instance-local",
          "[Conditionals]") {
  auto const &tree = (R"(
module child(input logic sel, input logic a, input logic b, output logic y);
  always_comb begin
    if (sel)
      y = a;
    else
      y = b;
  end
endmodule

module top(input logic sel0, sel1, a0, b0, a1, b1,
           output logic y0, y1);
  child u0(.sel(sel0), .a(a0), .b(b0), .y(y0));
  child u1(.sel(sel1), .a(a1), .b(b1), .y(y1));
endmodule
)");
  const NetlistTest test(tree, /*parallel=*/false,
                         /*materializeInternalVariables=*/true);

  std::unordered_map<std::string, std::set<std::string>> predicatesByOutput;
  for (auto const &node : test.graph.filterNodes(NodeKind::Assignment)) {
    auto const &assignment = node->as<Assignment>();
    if (!assignment.branchPredicate) {
      continue;
    }
    for (auto const &edge : assignment.getOutEdges()) {
      if (!edge->symbol.empty()) {
        predicatesByOutput[edge->symbol.hierarchicalPath].insert(
            assignment.branchPredicate->signal);
      }
    }
  }

  REQUIRE(predicatesByOutput.contains("top.u0.y"));
  REQUIRE(predicatesByOutput.contains("top.u1.y"));
  CHECK(predicatesByOutput.at("top.u0.y") == std::set<std::string>{"top.u0.sel"});
  CHECK(predicatesByOutput.at("top.u1.y") == std::set<std::string>{"top.u1.sel"});
}

TEST_CASE("Selected branch predicates carry bounds", "[Conditionals]") {
  auto const &tree = (R"(
module m(input logic [3:0] sel, input logic a, input logic b, output logic y);
  always_comb begin
    if (sel[0])
      y = a;
    else if (sel[2:1] == 2'b10)
      y = b;
    else
      y = 1'b0;
  end
endmodule
)");
  const NetlistTest test(tree);

  std::vector<BranchPredicate> predicates;
  for (auto const &node : test.graph.filterNodes(NodeKind::Assignment)) {
    auto const &assignment = node->as<Assignment>();
    if (assignment.branchPredicate) {
      predicates.push_back(*assignment.branchPredicate);
    }
  }

  auto bitPredicate = std::ranges::find_if(predicates, [](auto const &item) {
    return item.kind == "truthy" && item.signal == "m.sel" && item.bounds &&
           item.bounds->lower() == 0 && item.bounds->upper() == 0;
  });
  REQUIRE(bitPredicate != predicates.end());
  CHECK(bitPredicate->bitWidth == 4);

  auto partPredicate = std::ranges::find_if(predicates, [](auto const &item) {
    if (item.op != "and" || item.terms.size() != 2) {
      return false;
    }
    return std::ranges::any_of(item.terms, [](auto const &term) {
      return term.kind == "equals" && term.signal == "m.sel" &&
             term.bounds && term.bounds->lower() == 1 &&
             term.bounds->upper() == 2 && term.values == std::vector<std::string>{"10"};
    });
  });
  CHECK(partPredicate != predicates.end());
}

TEST_CASE("Ternary expression data edges carry branch predicates",
          "[Conditionals]") {
  auto const &tree = (R"(
module m(input logic a, input logic b, input logic ctrl, output logic c);
  assign c = ctrl ? a : b;
endmodule
)");
  const NetlistTest test(tree);

  std::unordered_map<std::string, BranchPredicate> predicatesBySignal;
  for (auto const &node : test.graph) {
    for (auto const &edge : node->getOutEdges()) {
      if (edge->branchPredicate) {
        predicatesBySignal.emplace(edge->symbol.hierarchicalPath,
                                   *edge->branchPredicate);
      }
    }
  }

  REQUIRE(predicatesBySignal.contains("m.a"));
  REQUIRE(predicatesBySignal.contains("m.b"));
  CHECK(predicatesBySignal.at("m.a").kind == "truthy");
  CHECK(predicatesBySignal.at("m.a").signal == "m.ctrl");
  CHECK(predicatesBySignal.at("m.a").bitWidth == 1);
  CHECK(predicatesBySignal.at("m.b").kind == "falsy");
  CHECK(predicatesBySignal.at("m.b").signal == "m.ctrl");
  CHECK(predicatesBySignal.at("m.b").bitWidth == 1);
}

TEST_CASE("Four-way case statement", "[Conditionals]") {
  auto const &tree = (R"(
module m(input logic [1:0] a, output logic b);
  always_comb
    case (a)
      2'b00: b = 0;
      2'b01: b = 1;
      2'b10: b = 2;
      2'b11: b = 3;
    endcase
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Case"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="Merge"]
  N7 [label="Assignment"]
  N8 [label="Merge"]
  N9 [label="Assignment"]
  N10 [label="Merge"]
  N1 -> N3 [label="a[1:0]"]
  N3 -> N4
  N3 -> N5
  N3 -> N7
  N3 -> N9
  N4 -> N6
  N4 -> N2 [label="b[0]"]
  N5 -> N6
  N5 -> N2 [label="b[0]"]
  N6 -> N8
  N7 -> N8
  N7 -> N2 [label="b[0]"]
  N8 -> N10
  N9 -> N10
  N9 -> N2 [label="b[0]"]
}
)");
}

TEST_CASE("Casez statements carry priority branch predicates",
          "[Conditionals]") {
  auto const &tree = (R"(
module m(input logic [1:0] sel, input logic a, input logic b, input logic d,
         output logic y);
  always_comb begin
    casez (sel)
      2'b1?: y = a;
      2'b?1: y = b;
      default: y = d;
    endcase
  end
endmodule
)");
  const NetlistTest test(tree);

  std::vector<BranchPredicate> predicates;
  for (auto const &node : test.graph.filterNodes(NodeKind::Assignment)) {
    auto const &assignment = node->as<Assignment>();
    if (assignment.branchPredicate) {
      predicates.push_back(*assignment.branchPredicate);
    }
  }

  REQUIRE(predicates.size() == 3);
  CHECK(predicates[0].kind == "casez_match");
  CHECK(predicates[0].signal == "m.sel");
  CHECK(predicates[0].value == "10");
  CHECK(predicates[0].mask == "10");

  CHECK(predicates[1].op == "and");
  REQUIRE(predicates[1].terms.size() == 2);
  CHECK(predicates[1].terms[0].kind == "casez_default");
  REQUIRE(predicates[1].terms[0].excluded.size() == 1);
  CHECK(predicates[1].terms[0].excluded[0].value == "10");
  CHECK(predicates[1].terms[0].excluded[0].mask == "10");
  CHECK(predicates[1].terms[1].kind == "casez_match");
  CHECK(predicates[1].terms[1].value == "01");
  CHECK(predicates[1].terms[1].mask == "01");

  CHECK(predicates[2].kind == "casez_default");
  REQUIRE(predicates[2].excluded.size() == 2);
  CHECK(predicates[2].excluded[0].value == "10");
  CHECK(predicates[2].excluded[1].value == "01");
}

TEST_CASE("Variable is not assigned on all control paths (else)", "[Netlist]") {
  auto const &tree = (R"(
module m(input logic a, output logic y);
  logic t;
  always_comb begin
    if (a) t = 1;
  end
  assign y = t;
endmodule
  )");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.y"));
}

TEST_CASE("Variable is not assigned on all control paths (then)", "[Netlist]") {
  auto const &tree = (R"(
module m(input logic a, output logic y);
  logic t;
  always_comb begin
    if (a) begin end
    else t = 1;
  end
  assign y = t;
endmodule
  )");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.y"));
}

TEST_CASE("Unreachable assignment is ignored in data flow analysis",
          "[Netlist]") {
  auto const &tree = (R"(
module m(input logic a, input logic b, output logic y);
  logic t;
  always_comb begin
    if (0) t = a;
    else   t = b;
  end
  assign y = t;
endmodule
  )");
  const NetlistTest test(tree);
  // Only b should be a valid path to y, a should not.
  CHECK(!test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.b", "m.y"));
}

TEST_CASE("Merge two control paths assigning to different parts of a vector",
          "[Conditional]") {
  auto const &tree = (R"(
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
  const NetlistTest test(tree);
  // Both b and c should be valid paths to y.
  CHECK(test.pathExists("m.b", "m.x"));
  CHECK(test.pathExists("m.c", "m.y"));
}

TEST_CASE("Merge two control paths assigning to the same part of a vector",
          "[Conditional]") {
  auto const &tree = (R"(
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
  const NetlistTest test(tree);
  // Both b and c should be valid paths to x.
  CHECK(test.pathExists("m.b", "m.x"));
  CHECK(test.pathExists("m.c", "m.x"));
}

TEST_CASE("Merge two control paths assigning to overlapping of a vector",
          "[Netlist]") {
  auto const &tree = (R"(
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
  const NetlistTest test(tree);
  // Both b and c should be valid paths to y.
  CHECK(test.pathExists("m.a", "m.x"));
  CHECK(test.pathExists("m.b", "m.y"));
  CHECK(test.pathExists("m.c", "m.y"));
  CHECK(test.pathExists("m.d", "m.z"));
}

TEST_CASE("Nested conditionals assigning variables", "[Netlist]") {
  // Test that the variables in multiple nested levels of conditions are
  // correctly added as dependencies of the output variable.
  auto const &tree = R"(
 module m(input a, input b, input c, input sel_a, input sel_b, output reg f);
  always @(*) begin
    if (sel_a == 1'b0) begin
      if (sel_b == 1'b0)
        f = a;
      else
        f = b;
    end else begin
      f = c;
    end
  end
 endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.f"));
  CHECK(test.pathExists("m.b", "m.f"));
  CHECK(test.pathExists("m.c", "m.f"));
  CHECK(test.pathExists("m.sel_a", "m.f"));
  CHECK(test.pathExists("m.sel_b", "m.f"));
}

TEST_CASE("Constant condition eliminates branch", "[Conditionals]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  always_comb begin
    if (1)
      b = a;
    else
      b = 0;
  end
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  // No Conditional node should be created for constant conditions.
  auto conditionals = test.graph.filterNodes(NodeKind::Conditional);
  int count = 0;
  for (auto &n : conditionals) {
    (void)n;
    count++;
  }
  CHECK(count == 0);
}
