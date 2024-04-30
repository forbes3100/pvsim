// Verilog compiler test -- control flow

`timescale 1 ns / 100 ps

module main;
    reg ErrFlag;

    integer     Int1, Int2;
    reg [1:0]   Vec1;

    initial begin
        Int1 = 1;
        Vec1 <= 2'h2;
        #1;

        if (Int1)
            $display("if Int1 = 1 (1)");
        else
            $display("if Int1 = 0 (1)");
        if (Vec1 == 0)
            $display("if Vec1 = 1 (0)");
        else
            $display("if Vec1 = 0 (0)");

        repeat (2) begin
            Vec1 <= Vec1 - 2'b1;
            #2;
        end
        $display("repeat Vec1 = %h (0)", Vec1);

        while (Vec1 < 3) begin
            Vec1 <= Vec1 + 2'b1;
            #2;
        end
        $display("while Vec1 = %h (3)", Vec1);

        Int2 = 10;
        #1;
        for (Int1 = 1; Int1 < 4; Int1 = Int1 + 1) begin
            Int2 = Int2 - 1;
            #2;
        end
        $display("for Int2 = %h (7)", Int2);

        $display("<done>");
    end
endmodule
