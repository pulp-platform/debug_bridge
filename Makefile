CXXFLAGS=
SRCS = main.cpp debug_if.cpp breakpoints.cpp rsp.cpp cache.cpp

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


all: debug_bridge

clean:
	rm -f ./*.o
	rm -f ./debug_bridge

debug_bridge: $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

ifdef fpga
push: debug_bridge
	scp ./debug_bridge $(FPGA_HOSTNAME):/root/
endif

sdk:
	make clean all
	mkdir -p $(PULP_SDK_HOME)/install/ws/bin
	cp debug_bridge $(PULP_SDK_HOME)/install/ws/bin