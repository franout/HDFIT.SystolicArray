
CXX = g++
CXX_FLAGS= -Wall \
		-W \
		-Wno-sign-compare \
		-Wno-unused-parameter \
		-Werror \
		-O3 \
		-pthread \
		-march=native \
		-std=c++17
		
CXX_FLAGS_VERILATED= -O3 \
		-march=native \
		-std=c++17

VERILATOR_ROOT ?= /usr/share/verilator
VERILATOR_TOP ?= /usr/share/verilator
NETLIST_FAULT_INJECTOR_TOP ?= $(HOME)/HDFIT.NetlistFaultInjector

VERILATOR_INC = -I$(VERILATOR_TOP)/include
VERILATOR_SRC = $(VERILATOR_TOP)/include/verilated.cpp
VERILATOR_SRC_THREADS_SAFE = $(VERILATOR_TOP)/include/verilated_threads.cpp
NETLIST_FAULT_INJECTOR_INC = -I$(NETLIST_FAULT_INJECTOR_TOP)
NETLIST_FAULT_INJECTOR_SRC = $(NETLIST_FAULT_INJECTOR_TOP)/netlistFaultInjector.cpp

VERILATOR_OPTIONS= -Wall -Wno-fatal --x-assign fast --x-initial fast --noassert --clk clk -CFLAGS -fPIC -Wall -Wno-fatal 
VERILATOR_MAKE_OPTIONS='OPT_FAST=-O3 -march=native'

DIR_SYSTOLIC_ARRAY = obj_SA
DIR_FMA = obj_FMA

DIR_SA_NETLIST = netlist
DIR_FMA_NETLIST = netlist_fma

.PHONY: all
all : testNetlist systolicArraySim.a openblas

$(DIR_SYSTOLIC_ARRAY)/VSystolicArray.mk: *.sv
	verilator $(VERILATOR_OPTIONS) -cc -Mdir $(DIR_SYSTOLIC_ARRAY) SystolicArray.sv

$(DIR_SYSTOLIC_ARRAY)/VSystolicArray__ALL.a: $(DIR_SYSTOLIC_ARRAY)/VSystolicArray.mk
	cd $(DIR_SYSTOLIC_ARRAY) && make -j18 $(VERILATOR_MAKE_OPTIONS) -f VSystolicArray.mk

$(DIR_FMA)/VFMA.mk: *.sv
	verilator $(VERILATOR_OPTIONS) -cc -Mdir $(DIR_FMA) FMA.sv	

$(DIR_FMA)/VFMA__ALL.a: $(DIR_FMA)/VFMA.mk
	cd $(DIR_FMA) && make -j18 $(VERILATOR_MAKE_OPTIONS) -f VFMA.mk

helpers.o: helpers.cpp helpers.h
	$(CXX) -c $(CXX_FLAGS) -fPIC $(VERILATOR_INC) helpers.cpp -o helpers.o

systolicArraySim.o : systolicArraySim.cpp systolicArraySim.h $(DIR_SYSTOLIC_ARRAY)/VSystolicArray__ALL.a
	$(CXX) -c $(CXX_FLAGS) -fPIC $(VERILATOR_INC) -I$(DIR_SYSTOLIC_ARRAY) systolicArraySim.cpp

systolicArraySim_netlist.o : systolicArraySim.cpp systolicArraySim.h $(DIR_SA_NETLIST)/obj_dir/VSystolicArray_netlist.mk netlistFaultInjector.o
	$(CXX) -c $(CXX_FLAGS) -fPIC $(VERILATOR_INC) $(NETLIST_FAULT_INJECTOR_INC)  -D NETLIST -I$(DIR_SA_NETLIST)/obj_dir -o systolicArraySim_netlist.o systolicArraySim.cpp

$(DIR_FMA_NETLIST)/FMA.v: *.sv
	mkdir -p $(DIR_FMA_NETLIST) && ./sv2v_fma.sh

$(DIR_FMA_NETLIST)/FMA_netlist.v: $(DIR_FMA_NETLIST)/FMA.v
	cd $(DIR_FMA_NETLIST) &&  yosys -s ../yosys_fma.script

$(DIR_FMA_NETLIST)/obj_dir/VFMA_netlist.mk: $(DIR_FMA_NETLIST)/FMA_netlist.v
	cd $(DIR_FMA_NETLIST) && verilator $(VERILATOR_OPTIONS) -cc FMA_netlist.v

$(DIR_FMA_NETLIST)/obj_dir/VFMA_netlist__ALL.a: $(DIR_FMA_NETLIST)/obj_dir/VFMA_netlist.mk
	cd $(DIR_FMA_NETLIST)/obj_dir && make -j18 $(VERILATOR_MAKE_OPTIONS) -f VFMA_netlist.mk

$(DIR_SA_NETLIST)/SystolicArray.v: *.sv
	mkdir -p $(DIR_SA_NETLIST) && ./sv2v.sh

