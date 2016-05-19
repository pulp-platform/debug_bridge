

# Introduction

This is a GDB server implementation that can be used to connect GDB to a target containing a Ri5CY core.

### What is supported:

This bridge can be used to connect either to the RTL simulator or the FPGA.

### Requirements:

In order to debug we need a RISC-V GDB, this debug bridge, and the executable.

# Building the Debug_bridge

The bridge can be build for either the ZYNQ or the RTL-Simulator

### Building the bridge for PULPino on ZedBoard
Run the following command to build it for a ZYNQ based board that is running Linux on the ARM cores:

    make pulpino=1

Note that an ARM compiler must be configured as this bridge will run on the ARM side of the board.
The following command automatically pushes the debug_bridge to the board:

    make pulpino=1 push

### Building the bridge for PULPino RTL-simulation

Run the following command to build it for the RTL simulator:

    make

In both cases, the build will produce an executable called debug_bridge.

# RISC-V GDB

Download the GDB for RISC-V and install it as decribed here: https://github.com/riscv/riscv-gnu-toolchain

# How to debug

On ZYNQ, the debug_bridge must be run on the ARM side as a classic application.
On the RTL-simulator, you first need to start modelsim with

    make {APP_NAME}.vsim

and then you have to source the script "run_memdpi.tcl"

    source tcl_files/run_memdpi.tcl

Now you can start the debug_bridge which establishes the connection to the simulator.

The ZYNQ or the RTL platform are now connected to the debug_bridge and ready to communicate with GDB.

In both cases, the bridge will listen for incoming connections on port 1234.

Then GDB can be launched with the following command:

     riscv32-unknown-elf-gdb -ex "target remote :1234"

Adjust the target address if you are running remote on a ZedBoard or a remote machine is running ModelSim.

Starting from here, classic gdb commands can be used.


## Useful GDB commands

To load a binary:

    load {APPNAME}.elf

To initialize the PC properly: (should be done after loading the binary)

    set $pc=0x80

To insert breakpoints when jumping to main and at address 0x800:

    b main
    b *0x800

To change the layout (display all registers, or source code):

    layout regs
    layout src

To continue to the next breakpoint:

    c

To get the current register values or display all breakpoints:

    info all-registers
    info breakpoints
