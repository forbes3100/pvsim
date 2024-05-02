#
# Makefile for pvsimu Verilog Simulator
#

all: pvsimu

UNAME = uname -s
OS = $(UNAME:sh)$(shell $(UNAME))
#CFLAGS_EXTRA = -D$(OS)

CFLAGS_EXTRA = -fshort-enums

SRC = \
  EvalSignal.cc ModelPCode.cc PVSimMain.cc \
  SimPalSrc.cc Simulator.cc Src.cc Utils.cc \
  Version.cc VLCoderPCode.cc VLCompiler.cc VLExpr.cc \
  VLInstance.cc VLModule.cc VLSysLib.cc

OBJ = $(SRC:.cc=.o)

pvsimu: $(OBJ)
	rm -f Version.o
	$(CXX) $(CXXFLAGS) $(CXXFLAGS_$@) -c Version.cc
	$(CXX) $(OBJ) -o $@

clean:
	rm -f *.o
	rm -f .del-depend

distclean: clean
	rm -f ec .del-* *.E *.asm *.dis

# ----------------- Standard Rules ------------------

.SUFFIXES:  .cc .asm .dis

CXXFLAGS = -g -Wall -O3 $(CFLAGS_EXTRA)

CXX = g++
LD = ld
RM = rm -f

ifeq ($(OS),Darwin)
	DIS = otool -tv
else
	DIS = objdump --disassemble-all --show-raw-insn --source
endif

# generate list of dependencies to be included into Makefile
.del-depend:
	$(CXX) $(CFLAGS) $(CFLAGS_$@) -M $(SRC) > $@

%.o: %.cc
	@ $(RM) $@
	$(CXX) $(CXXFLAGS) $(CXXFLAGS_$@) -c $<

# preprocess only
%.E: %.cc
	@ $(RM) $@
	$(CXX) $(CXXFLAGS) $(CXXFLAGS_$@) -E $< > $@

# disassemble C++ (renames .s to .asm so .s doesn't get assembled)
%.asm: %.cc
	$(CXX) $(CXXFLAGS) $(CXXFLAGS_$@) -S $<
	mv $(*F).s $@

%.nm: %.o
	nm $< > $@

%.U: %.o
	nm $< | grep ' U ' > $@

# disassemble binary and object files
%.dis: %
	$(DIS) $< > $@

%.dis: %.o
	$(DIS) $< > $@

include .del-depend

