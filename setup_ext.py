import setuptools
from pybind11.setup_helpers import Pybind11Extension, build_ext

# grab extension version from source
version = "None"
for line in open("src/Version.cc").readlines():
    if "ersion =" in line:
        version = line.split("=")[1].strip().replace('"', '').replace(';', '')
        break

ext_modules = [
    Pybind11Extension(
        "pvsimu",
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
        extra_compile_args = ["-fshort-enums"],
    ),
]

setuptools.setup(
    name = "pvsimu",
    version = version,
    author = "Scott Forbes",
    url = "http://github.com/pvsim/",
    description = "Verilog Simulator",
    long_description=open("README.md").read(),
    long_description_content_type="text/markdown",
    ext_modules = ext_modules,
    cmdclass={"build_ext": build_ext},
    packages=setuptools.find_packages(where="src"),
    package_dir={"": "src"},
    python_requires=">=3.6",
    setup_requires=["pybind11>=2.5.0", "setuptools>=42", "wheel"]
)
