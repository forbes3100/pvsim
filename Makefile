#
# Top-Level Makefile for PVSim Verilog Simulator
#

# note: Python 3.12 has removed Unicode_GET_SIZE needed by wxPython 4.2.0
# note: Python 3.11 doesn't bring the app to the front (work around it?)
PYTHON = python3.10

# set SUDO = sudo if installing the Python extension requires it
OSTYPE = $(shell uname)
ifeq ($(OSTYPE),Linux)
	SUDO =
else
	SUDO = sudo
endif

all: bdist

PYTHON_VERSION = $(shell $(PYTHON) -c 'import sys; print("cp" + "".join(map(str, sys.version_info[:2])))')
VERSLINE = $(shell grep gPSVersion src/Version.cc)
VERS1 = $(filter "%";,$(VERSLINE))
VERS = $(patsubst "%";,%,$(VERS1))

# Extract system information
OS_NAME1 = $(shell uname -s | tr '[:upper:]' '[:lower:]')
OS_NAME = $(patsubst darwin,macosx,$(OS_NAME1))
OS_MAJOR_VERSION = $(shell sw_vers -productVersion | cut -d. -f1)
ABI_TAG = 0
ARCH = $(shell uname -m)

PVSIMU_WHEEL = dist/pvsimu-$(VERS)-$(PYTHON_VERSION)-$(PYTHON_VERSION)-$(OS_NAME)_$(OS_MAJOR_VERSION)_$(ABI_TAG)_$(ARCH).whl

info_check:
	echo $(PYTHON_VERSION)
	echo $(OS_NAME)
	echo $(OS_MAJOR_VERSION)
	echo $(ABI_TAG)
	echo $(ARCH)
	echo $(PVSIMU_WHEEL)

FORCE:

upgrade:
	$(PYTHON) -m pip install --upgrade pip setuptools pybind11 wheel

# backend, as a command-line program
pvsimu: FORCE
	(cd src; make)
	mv src/pvsimu pvsimu

# run all command-line tests on pvsimu
testu: pvsimu
	(cd test; make)

run:
	open dist/PVSim.app

# create a soft link for Python_Dev.app to the current python
dev: PVSim_Dev.app/Contents/MacOS/python

PVSim_Dev.app/Contents/MacOS/python:
	ln -s `which $(PYTHON)` PVSim_Dev.app/Contents/MacOS/python

# backend, as a Python extension
$(PVSIMU_WHEEL): setup_ext.py src/*.cc src/*.h
	$(PYTHON) -m pip uninstall -y pvsimu
	$(PYTHON) setup_ext.py bdist_wheel
	$(PYTHON) -m pip install $(PVSIMU_WHEEL)

# OSX binary distribution: PVSim.app
dist/PVSim.app: $(PVSIMU_WHEEL) setup.py
	/bin/rm -rf dist/PVSim.app
	$(PYTHON) setup.py py2app

dist/pvsim-$(VERS).macosx.zip: dist/PVSim.app
	cd dist && zip -r pvsim-$(VERS).macosx.zip PVSim.app

# Alias for building the distribution
bdist: dist/pvsim-$(VERS).macosx.zip

# source distribution
sdist:
	$(PYTHON) setup_ext.py sdist

clean:
	(cd src; make clean)
	(cd test; /bin/rm -rf *.log *.events *.mif)

distclean: clean
	(cd src; make distclean)
	/bin/rm -rf __pycache__ build dist distribute-* *.pyc pvsimu
