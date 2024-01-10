#! /bin/bash

ar -M <<EOM
    OPEN systolicArraySim.a
    ADDLIB netlist/obj_dir/VSystolicArray_netlist__ALL.a
    SAVE
    END
EOM
ranlib systolicArraySim.a
