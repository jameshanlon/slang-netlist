[![Build and test](https://github.com/jameshanlon/slang-netlist/actions/workflows/build.yml/badge.svg)](https://github.com/jameshanlon/slang-netlist/actions/workflows/build.yml)
[![Documentation](https://github.com/jameshanlon/slang-netlist/actions/workflows/docs.yml/badge.svg)](https://github.com/jameshanlon/slang-netlist/actions/workflows/docs.yml)
[![codecov](https://codecov.io/gh/jameshanlon/slang-netlist/graph/badge.svg?token=ZLC0ZNECXJ)](https://codecov.io/gh/jameshanlon/slang-netlist)

# Slang Netlist

Slang Netlist is built on top of [slang](https://sv-lang.com) for analysing the
source-level static connectivity of a SystemVerilog design. It uses slang's AST
and data-flow analyses to construct a dependency graph of operations and provides
facilities for interacting with this data structure.

Slang Netlist is a C++ library and provides a command-line tool for interactive
use, and a Python module for straightforward integration into scripts.
Potential applications include timing path investigation, creation of tests for
design structure and connectivity, and checking of structural patterns for
quality-of-result reporting.


## Features

- Data dependencies that are resolved to a bit level.
- Procedural dependencies in always blocks, including evaluation of
  constant-valued conditions and unrolling of static loops.
- Integration with the facilities of slang's libraries.
- A command-line tool for interactive use.
- Python bindings to integrate the tool into scripts.

## Example

Here's how to trace a path in a ripple-carry adder using the command-line tool:

```systemverilog
module rca
  #(parameter WIDTH = 8)
  (input  logic               i_clk,
   input  logic               i_rst,
   input  logic [WIDTH-1:0]   i_op0,
   input  logic [WIDTH-1:0]   i_op1,
   output logic [WIDTH-1:0]   o_sum,
   output logic               o_co);

  logic [WIDTH-1:0] carry;
  logic [WIDTH-1:0] sum;

  assign carry[0] = 1'b0;
  assign {o_co, o_sum} = {co[WIDTH-1], sum};

  for (genvar i = 0; i < WIDTH - 1; i++) begin
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
  end
endmodule
```

Specifying start and end points for a path, ``slang-netlist`` searches for paths
between these points and returns information about a path, if it finds one.

```sh
slang-netlist adder.sv --from rca.i_op0 --to rca.o_sum
```

Example output:

```
tests/driver/rca.sv:6:31: note: input port i_op1
   input  logic [p_width-1:0] i_op1,
                              ^
tests/driver/rca.sv:6:31: note: value rca.i_op1[0:0]
   input  logic [p_width-1:0] i_op1,
                              ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[1:1]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[2:2]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[3:3]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[4:4]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[5:5]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[6:6]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:19:12: note: assignment
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
           ^
tests/driver/rca.sv:10:23: note: value rca.carry[7:7]
  logic [p_width-1:0] carry;
                      ^
tests/driver/rca.sv:28:7: note: assignment
      co_q  <= carry[p_width-1];
      ^
tests/driver/rca.sv:13:23: note: value rca.co_q[0:0]
  logic               co_q;
                      ^
tests/driver/rca.sv:13:23: note: value rca.co_q[0:0]
  logic               co_q;
                      ^
tests/driver/rca.sv:16:10: note: assignment
  assign {o_co, o_sum} = {co_q, sum_q};
         ^
tests/driver/rca.sv:7:31: note: value rca.o_sum[7:0]
   output logic [p_width-1:0] o_sum,
                              ^
tests/driver/rca.sv:7:31: note: output port o_sum
```

## Installation

### Building from source

Prerequisites:
- CMake >= 3.20
- Python 3
- C++20 compiler

```sh
git clone https://github.com/jameshanlon/slang-netlist.git
cd slang-netlist
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_PY_BINDINGS=ON \
    -DCMAKE_INSTALL_PREFIX=$PWD/install
cmake --build build -j --target install
ctest --test-dir build
```

## Related Projects

- [Slang](https://github.com/MikePopoloski/slang) 'SystemVerilog compiler and
  language services' is the main library this project depends upon to provide
  access to an elaborated AST with facilities for code evaluation and data flow
  analysis.

- [Netlist Paths](https://github.com/jameshanlon/netlist-paths) is a previous
  iteration of this project, but it instead used Verilator to provide access an
  elaborated AST. This approach had limitations in the way variable selections
  were represented, making it possible only to trace dependencies between named
  variables.

## Contributing

Contributions are welcome, check the [contributor
guidelines](https://github.com/jameshanlon/slang-netlist/blob/main/CONTRIBUTING.md).

## License

Slang Netlist is licensed under the MIT license. See [LICENSE](LICENSE) for details.
