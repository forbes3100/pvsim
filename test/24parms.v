// Verilog compiler	test --	module instantiation

`timescale 1 ns	/ 100 ps

module sub (input Clk);

	parameter	SUB		= 25;
	parameter	SUB2	= SUB /	2;
	parameter	SUB3	= -5;
	parameter	SUB4	= 12.5234;

	initial	begin
		$display("t0: SUB  = %d (6)", SUB);
		$display("t0: SUB2 = %d (3)", SUB2);
		$display("t0: SUB3 = %d (-5)", SUB3);
		$display("t0: SUB4 = [%t] ([12.5ns])", SUB4);
		#1;
		$display("t1: SUB  = %d (6)", SUB);
		$display("t1: SUB2 = %d (3)", SUB2);
	end

    reg Clk4;
    always #SUB4 Clk4 = ~Clk4;
endmodule

module main;
	reg	ErrFlag;

	parameter	Param1		= 4,
				Param2		= 2'b10,
				Param3		= 3	* Param2;

	reg			Clk;

	// instantiate a module	and	change its parameter
	defparam s.SUB = 6;
	sub	s(.Clk(Clk));

	initial	begin
		Clk	<= 1;
		// units=-9=1ns, precision=1, suffix, min_field_wid
		$timeformat(-9, 1, "ns", 1);
		#10;
		$display("Param1 = %d (4)",	Param1);
		$display("Param2 = %h (2)",	Param2);
		$display("Param3 = %d (6)",	Param3);
		$display("Param1*3 = %d (12)", Param1 *	3);
		$display("Param2<<16 = %h (20000)",	Param2 << 16);
		Clk	<= 0;
		#10;
		$display("<done>");
	end
endmodule
