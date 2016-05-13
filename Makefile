CXXFLAGS=-std=c++0x -g
SRCS = debug_if.cpp breakpoints.cpp rsp.cpp cache.cpp bridge.cpp

CXX=g++
SRCS += sim.cpp
ifdef pulpemu
	CXXFLAGS+=-DFPGA -DPULPEMU
	CXX=arm-linux-gnueabihf-g++
	SRCS += mem_zynq_apb_spi.cpp
endif
ifdef pulpino
	CXXFLAGS+=-DFPGA
	CXX=arm-xilinx-linux-gnueabi-g++
	SRCS += mem_zynq_spi.cpp
endif
ifndef pulpino
ifndef pulpemu
	CXX=g++
	SRCS += sim.cpp
endif
endif

EXE_SRCS = main.cpp $(SRCS)
LIB_SRCS = $(SRCS)

ifdef GEN_LIB
all: debug_bridge libdebugbridge.so
else
all: debug_bridge
endif

clean:
	rm -f ./*.o
	rm -f ./debug_bridge
	rm -f ./libdebugbridge.so

debug_bridge: $(EXE_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

libdebugbridge.so: $(LIB_SRCS)
	$(CXX) $(CXXFLAGS) -g -O3 -fPIC -shared -o $@ $^

ifdef pulpino
push: debug_bridge
	scp ./debug_bridge $(FPGA_HOSTNAME):/root/
endif

sdk:
	make clean all GEN_LIB=1
	mkdir -p $(PULP_SDK_HOME)/install/ws/lib
	mkdir -p $(PULP_SDK_HOME)/install/ws/include
	cp *.h $(PULP_SDK_HOME)/install/ws/include
	cp debug_bridge $(PULP_SDK_HOME)/install/ws/bin
	cp libdebugbridge.so $(PULP_SDK_HOME)/install/ws/lib
