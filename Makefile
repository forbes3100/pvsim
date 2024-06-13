#
# Top-Level Makefile for PVSim Verilog Simulator
#

# note: Python 3.12 has removed Unicode_GET_SIZE needed by wxPython 4.2.0
# note: Python 3.11 doesn't bring the app to the front (work around it?)
#PYTHON = python3.10

# Use the virtual environment's Python interpreter
PYTHON = venv/bin/python

# set SUDO = sudo if installing the Python extension requires it
OSTYPE = $(shell uname)
ifeq ($(OSTYPE),Linux)
	SUDO =
else
	SUDO = sudo
endif

all: activate pvsimu_ext

# Ensure the virtual environment is activated
activate:
	. venv/bin/activate

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
PVSim_Dev.app/Contents/MacOS/python:
	ln -s `which python3` PVSim_Dev.app/Contents/MacOS/python

# backend, as a Python extension
pvsimu_ext: PVSim_Dev.app/Contents/MacOS/python
	$(PYTHON) setup_ext.py build -g install --prefix=$(shell $(PYTHON) -c 'import site; print(site.getsitepackages()[0])')

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

.PHONY: activate pvsimu_ext bdist sdist clean distclean