$(DIR_SA_NETLIST)/SystolicArray_netlist.v: $(DIR_SA_NETLIST)/SystolicArray.v
	cd $(DIR_SA_NETLIST) &&  yosys -s ../yosys.script && $(NETLIST_FAULT_INJECTOR_TOP)/netlistFaultInjector SystolicArray_netlist.v SystolicArray

$(DIR_SA_NETLIST)/obj_dir/VSystolicArray_netlist.mk: $(DIR_SA_NETLIST)/SystolicArray_netlist.v
	cd $(DIR_SA_NETLIST) && verilator $(VERILATOR_OPTIONS) -cc SystolicArray_netlist.v

$(DIR_SA_NETLIST)/obj_dir/VSystolicArray_netlist__ALL.a: $(DIR_SA_NETLIST)/obj_dir/VSystolicArray_netlist.mk
	cd $(DIR_SA_NETLIST)/obj_dir && make -j18 $(VERILATOR_MAKE_OPTIONS) -f VSystolicArray_netlist.mk

netlistFaultInjector.o: $(NETLIST_FAULT_INJECTOR_SRC) $(NETLIST_FAULT_INJECTOR_TOP)/netlistFaultInjector.hpp
	$(CXX) -c $(CXX_FLAGS) -fPIC $(NETLIST_FAULT_INJECTOR_INC) $(NETLIST_FAULT_INJECTOR_SRC) -o netlistFaultInjector.o

$(DIR_SA_NETLIST)/SystolicArrayFiSignals.cpp: $(DIR_SA_NETLIST)/SystolicArray_netlist.v

SystolicArrayFiSignals.o:  $(DIR_SA_NETLIST)/SystolicArrayFiSignals.cpp $(NETLIST_FAULT_INJECTOR_TOP)/netlistFaultInjector.hpp
	$(CXX) -c $(CXX_FLAGS) -fPIC -I. $(NETLIST_FAULT_INJECTOR_INC) $(DIR_SA_NETLIST)/SystolicArrayFiSignals.cpp -o SystolicArrayFiSignals.o

verilated.o : $(VERILATOR_SRC)
	$(CXX) -c $(CXX_FLAGS_VERILATED) -fPIC $(VERILATOR_SRC) -o verilated.o

verilated_threads.o: $(VERILATOR_SRC_THREADS_SAFE)
	$(CXX) -c $(CXX_FLAGS_VERILATED) -fPIC $(VERILATOR_SRC_THREADS_SAFE) -o verilated_threads.o

systolicArraySim.a : verilated.o systolicArraySim_netlist.o helpers.o netlistFaultInjector.o SystolicArrayFiSignals.o $(DIR_SA_NETLIST)/obj_dir/VSystolicArray_netlist__ALL.a verilated_thread.o
	ar r systolicArraySim.a verilated.o systolicArraySim_netlist.o helpers.o netlistFaultInjector.o SystolicArrayFiSignals.o verilated_threads.o
	ranlib systolicArraySim.a
	./addLib.sh # adds VSystolicArray_netlist__ALL.a

test : $(DIR_FMA)/VFMA__ALL.a $(DIR_SYSTOLIC_ARRAY)/VSystolicArray__ALL.a helpers.o systolicArraySim.o verilated.o main.cpp verilated_threads.o
	$(CXX) $(CXX_FLAGS) -I$(DIR_FMA)  $(VERILATOR_INC) main.cpp -o test systolicArraySim.o \
	$(DIR_SYSTOLIC_ARRAY)/VSystolicArray__ALL.a $(DIR_FMA)/VFMA__ALL.a helpers.o verilated.o verilated_threads.o

testNetlist: $(DIR_FMA_NETLIST)/obj_dir/VFMA_netlist__ALL.a $(DIR_SA_NETLIST)/obj_dir/VSystolicArray_netlist__ALL.a helpers.o systolicArraySim_netlist.o netlistFaultInjector.o SystolicArrayFiSignals.o verilated.o main.cpp verilated_threads.o
	$(CXX) $(CXX_FLAGS) -D NETLIST -I$(DIR_FMA_NETLIST)/obj_dir  $(VERILATOR_INC) main.cpp -o testNetlist systolicArraySim_netlist.o \
	$(DIR_SA_NETLIST)/obj_dir/VSystolicArray_netlist__ALL.a $(DIR_FMA_NETLIST)/obj_dir/VFMA_netlist__ALL.a helpers.o netlistFaultInjector.o SystolicArrayFiSignals.o verilated.o verilated_threads.o

openblas: systolicArraySim.a
	cd openblas && make openblas

clean :
	rm -f -r $(DIR_SYSTOLIC_ARRAY) $(DIR_FMA) $(DIR_SA_NETLIST) $(DIR_FMA_NETLIST) ./test ./testNetlist ./mma.a ./*.o systolicArraySim.a
	cd openblas && make clean
