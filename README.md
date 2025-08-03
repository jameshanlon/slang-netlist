[![Build and test](https://github.com/jameshanlon/slang-netlist/actions/workflows/build.yml/badge.svg)](https://github.com/jameshanlon/slang-netlist/actions/workflows/build.yml)
[![Documentation](https://github.com/jameshanlon/slang-netlist/actions/workflows/docs.yml/badge.svg)](https://github.com/jameshanlon/slang-netlist/actions/workflows/docs.yml)

# Slang netlist

> **Warning**
>
> slang-netlist is a work in progress and may not work as expected. If you
> encounter a problem, please submit a bug report via Issues.

slang-netlist is tool built on top of [slang](https://sv-lang.com) for
analysing the source-level static connectivity of a SystemVerilog design.
This can be useful to develop structural checks or investigate timing paths,
for example, rather than having to use synthesis to obtain a gate-level netlist.
slang-netlist can be used as a C++ library, a Python module or as a command-line
tool.

Using an example of a simple ripple-carry adder:

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

The ``slang-netlist`` command-line tool can be used to trace paths through the
design, such as:

```
➜  slang-netlist adder.sv --from adder.i_op0 --to adder.o_sum
... # TODO
```

## Installation and documenation

For more information, please see the
[documentation](https://jameshanlon.com/slang-netlist).

## License

slang-netlist is licensed under the MIT license. See [LICENSE](LICENSE) for details.
