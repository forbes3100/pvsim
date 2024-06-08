#
# Top-Level Makefile for PVSim Verilog Simulator
#

# note: Python 3.12 has removed Unicode_GET_SIZE needed by wxPython 4.2.0
PYTHON = python3.10

# set SUDO = sudo if installing the Python extension requires it
OSTYPE = $(shell uname)
ifeq ($(OSTYPE),Linux)
	SUDO =
else
	SUDO = sudo
endif

all: pvsimu_ext

VERSLINE = $(shell grep gPSVersion src/Version.cc)
VERS1 = $(filter "%";,$(VERSLINE))
VERS = $(patsubst "%";,%,$(VERS1))

FORCE:

# backend, as a command-line program
pvsimu: FORCE
	(cd src; make)
	mv src/pvsimu pvsimu

# run all command-line tests on pvsimu
testu: pvsimu
	(cd test; make)

# create a soft link for Python_Dev.app to the current python
PVSim_Dev.app/Contents/MacOS/$(PYTHON):
	ln -s `which python3` PVSim_Dev.app/Contents/MacOS/$(PYTHON)

# backend, as a Python extension
pvsimu_ext: PVSim_Dev.app/Contents/MacOS/$(PYTHON)
	$(PYTHON) setup_ext.py build -g install --user

# OSX binary distribution: PVSim.app
bdist:
	mkdir -p dist
	(cd dist; /bin/rm -rf PVSim.app)
	$(PYTHON) setup.py py2app
	(cd dist; zip -r pvsim-$(VERS).macosx.zip PVSim.app)

# source distribution
sdist:
	$(PYTHON) setup_ext.py sdist

clean:
	(cd src; make clean)
	(cd test; /bin/rm -rf *.log *.events *.mif)

distclean: clean
	(cd src; make distclean)
	/bin/rm -rf build dist distribute-* *.pyc pvsimu
