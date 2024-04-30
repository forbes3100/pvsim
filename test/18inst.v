// Verilog compiler test -- module instantiation

`timescale 1 ns / 100 ps

`define OLD_STYLE
`ifdef OLD_STYLE
module tmod(
            Clk,
            In,
            InC,
            InV,
            InCV,
            //IO,
            //IOV,
            OutW,
            OutR,
            OutVW,
            OutVR);

input               Clk;
input               In;
input               InC;
input       [1:0]   InV;
input       [1:0]   InCV;
//inout               IO;
//inout       [1:0]   IOV;
output              OutW;
output reg          OutR;
output      [1:0]   OutVW;
output reg  [1:0]   OutVR;

`else
module tmod(
    input               Clk,
    input               In,
    input               InC,
    input       [1:0]   InV,
    input       [1:0]   InCV,
    //inout             IO,
    //inout     [1:0]   IOV,
    output              OutW,
    output reg          OutR,
    output      [1:0]   OutVW,
    output reg  [1:0]   OutVR
    );
`endif

    assign OutW = Clk ^ In ^ InC;
    assign OutVW = {Clk, In} ^ OutVR ^ InV ^ InCV;

    always @(posedge Clk) begin
        OutR <= OutR ^ In;
        OutVR <= OutVR + 2'd1;
    end

    //assign IO = In ? OutW : OutR;
    //assign IOV = In ? OutVW : OutVR;

endmodule
    
module main;
    reg ErrFlag;

    reg         Clk;
    reg         Reg1;
    reg [1:0]   Vec1;

    wire        Wire1 = ~Reg1;
    wire [1:0]  WVec1 = ~Vec1;
    //wire        OutW1;
    wire        OutW2;
    wire        OutR1;
    //wire [1:0]    OutR2;
    wire        OutR2;
    wire [1:0]  OutVW1, OutVW2;
    wire [1:0]  OutVR1;
    wire [3:0]  OutVR2;
    //tri       IO1, IO2;
    //tri [1:0] IOV1, IOV2;

    //assign OutR2[0] = Clk;
    assign OutVR2[3] = 1'b1;
    assign OutVR2[0] = Clk;

    tmod m1 (
        .Clk(   Clk),
        .In(    Wire1),
        .InC(   1'b1),
        .InV(   WVec1),
        .InCV(  2'b10),
        //.IO(  IO1),
        .IOV(  ),
        .OutW(  OutW1),
        .OutR(  OutR1),
        .OutVW( OutVW1),
        .OutVR( OutVR1)
    );

    tmod m2 (
        .Clk(   Clk),
        .In(    WVec1[0]),
        .InC(   1'b0),
        .InV(   WVec1),
        .InCV(  2'b01),
        //.IO(  IO2),
        //.IOV( IOV2),
        .OutW(  OutW2),
        //.OutR(    OutR2[1]),
        .OutR(  OutR2),
        .OutVW( OutVW2),
        .OutVR( OutVR2[2:1])
    );

    initial begin
        $barClock(Clk);
        $timeformat(-9, -10, " ns", 6);

        Clk <= 1;
        Vec1 <= 2'b00;
        Reg1 <= 0;
        #7.9;
        //$display("%t: OutW1  = %h (1)", $time, OutW1);
        $display("%t: OutW2  = %h (0)", $time, OutW2);
        $display("%t: OutR1  = %h (1)", $time, OutR1);
        $display("%t: OutR2  = %h (1)", $time, OutR2);
        $display("%t: OutVW1 = %h (3)", $time, OutVW1);
        $display("%t: OutVW2 = %h (0)", $time, OutVW2);
        $display("%t: OutVR1 = %h (1)", $time, OutVR1);
        $display("%t: OutVR2 = %h (b)", $time, OutVR2);
        //$display("%t: IO1    = %h (0)", $time, IO1);
        //$display("%t: IO2    = %h (0)", $time, IO2);
        //$display("%t: IOV1   = %h (0)", $time, IO1);
        //$display("%t: IOV2   = %h (0)", $time, IO2);

        Clk <= 0;
        Vec1 <= 2'b01;
        Reg1 <= 0;
        #10;
        //$display("%t: OutW1  = %h (0)", $time, OutW1);
        $display("%t: OutW2  = %h (0)", $time, OutW2);
        $display("%t: OutR1  = %h (1)", $time, OutR1);
        $display("%t: OutR2  = %h (1)", $time, OutR2);
        $display("%t: OutVW1 = %h (0)", $time, OutVW1);
        $display("%t: OutVW2 = %h (2)", $time, OutVW2);
        $display("%t: OutVR1 = %h (1)", $time, OutVR1);
        $display("%t: OutVR2 = %h (a)", $time, OutVR2);
        //$display("%t: IO1    = %h (0)", $time, IO1);
        //$display("%t: IO2    = %h (0)", $time, IO2);
        //$display("%t: IOV1   = %h (0)", $time, IO1);
        //$display("%t: IOV2   = %h (0)", $time, IO2);

        Clk <= 1;
        Vec1 <= 2'b11;
        Reg1 <= 1;
        #10;
        //$display("%t: OutW1  = %h (0)", $time, OutW1);
        $display("%t: OutW2  = %h (1)", $time, OutW2);
        $display("%t: OutR1  = %h (0)", $time, OutR1);
        $display("%t: OutR2  = %h (1)", $time, OutR2);
        $display("%t: OutVW1 = %h (2)", $time, OutVW1);
        $display("%t: OutVW2 = %h (1)", $time, OutVW2);
        $display("%t: OutVR1 = %h (2)", $time, OutVR1);
        $display("%t: OutVR2 = %h (d)", $time, OutVR2);
        //$display("%t: IO1    = %h (0)", $time, IO1);
        //$display("%t: IO2    = %h (0)", $time, IO2);
        //$display("%t: IOV1   = %h (0)", $time, IO1);
        //$display("%t: IOV2   = %h (0)", $time, IO2);

        $display("<done>");
    end
endmodule
