import sys
from setuptools import setup, Extension

mainscript = "pvsim.py"

# grab version from source
version = "None"
for line in open(mainscript).readlines():
    if line.startswith("guiVersion ="):
        version = line.split("=")[1].strip().replace('"', '')

module1 = Extension("pvsimu",
                    sources = [
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
                    define_macros = [("EXTENSION", None)],
                    extra_compile_args = ["-fshort-enums"])

if __name__ == "__main__":
    setup (name = "pvsim",
           version = version,
           author = "Scott Forbes",
           url = "http://github.com/pvsim/",
           description = "Verilog Simulator",
           ext_modules = [module1])
