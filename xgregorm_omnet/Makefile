# xgregorm: preklad projektu OMNeT++ / Veins. Potrebne premenne prostredia: OMNeT++ (source setenv) a VEINS_HOME.
VEINS_HOME ?= $(HOME)/omnet/veins-veins-5.2

all: src/Makefile
	$(MAKE) -C src MODE=release

src/Makefile: src/*.ned src/*.msg src/*.cc src/*.h
	cd src && opp_makemake -f --deep -o xgregorm_omnet -O out -I$(VEINS_HOME)/src -I../../src/automotive/model/Applications -L$(VEINS_HOME)/src -lveins\$$\(D\)

clean:
	-$(MAKE) -C src clean
	rm -f src/Makefile
