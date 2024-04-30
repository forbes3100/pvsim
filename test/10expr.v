// Verilog compiler test -- expressions

`timescale 1 ns / 100 ps

module main;
    reg ErrFlag;
    reg [5:0] Var1;
    reg [12:0] Var2;

    initial begin
        $display("lit dec int = %d (0)", 0);
        $display("lit hex 32b = %h (0)", 32'h0);
        $display("lit bin 32b = %h (0)", 32'b0);
        $display("lit dec int = %d (1)", 1);
        $display("lit hex 1b  = %h (1)", 1'h1);
        $display("lit bin 1b  = %h (1)", 1'b1);
        $display("lit dec int = %d (12345678)", 12345678);
        $display("lit hex 32b = %h (12345678)", 32'h12345678);
        $display("lit bin 32b = %h (12345678)", 32'b00010010001101000101011001111000);
        $display("lit dec int = %d (-1)", -1);
        $display("lit hex 32b = %h (ffffffff)", 32'hffffffff);

        $display("add int = %d (12345678)", 12340000 + 5678);
        $display("sub int = %d (12345678)", 12346789 - 1111);
        $display("mul int = %d (7006652)", 1234 * 5678);
        $display("div int = %d (2173)", 12340000 / 5678);
        $display("sla int = %d (1024)", 1 << 10);
        $display("sra int = %d (8)", 64 >> 3);
        $display("eq int = %d (0)", 1234 == 1235);
        $display("eq int = %d (1)", 1234 == 1234);
        $display("ne int = %d (0)", 1234 != 1234);
        $display("ne int = %d (1)", 1234 != 1235);
        $display("gt int = %d (0)", 1234 > 1234);
        $display("gt int = %d (1)", 1234 > 1233);
        $display("le int = %d (0)", 1234 <= 1233);
        $display("le int = %d (1)", 1234 <= 1234);
        $display("lt int = %d (0)", 1234 < 1234);
        $display("lt int = %d (1)", 1234 < 1235);
        $display("ge int = %d (0)", 1234 >= 1235);
        $display("ge int = %d (1)", 1234 >= 1234);

        $display("not 1b = %h (0)", !1'b1);
        $display("not 1b = %h (1)", !1'b0);
        $display("com 1b = %h (0)", ~1'b1);
        $display("com 1b = %h (1)", ~1'b0);
        $display("and 1b = %h (0)", 1'b0 & 1'b1);
        $display("and 1b = %h (1)", 1'b1 & 1'b1);
        $display("or  1b = %h (0)", 1'b0 | 1'b0);
        $display("or  1b = %h (1)", 1'b1 | 1'b0);
        $display("xor 1b = %h (0)", 1'b1 ^ 1'b1);
        $display("xor 1b = %h (1)", 1'b1 ^ 1'b0);
        $display("not 16b = %h (0)", !16'h1234);
        $display("not 16b = %h (1)", !16'h0000);
        $display("com 16b = %h (1234)", ~16'hedcb);
        $display("and 16b = %h (1234)", 16'hff34 & 16'h12ff);
        $display("or  16b = %h (1234)", 16'h0034 | 16'h1200);
        $display("xor 16b = %h (1234)", 16'h0035 ^ 16'h1201);
        $display("add 2b = %h (3)", 2'b01 + 2'b10);
        $display("sub 2b = %h (1)", 2'b11 - 2'b10);
        $display("add 32b = %h (12345678)", 32'h12340000 + 32'h5678);
        $display("sub 32b = %h (12345678)", 32'h12346789 - 32'h1111);
        $display("mul 16b = %h (1230)", 16'h0123 * 16'h0010);
        $display("div 16b = %h (123)", 16'h1234 / 16'h0010);
        $display("sla 16b = %h (12340000)", 16'h1234 << 16);
        $display("sra 16b = %h (123)", 16'h1234 >> 4);
        $display("eq 16b = %h (0)", 16'h1234 == 16'h1235);
        $display("eq 16b = %h (1)", 16'h1234 == 16'h1234);
        $display("ne 16b = %h (0)", 16'h1234 != 16'h1234);
        $display("ne 16b = %h (1)", 16'h1234 != 16'h1235);

        $display("expr int = %d (12345678)", (1264 - 30) * 10000 + 56780 / 10);
        $display("expr 1b = %h (1)", (1'b1 & 1'b1) | ~1'b1);
        $display("expr 32b = %h (12345678)", ((16'h1264 - 'h30) << 16) + (20'h56780 >> 4));
        $display("expr mix = %h (1234000)", 16'h1234 * 4096);
        $display("expr mix = %h (12340000)", 'h1234 * 65536);
        $display("expr mix = %h (12345678)", ('h1264 - 'h30) * (1 << 16) + 'h56780 / 'h10);
        Var1 <= 'h12;
        #0.01;
        Var2 <= 2048 * {7'b0, Var1} / 64;
        //Var2 <= 2048 * Var1 / 64;
        #0.01;
        $display("expr mix = %h (240)", Var2);

        $display("cat = %h (12345678)", { 20'h12345, 4'h6, 7'b0111100, 1'b0 });
        $display("uand 16b = %h (0)", (& 16'h1234));
        $display("uand 16b = %h (1)", (& 16'hffff));
        $display("uor  16b = %h (0)", (| 16'h0000));
        $display("uor  16b = %h (1)", (| 16'h1234));
        $display("uxor 16b = %h (0)", (^ 16'h1233));
        $display("uxor 16b = %h (1)", (^ 16'h1234));

        $display("sel int = %d (12345678)", 1 ? 12345678 : 42);
        $display("sel 32b = %h (12345678)", 1'b1 ? 32'h12345678 : 32'h42);
        //$display("sel expr = %h (42000000)", 0 ? 32'h12345678 : 8'h42 << 24);
        $display("sel expr = %h (deadbeef)", 0 ? 32'h12345678 : {16'hdead, 16'hbeef});
        $display("sel nest = %h (deadbeef)", 1'b0 ? 32'h12345678 :
                                            1'b1 ? {16'hdead, 16'hbeef} : 32'h01010101);

        $display("<done>");
    end
endmodule
