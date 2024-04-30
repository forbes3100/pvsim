#!/usr/bin/env python
from __future__ import print_function
###############################################################################
#
#               PVSim Verilog Simulator Test Suite
#
# This is an Python script that compiles and runs all [0-9]*.v files in this
# directory, and checks the output of each by comparing results with expected
# results.
#
# This file is part of PVSim.
#
###############################################################################

import sys, os, string, time, re
import subprocess as sb

pvsim = "../pvsimu"

def reportErr(msg):
    print(59*"+")
    print("+++", msg)
    print(59*"+")

totalErrs = 0

# Gather a list of the .psim files to test
if len(sys.argv) > 1:
    # from either the command line
    vfiles = sys.argv[1:]
else:
    # or all those in the current directory
    testFilePat = re.compile(r"[0-9][0-9].*\.v")
    vfiles = [f for f in os.listdir(".") if testFilePat.match(f)]
    vfiles.sort()

# Send the output to both the screen and a log file

class Logger(object):
    def __init__(self, logFile):
        self.out = sys.stdout
        self.logFile = logFile

    def write(self, s):
        self.logFile.write(s)
        self.out.write(s)

sys.stdout = Logger(file("pvsim_test.log", "w"))

print("Testing PVSim", time.ctime())
print()

resultPat = re.compile(r"^(.+) *= (.+) \(([^)]+)")
expErrPat1 = re.compile(r".*<experr>([^<]+)</experr>.*")
expErrPat2 = re.compile(r"^// ... ERROR.*")
expErrPat3 = re.compile(r"^// ... *(.+)")

# Run a simulation of each Verilog .psim file, and check that resulting output
# lines match their expect values

for vfile in vfiles:
    print(59*"=")
    print("=== TEST", vfile)
    print(59*"=")
    vfilebase, ext = os.path.splitext(vfile)
    pr = sb.Popen([pvsim, "-q", vfilebase + ".psim"],
                  stdout=sb.PIPE, stderr=sb.STDOUT)
    ofile = pr.stdout
    testErrs = 0
    expectErrMsgState = 0
    done = False

    for i, line in enumerate(ofile.readlines()):
        line = line.strip()
        m = resultPat.match(line)
        if m:
            # line is a test result: check it
            what, result, want = m.groups()
            if result != want:
                print(line)
                reportErr("Wrong result: %s wanted '%s', got '%s'" % \
                          (what, want, result))
                testErrs += 1
            else:
                print(line, "OK")
        else:
            print(line)
            m = expErrPat1.match(line)
            if m and expectErrMsgState == 0:
                # an expected error message follows: ignore it if matching
                expectErrMsgState = 1
                want = m.group(1)
                print("Found error message part 1: want=", want)
            else:
                m = expErrPat2.match(line)
                if m and expectErrMsgState == 1:
                    print("Found error message part 2")
                    expectErrMsgState = 2
                else:
                    m = expErrPat3.match(line)
                    if m and expectErrMsgState == 2:
                        result = m.group(1)
                        print("Found error message part 3: result=", result)
                        result = result.strip()
                        if result != want:
                            reportErr("Wrong error message")
                            testErrs += 1
                        done = True
                        break
                    elif line == "<done>":
                        # encountered expected end of file
                        done = True
                        break
    if not done:
        reportErr("End of file")
        testErrs += 1
    ofile.close()
    print("Test done, %d error%s.\n" % \
          (testErrs, ("s", "")[testErrs == 1]))
    totalErrs += testErrs

print(59*"=")
print("==== All test done, %d error%s total." % \
        (totalErrs, ("s", "")[totalErrs == 1]))
print(59*"=")
