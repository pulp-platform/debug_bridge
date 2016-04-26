

SRCS = main.cpp debug_if.cpp

ifdef fpga
	CXX=arm-xilinx-linux-gnueabi-g++
	SRCS += fpga.cpp
else
	CXX=g++
	SRCS += sim.cpp
endif


all: debug_bridge

clean:
	rm -f ./*.o
	rm -f ./debug_bridge

debug_bridge: $(SRCS)
	$(CXX) -o $@ $^

ifdef '$(fpga)'
push: debug_bridge
	scp ./debug_bridge $(FPGA_HOSTNAME):/root/
endif
