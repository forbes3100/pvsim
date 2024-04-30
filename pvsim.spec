# -*- mode: python -*-

# pyinstaller-2.0 build spec to create PVSim.exe

a = Analysis(['pvsim.py'],
             pathex=[])

pyz = PYZ(a.pure)

data = a.datas + [
            ('example1.psim', 'examples/example1.psim', 'DATA'),
            ('example1.v', 'examples/example1.v', 'DATA'),
             ]

exe = EXE(pyz,
          a.scripts,
          a.binaries,
          a.zipfiles,
          data,
          name='dist/PVSim.exe',
          debug=False,    # True to see error messages when launching .exe
          strip=False,
          icon='rsrc/PVSim_app_icon128.ico', 
          upx=True,
          console=False)

app = BUNDLE(exe,
             name='dist/PVSim.exe.app')
