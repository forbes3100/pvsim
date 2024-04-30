// Verilog compiler test -- preprocessor

`timescale 1 ns / 10 ps

`define FALSE   0
`define TRUE    1
// comment
`define Def_1  (42 + `TRUE)

module main;
    reg ErrFlag;

    initial begin
        $display("FALSE = %h (0)", `FALSE);
        $display("TRUE = %h (1)", `TRUE
            );
        $display("Def1 = %d (43)", `Def_1);
`ifdef FALSE
        $display("ifdef FALSE = 1 (1)");
`endif
`ifdef FALSE
        $display("ifdef FALSE = 1 (1)");
`else
        $display("ifdef FALSE = 0 (1)");
`endif
`ifdef NOT_DEFINED
        $display("ifdef NOT_DEFINED = 0 (1)");
`else
        $display("ifdef NOT_DEFINED = 1 (1)");
`ifdef FALSE
        $display("ifdef nested FALSE = 1 (1)");
`else
        $display("ifdef nested FALSE = 0 (1)");
`endif
`endif

        $display("<done>");
    end
endmodule
