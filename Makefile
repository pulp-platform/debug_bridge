

CXXFLAGS=
SRCS = main.cpp debug_if.cpp breakpoints.cpp rsp.cpp cache.cpp

ifdef fpga
	CXXFLAGS+=-DFPGA
	CXX=arm-xilinx-linux-gnueabi-g++
	SRCS += mem_zynq_spi.cpp mem_zynq_apb_spi.cpp
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
