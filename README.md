[![Build and test](https://github.com/jameshanlon/slang-netlist/actions/workflows/build.yml/badge.svg)](https://github.com/jameshanlon/slang-netlist/actions/workflows/build.yml)
[![Documentation](https://github.com/jameshanlon/slang-netlist/actions/workflows/docs.yml/badge.svg)](https://github.com/jameshanlon/slang-netlist/actions/workflows/docs.yml)

# Slang netlist

> **Warning**
>
> slang-netlist is a work in progress and may not work as expected. If you
> encounter a problem, please submit a bug report via
> [Issues](https://github.com/jameshanlon/slang-netlist/issues).

slang-netlist is built on top of [slang](https://sv-lang.com) for analysing the
source-level static connectivity of a SystemVerilog design. It uses slang's
data-flow analyses to construct a dependency graph of operations within a
design, and provides facilities for interacting with this data structure.
slang-netlist is written as a C++ library and provides a command-line tool for
interactive use, and a Python module for straightforward integration into
scripts.

Compared with standard front-end EDA tools such as Synopsys Verdi and Spyglass,
Netlist Paths is oriented towards command-line use for exploration of a design
(rather than with a GUI), and for integration with Python infrastructure (rather
than TCL) to build tools for analysing or debugging a design.

By focusing on source-level connectivity it is lightweight and will run faster
than standard tools to perform a comparable task, whilst also being open source
and unrestricted by licensing issues. Applications include critical timing path
investigation, creation of unit tests for design structure and connectivity,
and development of patterns for quality-of-result reporting.

## Features

- Represenation of bit-level variable dependencies.
- Representation of procedural dependencies with evaluation of constant-valued
  conditions and unrolling of loops with constant bounds.
- Command-line tool.
- Python bindings.

## Example

Here's an example of using the command line tool to trace a path in a
ripple-carry adder:

```
➜  cat tests/driver/rca.sv
module rca
  #(parameter WIDTH = 8)
  (input  logic               i_clk,
   input  logic               i_rst,
   input  logic [WIDTH-1:0] i_op0,
   input  logic [WIDTH-1:0] i_op1,
   output logic [WIDTH-1:0] o_sum,
   output logic              o_co);

  logic [WIDTH-1:0] carry;
  logic [WIDTH-1:0] sum;

  assign carry[0] = 1'b0;
  assign {o_co, o_sum} = {co[WIDTH-1], sum};

  for (genvar i = 0; i < WIDTH - 1; i++) begin
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
  end
```

Specifying start and end points for a path, the  ``slang-netlist`` tool searches
for paths between these points and returns information about a path, if it finds
one.

```
➜  slang-netlist adder.sv --from adder.i_op0 --to adder.o_sum
tests/driver/rca.sv:6:31: note: input port i_op1
   input  logic [p_width-1:0] i_op1,
                              ^
tests/driver/rca.sv:6:31: note: symbol i_op1[0:0]
   input  logic [p_width-1:0] i_op1,
                              ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: symbol carry[1:1]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: symbol carry[2:2]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: symbol carry[3:3]
  logic [p_width-1:0] carry;
                      ^
...
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: symbol carry[7:7]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:28:7: note: assignment
      co_q  <= carry[p_width-1];
      ^
tests/driver/rca.sv:13:23: note: symbol co_q[0:0]
  logic               co_q;
                      ^
tests/driver/rca.sv:16:10: note: assignment
  assign {o_co, o_sum} = {co_q, sum_q};
         ^
tests/driver/rca.sv:7:31: note: symbol o_sum[0:7]
   output logic [p_width-1:0] o_sum,
                              ^
tests/driver/rca.sv:7:31: note: output port o_sum
   output logic [p_width-1:0] o_sum,
```

## Related projects

- [Slang](https://github.com/MikePopoloski/slang) 'SystemVerilog compiler and
  language services' is the main library this project depends upon to provide
  access to an elaborated AST with facilities for code evaluation and data flow
  analysis.

- [Netlist Paths](https://github.com/jameshanlon/netlist-paths) is a previous
  iteration of this project, but it instead used Verilator to provide access an
  elaborated AST. This approach had limitations in the way variable selections
  were represented, making it possible only to trace dependencies between named
  variables.

## License

slang-netlist is licensed under the MIT license. See [LICENSE](LICENSE) for
details.
