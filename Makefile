SRCDIR ?= /opt/fpp/src
include ${SRCDIR}/makefiles/common/setup.mk
include $(SRCDIR)/makefiles/platform/*.mk

all: libfpp-net2rf.$(SHLIB_EXT)
debug: all

OBJECTS_fpp_net2rf_so += src/Net2RFPlugin.o
LIBS_fpp_net2rf_so += -L${SRCDIR} -lfpp -ljsoncpp
CXXFLAGS_src/Net2RFPlugin.o += -I${SRCDIR}

%.o: %.cpp src/ZoneLogic.h src/ControllerCheck.h src/Helpers.h Makefile
	$(CCACHE) $(CC) $(CFLAGS) $(CXXFLAGS) $(CXXFLAGS_$@) -c $< -o $@

libfpp-net2rf.$(SHLIB_EXT): $(OBJECTS_fpp_net2rf_so) ${SRCDIR}/libfpp.$(SHLIB_EXT)
	$(CCACHE) $(CC) -shared $(CFLAGS_$@) $(OBJECTS_fpp_net2rf_so) $(LIBS_fpp_net2rf_so) $(LDFLAGS) -o $@

# Host tests for the colour logic, the setup check and the text helpers (no FPP needed; the check needs libjsoncpp-dev): make test
test: test/test_zone_logic.cpp test/test_controller_check.cpp test/test_helpers.cpp src/ZoneLogic.h src/ControllerCheck.h src/Helpers.h
	g++ -std=c++17 -Wall -Wextra -Isrc test/test_zone_logic.cpp -o test/test_zone_logic && ./test/test_zone_logic
	g++ -std=c++17 -Wall -Wextra -Isrc -I/usr/include/jsoncpp test/test_controller_check.cpp -ljsoncpp -o test/test_controller_check && ./test/test_controller_check
	g++ -std=c++17 -Wall -Wextra -Isrc test/test_helpers.cpp -o test/test_helpers && ./test/test_helpers

clean:
	rm -f libfpp-net2rf.so $(OBJECTS_fpp_net2rf_so) test/test_zone_logic test/test_controller_check test/test_helpers
