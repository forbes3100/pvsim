; -- pvsim_exe.iss --

[Setup]
AppName=PVSim
AppVersion=6.0.2
DefaultDirName={pf}\PVSim
DefaultGroupName=PVSim
; UninstallDisplayIcon={app}\MyProg.exe
Compression=lzma2
SolidCompression=yes
OutputDir=Output

[Files]
Source: "dist\PVSim.exe"; DestDir: "{app}"
Source: "README.rst.txt"; DestDir: "{app}"; Flags: isreadme

[Icons]
Name: "{group}\PVSim"; Filename: "{app}\PVSim.exe"
