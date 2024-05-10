"""
py2app build script for Mac OSX PVSim.app

Usage (Mac OS X):
    python setup.py py2app

(Windows build uses pyinstaller instead of py2exe)
"""

import sys
from setuptools import setup

mainscript = "pvsim.py"

# grab version and copyright info from source
version = "None"
copyright = "None"
for line in open(mainscript).readlines():
    if line.startswith("# Copyright"):
        copyright = line.strip().split(" ", 1)[1]
    elif line.startswith("guiVersion ="):
        version = line.split("=")[1].strip().replace('"', '')

if sys.platform == "darwin":
    extra_options = dict(
        setup_requires=["py2app"],
        app=[mainscript],
        options=dict(py2app=dict(
            iconfile="rsrc/PVSim_app.icns",
            includes=["pvsimu"],
            excludes=["numpy", "PIL"],  # Exclude numpy and PIL explicitly
            plist=dict(
                CFBundleGetInfoString="Verilog Simulator",
                CFBundleIdentifier="net.sf.pvsim",
                CFBundleVersion=version,
                NSHumanReadableCopyright=copyright,
            ),
            resources=["examples/example1.psim",
                       "examples/example1.v"],
        )),
    )
elif sys.platform == "win32":
    extra_options = dict(
        setup_requires=["pyinstaller"],
    )
else:
    extra_options = dict(
        scripts=[mainscript],
    )

setup(
    name="PVSim",
    ext_modules=[],
    **extra_options
)
