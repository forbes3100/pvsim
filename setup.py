"""
py2app build script for Mac OSX PVSim.app

Usage (Mac OS X):
    python setup.py py2app

(Windows build uses pyinstaller instead of py2exe)
"""

from setuptools import setup, Extension
import sys

mainscript = "pvsim.py"

# Grab version and copyright info from source
version = "None"
copyright = "None"
for line in open(mainscript).readlines():
    if line.startswith("# Copyright"):
        copyright = line.strip().split(" ", 1)[1]
    elif line.startswith("guiVersion ="):
        version = line.split("=")[1].strip().replace('"', '')

# Define the extension module
pvsimu_module = Extension("pvsimu",
                          sources=[
                              "src/EvalSignal.cc",
                              "src/ModelPCode.cc",
                              "src/PVSimExtension.cc",
                              "src/SimPalSrc.cc",
                              "src/Simulator.cc",
                              "src/Src.cc",
                              "src/Utils.cc",
                              "src/VLCoderPCode.cc",
                              "src/VLCompiler.cc",
                              "src/VLExpr.cc",
                              "src/VLInstance.cc",
                              "src/VLModule.cc",
                              "src/VLSysLib.cc",
                              "src/Version.cc"],
                          define_macros=[("EXTENSION", None)],
                          extra_compile_args=["-fshort-enums"])

if sys.platform == "darwin":
    extra_options = dict(
        setup_requires=["py2app"],
        app=[mainscript],
        options=dict(py2app=dict(
            iconfile="rsrc/PVSim_app.icns",
            includes=["pvsimu"],
            packages=["pvsimu"],  # Ensure pvsimu is included
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
    ext_modules=[pvsimu_module],
    **extra_options
)
