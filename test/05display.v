// Verilog compiler test -- display, strings

`timescale 1 ns / 100 ps

module main;
    reg ErrFlag;

    initial begin
        $display("display1 = 12345678 (12345678)");
        $display("display2 = abc_DEF (abc_DEF)");

        $display("<done>");
    end
endmodule
