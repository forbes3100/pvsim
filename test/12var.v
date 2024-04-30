// Verilog compiler test -- variables

`timescale 1 ns / 100 ps

module main;
    reg ErrFlag;

    integer     Int1, Int2;
    reg         Reg1, Reg2;
    reg [1:0]   Vec1, Vec2;
    reg [1:0]   Mem1[0:1];

    initial begin
        Int1 = 5678;
        Int2 = 12340000;
        Reg1 <= 0;
        Reg2 <= 1'b1;
        Vec1 <= 2'b10;
        Vec2 <= 1 + 2;
        Mem1[0] <= 3;
        Mem1[1] <= 1;
        #1;
        $display("Int1 = %d (5678)", Int1);
        $display("Int2 = %d (12340000)", Int2);
        $display("Reg1 = %h (0)", Reg1);
        $display("Reg2 = %h (1)", Reg2);
        $display("Vec1 = %h (2)", Vec1);
        $display("Vec2 = %h (3)", Vec2);
        $display("Mem1[0] = %h (3)", Mem1[0]);
        $display("Mem1[1] = %h (1)", Mem1[1]);
        $display("expr int = %d (12345678)", Int1 + Int2);
        $display("expr 1b = %h (1)", Reg1 | Reg2);
        $display("expr vec = %h (4)", (Vec2 - Vec1) << 2);
        $display("expr mem = %h (8)", (Mem1[0] - Mem1[1]) << 2);
        $display("expr mix = %h (0)",
            (Int2 != 0) ? {Reg1, ~Reg2} : Vec1 + Mem1[1]);
        $display("expr mix = %h (3)",
            (Int2 == 0) ? {Reg1, ~Reg2} : Vec1 + Mem1[1]);
        
        $display("<done>");
    end

endmodule
