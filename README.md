# HDFIT.SystolicArray

This repository is part of the [Hardware Design Fault Injection Toolkit (HDFIT)](https://github.com/IntelLabs/HDFIT). HDFIT enables end-to-end fault injection experiments and comprises additionally [HDFIT.NetlistFaultInjector](https://github.com/IntelLabs/HDFIT.NetlistFaultInjector) and [HDFIT.ScriptsHPC](https://github.com/IntelLabs/HDFIT.ScriptsHPC).

<p align="center" width="100%">
    <img src="HDFIT.png" alt="HDFIT HPC Toolchain" width="80%"/>
</p>

This repository provides a System Verilog systolic array implementation with a test bench, plus the interface "systolicArraySim.a" between the cycle-based simulation of the former and high-level matrix multiplication invocations in OpenBLAS. To this end, this repository also contains an 'openblas' make target, which clones OpenBLAS, applies a git-patch to implement the fault injection interface and compiles the OpenBLAS library (see openblas folder).

## Dependencies
When using Ubuntu, please install these dependencies by hand as Ubuntu versions are more than two years old.
* [sv2v](https://github.com/zachjs/sv2v) (no need to install, compiling is sufficient). Tested with v0.0.10-1-gc00f508.
* [Yosys](https://github.com/YosysHQ/yosys). Tested with v0.20+22.
* [Verilator](https://www.veripool.org/verilator/). Clone from [Github](https://github.com/verilator/verilator). Tested with v4.225.
* [HDFIT.NetlistFaultInjector](https://github.com/IntelLabs/HDFIT.NetlistFaultInjector)

## Compiling
* In the Makefile, set VERILATOR_TOP and NETLIST_FAULT_INJECTOR_TOP on top of the file to the correct locations (and compile netlistFaultInjector!).
* In sv2v.sh and sv2v_fma.sh, it is assumed that the sv2v command can be found via PATH.
* 'make testNetlist && ./testNetlist' to run unit tests.
* 'make systolicArraySim.a' to generate the library used as HDFIT RTL fault simulation interface.
