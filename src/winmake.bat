g++ -O3 -fshort-enums -c EvalSignal.cc
g++ -O3 -fshort-enums -c ModelPCode.cc
g++ -O3 -fshort-enums -c PVSimMain.cc
g++ -O3 -fshort-enums -c SimPalSrc.cc
g++ -O3 -fshort-enums -c Simulator.cc
g++ -O3 -fshort-enums -c Src.cc
g++ -O3 -fshort-enums -c Utils.cc
g++ -O3 -fshort-enums -c Version.cc
g++ -O3 -fshort-enums -c VLCoderPCode.cc
g++ -O3 -fshort-enums -c VLCompiler.cc
g++ -O3 -fshort-enums -c VLExpr.cc
g++ -O3 -fshort-enums -c VLInstance.cc
g++ -O3 -fshort-enums -c VLModule.cc
g++ -O3 -fshort-enums -c VLSysLib.cc

g++ -static EvalSignal.o ModelPCode.o PVSimMain.o SimPalSrc.o ^
  Simulator.o Src.o Utils.o Version.o VLCoderPCode.o VLCompiler.o ^
  VLExpr.o VLInstance.o VLModule.o VLSysLib.o -o ../pvsimu.exe
