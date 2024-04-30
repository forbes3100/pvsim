// Verilog compiler test -- wires

`timescale 1 ns / 100 ps

module main;
	reg ErrFlag;

	reg			Reg1;
	reg [1:0]	Vec1;
	wire		Wire1, Wire2;
	wire [1:0]	WVec1, WVec2;
	tri			Bus, BusU, BusD;
	tri [1:0]	VBus, VBusU, VBusD;
	tri [1:0]	SVBus, SVBusU, SVBusD;

	pullup   (BusU);
	pulldown (BusD);
	pullup   (VBusU);
	pulldown (VBusD);
	pullup   (SVBusU);
	pulldown (SVBusD);

	assign Wire1 = 1'b1;
	assign Wire2 = #10 ~Wire2;
	assign WVec1 = 2'h3;
	//assign WVec2 = #8 WVec2 - 2'b1;
	assign WVec2 = ~Vec1;

	wire Wire3 = (Wire2 ^ Vec1[1]);

	always @(posedge Wire2) begin
		Reg1 <= ~Reg1;
		Vec1 <= Vec1 + 2'd1;
		$display("Wire2 event");
	end

	wire OE = #2 Wire2;

	assign Bus  = OE ? Reg1 : 1'hz;
	assign BusU = OE ? Reg1 : 1'hz;
	assign BusD = OE ? Reg1 : 1'hz;

	assign VBus  = OE ? Vec1 : 2'hz;
	assign VBusU = OE ? Vec1 : 2'hz;
	assign VBusD = OE ? Vec1 : 2'hz;

	assign SVBus[0]  =  OE ? Vec1[0] : 1'hz;
	assign SVBus[1]  = ~OE ? Vec1[1] : 1'hz;
	assign SVBusU[0] =  OE ? Vec1[0] : 1'hz;
	assign SVBusU[1] = ~OE ? Vec1[1] : 1'hz;
	assign SVBusD[0] =  OE ? Vec1[0] : 1'hz;
	assign SVBusD[1] = ~OE ? Vec1[1] : 1'hz;

	initial begin
		$barClock(Wire2);
		$timeformat(-9, -10, " ns", 6);

		Vec1 <= 2'b10;
		#0.9;
		Vec1 <= Vec1 - 2'b1;

		// 1 ns
		$display("%t: Reg1   = %h (0)", $time, Reg1);
		$display("%t: Vec1   = %h (2)", $time, Vec1);
		$display("%t: Wire1  = %h (1)", $time, Wire1);
		$display("%t: Wire2  = %h (0)", $time, Wire2);
		$display("%t: WVec1  = %h (3)", $time, WVec1);
		$display("%t: WVec2  = %h (1)", $time, WVec2);
		$display("%t: Wire3  = %h (1)", $time, Wire3);
		#1;
		// 2 ns
		$display("%t: Reg1   = %h (0)", $time, Reg1);
		$display("%t: Vec1   = %h (1)", $time, Vec1);
		$display("%t: Wire1  = %h (1)", $time, Wire1);
		$display("%t: Wire2  = %h (0)", $time, Wire2);
		$display("%t: WVec1  = %h (3)", $time, WVec1);
		$display("%t: WVec2  = %h (2)", $time, WVec2);
		$display("%t: Wire3  = %h (0)", $time, Wire3);
		#14;
		// 16 ns
		$display("%t: Reg1   = %h (1)", $time, Reg1);
		$display("%t: Vec1   = %h (2)", $time, Vec1);
		$display("%t: Wire1  = %h (1)", $time, Wire1);
		$display("%t: Wire2  = %h (1)", $time, Wire2);
		$display("%t: WVec1  = %h (3)", $time, WVec1);
		$display("%t: WVec2  = %h (1)", $time, WVec2);
		$display("%t: Wire3  = %h (0)", $time, Wire3);
		$display("%t: BusU   = %h (1)", $time, BusU);
		$display("%t: BusD   = %h (1)", $time, BusD);
		$display("%t: VBusU  = %h (2)", $time, VBusU);
		$display("%t: VBusD  = %h (2)", $time, VBusD);
		$display("%t: SVBusU = %h (2)", $time, SVBusU);
		$display("%t: SVBusD = %h (2)", $time, SVBusD);
		#12;
		// 28 ns
		$display("%t: Reg1   = %h (1)", $time, Reg1);
		$display("%t: Vec1   = %h (2)", $time, Vec1);
		$display("%t: Wire1  = %h (1)", $time, Wire1);
		$display("%t: Wire2  = %h (0)", $time, Wire2);
		$display("%t: WVec1  = %h (3)", $time, WVec1);
		$display("%t: WVec2  = %h (1)", $time, WVec2);
		$display("%t: Wire3  = %h (1)", $time, Wire3);
		$display("%t: BusU   = %h (1)", $time, BusU);
		$display("%t: BusD   = %h (0)", $time, BusD);
		$display("%t: VBusU  = %h (3)", $time, VBusU);
		$display("%t: VBusD  = %h (0)", $time, VBusD);
		$display("%t: SVBusU = %h (3)", $time, SVBusU);
		$display("%t: SVBusD = %h (2)", $time, SVBusD);
		#10;
		// 38 ns
		$display("%t: Reg1   = %h (0)", $time, Reg1);
		$display("%t: Vec1   = %h (3)", $time, Vec1);
		$display("%t: Wire1  = %h (1)", $time, Wire1);
		$display("%t: Wire2  = %h (1)", $time, Wire2);
		$display("%t: WVec1  = %h (3)", $time, WVec1);
		$display("%t: WVec2  = %h (0)", $time, WVec2);
		$display("%t: Wire3  = %h (0)", $time, Wire3);
		$display("%t: BusU   = %h (0)", $time, BusU);
		$display("%t: BusD   = %h (0)", $time, BusD);
		$display("%t: VBusU  = %h (3)", $time, VBusU);
		$display("%t: VBusD  = %h (3)", $time, VBusD);
		$display("%t: SVBusU = %h (3)", $time, SVBusU);
		$display("%t: SVBusD = %h (1)", $time, SVBusD);
		#10;
		// 48 ns
		$display("%t: Reg1   = %h (0)", $time, Reg1);
		$display("%t: Vec1   = %h (3)", $time, Vec1);
		$display("%t: Wire1  = %h (1)", $time, Wire1);
		$display("%t: Wire2  = %h (0)", $time, Wire2);
		$display("%t: WVec1  = %h (3)", $time, WVec1);
		$display("%t: WVec2  = %h (0)", $time, WVec2);
		$display("%t: Wire3  = %h (1)", $time, Wire3);
		$display("%t: BusU   = %h (1)", $time, BusU);
		$display("%t: BusD   = %h (0)", $time, BusD);
		$display("%t: VBusU  = %h (3)", $time, VBusU);
		$display("%t: VBusD  = %h (0)", $time, VBusD);
		$display("%t: SVBusU = %h (3)", $time, SVBusU);
		$display("%t: SVBusD = %h (2)", $time, SVBusD);

		#10;
		$display("<done>");
	end
endmodule
