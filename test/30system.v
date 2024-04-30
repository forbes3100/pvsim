// Verilog compiler test -- system calls

`timescale 1 ns / 100 ps

module main;
    reg ErrFlag;

    integer oFile;
    reg [31:0] Mem1[0:1];

    initial begin
        // $display(fmt, ...) or $display(net, fmt, ...)
        // $display(net, fmt, ...) translates into either:
        //      $annotate(net, fmt, ...)
        // or if fmt starts with "*** ":
        //      $flagError(net, netName, 0, fmt, ...)
        // $time(): return current simulation time
        $display(" 0.001: $time = [%t] ([                   0])", $time);
        #10;
        $display("10.001: $time t     = [%t] ([                  10])", $time);
        $display("10.001: $time 5t    = [%5t] ([   10])", $time);
        $display("10.001: $time 5.1t  = [%5.1t] ([ 10.0])", $time);
        $display("10.001: $time 9.3t  = [%9.3t] ([   10.001])", $time);
        $timeformat(-9, 1, "", 1);
        $display("10.001: $timef t    = [%t] ([10.0])", $time);
        $display("10.001: $timef 9.3t = [%9.3t] ([   10.001])", $time);
        $timeformat(-9, 1, " ns", 1);
        $display("10.001: $timef ns   = [%9.3t] ([   10.001 ns])", $time);
        $timeformat(-9, 1, " ns", 8);
        $display("10.001: $time 5t ns = [%t] ([ 10.0 ns])", $time);
        #1224.5;
        $display("10.001: #1224.5     = [%t] ([1234.5 ns])", $time);
        $timeformat(-6, 3, " us", 8);
        $display("10.001: $time us    = [%t] ([1.235 us])", $time);

        // $dist_uniform(seed, start, end): random integer between start, end
        $display("$dist_uniform(5678, 0, 100) = %d (39)", $dist_uniform(5678, 0, 100));
        $display("$dist_uniform(5678, 0, 100) = %d (39)", $dist_uniform(5678, 0, 100));
        $display("$dist_uniform(0, 100, 500) = %d (144)", $dist_uniform(0, 100, 500));

        // $random(limit): return a random integer between 0 and limit
        $display("$random(0) = %d (0)", $random(0));
        $display("$random(10) = %d (1)", $random(10));
        $display("$random(10) = %d (6)", $random(10));
        $display("$random(1000000) = %d (81899)", $random(1000000));
        $display("$random(1000000) = %d (681800)", $random(1000000));

        // $fopen(filename, mode)
        $display("Writing file 30system.mif");
        oFile = $fopen("30system.mif");
        $fdisplay(oFile, "-- Temp test file");
        $fdisplay(oFile, "WIDTH=32;");
        $fdisplay(oFile, "DEPTH=%d;", 2);
        $fdisplay(oFile, "ADDRESS_RADIX=HEX;");
        $fdisplay(oFile, "DATA_RADIX=%s;", "HEX");
        $fdisplay(oFile, "CONTENT BEGIN");
        $fdisplay(oFile, "  000 : %08h;", 32'h12345678);
        $fdisplay(oFile, "  %03d : %08h;", 1, 32'hdeadbeef);
        $fdisplay(oFile, "END;");
        $fclose(oFile);

        // $readmemmif("filename", memoryname)
        $display("Reading file 30system.mif");
        $readmemmif("30system.mif", Mem1);
        $display("Mem1[0] = %h (12345678)", Mem1[0]);
        $display("Mem1[1] = %08h (deadbeef)", Mem1[1]);

`ifdef NOTYET
        // $barClock(signal)
        // $debug(code)
        // $bp()
        // $flagError(net, errName, miss, fmt, ...)
        // annotate(signal, fmt, ...)
        // stop(code)
`endif
        $display("<done>");
    end
endmodule
