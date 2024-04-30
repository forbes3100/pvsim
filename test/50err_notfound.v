// Verilog compiler test -- Error: not found

`timescale 1 ns / 10 ps

module main;
    reg ErrFlag;

    initial begin
        $display("<experr>'badmod' not found</experr>");
        badmod.var1 = 1;
    end
endmodule
