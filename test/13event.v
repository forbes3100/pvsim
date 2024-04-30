// Verilog compiler test -- events, barClock

`timescale 1 ns / 100 ps

module main;
    reg ErrFlag;

    reg         Clk;
    reg         Reg1, Reg2, Reg3;
    reg [1:0]   Vec1;

    always @(Clk) begin
        $display("Clk change");
        #10;
        Clk <= ~Clk;
    end

    always @(posedge Clk) begin
        Reg1 <= ~Reg1;
        $display("Clk rising");
    end

    always @(negedge Clk) begin
        Reg2 <= ~Reg2;
        $display("Clk falling");
    end

    always @(Reg1 or Reg2) begin
        Vec1 <= Vec1 + 2'b1;
        $display("Reg1 or Reg2 change");
    end

    always @(Vec1) begin
        Reg3 <= ~Reg3;
        $display("Vec1 change");
    end

    initial begin
        $barClock(Clk);
        $timeformat(-9, -10, " ns", 6);
        Clk <= 1;
        Reg1 <= 0;
        Reg2 <= 1;
        Reg3 <= 0;
        Vec1 <= 0;

        #0.9;
        $display("%t: Clk  = %h (1)", $time, Clk);
        $display("%t: Reg1 = %h (1)", $time, Reg1);
        $display("%t: Reg2 = %h (1)", $time, Reg2);
        $display("%t: Reg3 = %h (0)", $time, Reg3);
        $display("%t: Vec1 = %h (2)", $time, Vec1);

        #9;
        $display("%t: Clk  = %h (1)", $time, Clk);
        $display("%t: Reg1 = %h (1)", $time, Reg1);
        $display("%t: Reg2 = %h (1)", $time, Reg2);
        $display("%t: Reg3 = %h (0)", $time, Reg3);
        $display("%t: Vec1 = %h (2)", $time, Vec1);

        #1;
        $display("%t: Clk  = %h (0)", $time, Clk);
        $display("%t: Reg1 = %h (1)", $time, Reg1);
        $display("%t: Reg2 = %h (0)", $time, Reg2);
        $display("%t: Reg3 = %h (1)", $time, Reg3);
        $display("%t: Vec1 = %h (3)", $time, Vec1);

        #10;
        $display("%t: Clk  = %h (1)", $time, Clk);
        $display("%t: Reg1 = %h (0)", $time, Reg1);
        $display("%t: Reg2 = %h (0)", $time, Reg2);
        $display("%t: Reg3 = %h (0)", $time, Reg3);
        $display("%t: Vec1 = %h (0)", $time, Vec1);

        #10;
        $display("%t: Clk  = %h (0)", $time, Clk);
        $display("%t: Reg1 = %h (0)", $time, Reg1);
        $display("%t: Reg2 = %h (1)", $time, Reg2);
        $display("%t: Reg3 = %h (1)", $time, Reg3);
        $display("%t: Vec1 = %h (1)", $time, Vec1);

        #20;
        $display("<done>");
        $stop(0);
    end
endmodule
