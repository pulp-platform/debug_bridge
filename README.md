# Introduction

This is a GDB server implementation that can be used to connect GDB to a target containing a Ri5CY core.

# What is supported

This bridge can be used to connect either to the RTL simulator or the FPGA.

# Build

## PULPino on ZedBoard
Run the following command to build it for a ZYNQ based board that is running Linux on the ARM cores:

    make pulpino=1

Note that an ARM compiler must be configured as this bridge will run on the ARM side of the board.

## PULPino/PULP RTL simulation

Run the following command to build it for the RTL simulator:

    make

In both cases, the build will produce an executable called debug_bridge.

The following instructions can be followed to build GDB for RISCV: https://github.com/riscv/riscv-gnu-toolchain

# Usage

First the platform must be running.

Then the bridge must be launched. On ZYNQ, it must be run on the ARM side as a classic application. On the RTL simulator, it must be run on the workstation.

In both cases, the bridge will listen for incoming connections on port 1234.

Then gdb can be launched with the following command:

     riscv32-unknown-elf-gdb -ex "target remote :1234"

Adjust the target address if you are running remote on a ZedBoard or a remote machine is running ModelSim.

Starting from here, classic gdb commands can be used.
