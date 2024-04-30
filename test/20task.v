// Verilog compiler test -- tasks, named blocks

`timescale 1 ns / 100 ps

module mod1 (
    input       Clk,
    input       OE_l,
    output      DataW
        );

    reg         DataR;
    wire        OEA_l = 0;
    tri         DataT;
    reg [3:0]   CountM;

    always @(posedge Clk) begin : DataCmpl
                DataR <= ~DataR;
    end

    assign DataW = OEA_l ? 1'bz : DataR;
    assign DataT = OEA_l ? 1'bz : DataR;

    task taskm;
        input Arg1;
        begin
            @(posedge Clk);
            CountM = CountM + 1;
            @(posedge Clk);
            CountM = CountM + 1;
        end
    endtask

endmodule

module main;
        reg ErrFlag;

    reg Clk1, Clk2;
    always #10 begin : Clk1Run
        Clk1 = ~Clk1;
    end

    always #15 Clk2 = ~Clk2;

    tri BusA;
    tri BusB;
    reg OE_l;
    reg [3:0] Count0;
    reg [3:0] Count1;

    task task0;
        begin
            @(posedge Clk1);
            Count0 = Count0 + 1;
            @(posedge Clk1);
            Count0 = Count0 + 1;
        end
    endtask

    task task1;
        input Read;
        begin
            @(posedge Clk1) OE_l <= Read;
            Count1 = Count1 + 1;
            @(posedge Clk1) OE_l <= 1;
            Count1 = Count1 + 1;
        end
    endtask

    mod1 m1 (
                .Clk    (Clk1),
                .OE_l   (OE_l),
                .DataW  (BusA));

    initial begin
        #80;
        task0;
        task0;
    end

    initial begin
        #80;
        task1(0);
        task1(1);
        m1.taskm(1);
    end

    initial begin : Setup
        $barClock(Clk1);
        $timeformat(-9, -10, " ns", 6);

        #84.9; // at 85
        $display("%t: Clk1  = %h (0)", $time, Clk1);
        $display("%t: Clk2  = %h (1)", $time, Clk2);
        $display("%t: DataR = %h (0)", $time, m1.DataR);
        $display("%t: OE_l  = %h (0)", $time, OE_l);
        $display("%t: BusA  = %h (0)", $time, BusA);
        $display("%t: BusB  = %h (0)", $time, BusB);
        $display("%t: Count0 = %h (0)", $time, Count0);
        $display("%t: Count1 = %h (0)", $time, Count1);
        $display("%t: CountM = %h (0)", $time, m1.CountM);

        #15; // at 100
        $display("%t: Clk1  = %h (1)", $time, Clk1);
        $display("%t: Clk2  = %h (0)", $time, Clk2);
        $display("%t: DataR = %h (1)", $time, m1.DataR);
        $display("%t: OE_l  = %h (0)", $time, OE_l);
        $display("%t: BusA  = %h (1)", $time, BusA);
        $display("%t: BusB  = %h (1)", $time, BusB);
        $display("%t: Count0 = %h (1)", $time, Count0);
        $display("%t: Count1 = %h (1)", $time, Count1);
        $display("%t: CountM = %h (0)", $time, m1.CountM);

        OE_l <= 1;
        #15; // at 115
        $display("%t: Clk1  = %h (1)", $time, Clk1);
        $display("%t: Clk2  = %h (1)", $time, Clk2);
        $display("%t: DataR = %h (0)", $time, m1.DataR);
        $display("%t: OE_l  = %h (1)", $time, OE_l);
        $display("%t: BusA  = %h (0)", $time, BusA);
        $display("%t: BusB  = %h (0)", $time, BusB);
        $display("%t: Count0 = %h (2)", $time, Count0);
        $display("%t: Count1 = %h (2)", $time, Count1);
        $display("%t: CountM = %h (0)", $time, m1.CountM);

        #60; // at 175
        $display("%t: Clk1  = %h (1)", $time, Clk1);
        $display("%t: Clk2  = %h (1)", $time, Clk2);
        $display("%t: DataR = %h (1)", $time, m1.DataR);
        $display("%t: OE_l  = %h (1)", $time, OE_l);
        $display("%t: BusA  = %h (1)", $time, BusA);
        $display("%t: BusB  = %h (0)", $time, BusB);
        $display("%t: Count0 = %h (4)", $time, Count0);
        $display("%t: Count1 = %h (4)", $time, Count1);
        $display("%t: CountM = %h (1)", $time, m1.CountM);
        #15;
        OE_l <= 0;
        #200; // at 390
        $display("%t: Clk1  = %h (0)", $time, Clk1);
        $display("%t: Clk2  = %h (1)", $time, Clk2);
        $display("%t: DataR = %h (1)", $time, m1.DataR);
        $display("%t: OE_l  = %h (0)", $time, OE_l);
        $display("%t: BusA  = %h (1)", $time, BusA);
        $display("%t: BusB  = %h (0)", $time, BusB);
        $display("%t: Count0 = %h (4)", $time, Count0);
        $display("%t: Count1 = %h (4)", $time, Count1);
        $display("%t: CountM = %h (2)", $time, m1.CountM);

        $display("<done>");
    end

    assign BusB = OE_l ? 1'bz : Clk1;

endmodule
