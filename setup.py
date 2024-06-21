"""
py2app build script for Mac OSX PVSim.app

Usage (Mac OS X):
    python setup.py py2app

(Windows build uses pyinstaller instead of py2exe)
"""

import sys
from setuptools import setup

import pvsim

mainscript = "pvsim.py"

# grab version and copyright info from source
version = pvsim.__version__
copyright = pvsim.__copyright__

# Define dependencies
dependencies = ["wxPython", "pypubsub", "pvsimu"]

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
    version=version,
    ext_modules=[],
    install_requires=dependencies,
    **extra_options
)
