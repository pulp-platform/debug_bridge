CXXFLAGS=
SRCS = debug_if.cpp breakpoints.cpp rsp.cpp cache.cpp bridge.cpp

ifdef fpga
ifdef pulp
	CXXFLAGS+=-DFPGA -DPULPEMU
	CXX=arm-linux-gnueabihf-g++
	SRCS += mem_zynq_apb_spi.cpp
else
	CXXFLAGS+=-DFPGA
	CXX=arm-xilinx-linux-gnueabi-g++
	SRCS += mem_zynq_spi.cpp
endif
else
	CXX=g++
	SRCS += sim.cpp
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

debug_bridge: $(EXE_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

libdebugbridge.so: $(LIB_SRCS)
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ $^

ifdef fpga
push: debug_bridge
	scp ./debug_bridge $(FPGA_HOSTNAME):/root/
endif

sdk:
	make clean all GEN_LIB=1
	mkdir -p $(PULP_SDK_HOME)/install/ws/bin
	cp *.h $(PULP_SDK_HOME)/install/ws/include
	cp debug_bridge $(PULP_SDK_HOME)/install/ws/bin
	cp libdebugbridge.so $(PULP_SDK_HOME)/install/ws/lib