python setup_ext.py build --force

g++ -mno-cygwin -shared -s ^
  build\temp.win32-2.7\Release\src\evalsignal.o ^
  build\temp.win32-2.7\Release\src\modelpcode.o ^
  build\temp.win32-2.7\Release\src\pvsimextension.o ^
  build\temp.win32-2.7\Release\src\simpalsrc.o ^
  build\temp.win32-2.7\Release\src\simulator.o ^
  build\temp.win32-2.7\Release\src\src.o ^
  build\temp.win32-2.7\Release\src\utils.o ^
  build\temp.win32-2.7\Release\src\vlcoderpcode.o ^
  build\temp.win32-2.7\Release\src\vlcompiler.o ^
  build\temp.win32-2.7\Release\src\vlexpr.o ^
  build\temp.win32-2.7\Release\src\vlinstance.o ^
  build\temp.win32-2.7\Release\src\vlmodule.o ^
  build\temp.win32-2.7\Release\src\vlsyslib.o ^
  build\temp.win32-2.7\Release\src\version.o ^
  build\temp.win32-2.7\Release\src\pvsimu.def ^
  -LC:\Python27\libs -LC:\Python27\PCbuild -lpython27 ^
  -o build\lib.win32-2.7\pvsimu.pyd

python setup_ext.py install
