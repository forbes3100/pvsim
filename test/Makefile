
all: regression_tests

regression_tests:
	./pvsim_test.py

PVSIM = ../pvsimu

%.log: %.psim %.v ${PVSIM}
	${PVSIM} -q $*.psim

%.debug: %.psim %.v ${PVSIM}
	${PVSIM} -d3 $*.psim

clean:
	/bin/rm -rf *.events *.log 30system.mif
