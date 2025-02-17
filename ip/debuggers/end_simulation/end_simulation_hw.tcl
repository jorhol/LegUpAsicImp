# TCL File Generated by University of Toronto's LegUp group
# DO NOT MODIFY

# +-----------------------------------
# | Specify required package(s) 
# | 
package require -exact qsys 12.0
#package require -exact qsys 13.0
# | 
# +-----------------------------------


# +-----------------------------------
# | module end_simulation
# | 
set_module_property DESCRIPTION "When this core is written to, it will end the simulation"
set_module_property NAME end_simulation
set_module_property VERSION 1.0
set_module_property INTERNAL false
set_module_property OPAQUE_ADDRESS_MAP true
set_module_property GROUP "LegUp/Debuggers"
set_module_property AUTHOR "University of Toronto - LegUp Group"
set_module_property DISPLAY_NAME "End Simulation"
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
set_module_property EDITABLE false
set_module_property ANALYZE_HDL false
set_module_property REPORT_TO_TALKBACK false
set_module_property ALLOW_GREYBOX_GENERATION false
# | 
# +-----------------------------------

# +-----------------------------------
# | file sets
# | 
add_fileset QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL end_simulation
set_fileset_property QUARTUS_SYNTH ENABLE_RELATIVE_INCLUDE_PATHS false
add_fileset_file end_simulation.v VERILOG PATH hdl/end_simulation.v TOP_LEVEL_FILE

add_fileset SIM_VERILOG SIM_VERILOG "" ""
set_fileset_property SIM_VERILOG TOP_LEVEL end_simulation
set_fileset_property SIM_VERILOG ENABLE_RELATIVE_INCLUDE_PATHS false
add_fileset_file end_simulation.v VERILOG PATH hdl/end_simulation.v

add_fileset SIM_VHDL SIM_VHDL "" ""
set_fileset_property SIM_VHDL TOP_LEVEL end_simulation
set_fileset_property SIM_VHDL ENABLE_RELATIVE_INCLUDE_PATHS false
add_fileset_file end_simulation.v VERILOG PATH hdl/end_simulation.v
# | 
# +-----------------------------------

# +-----------------------------------
# | parameters
# | 
# | 
# +-----------------------------------

# +-----------------------------------
# | connection point clock
# | 
add_interface clock clock end
set_interface_property clock clockRate 0
set_interface_property clock ENABLED true
set_interface_property clock EXPORT_OF ""
set_interface_property clock PORT_NAME_MAP ""
set_interface_property clock SVD_ADDRESS_GROUP ""

add_interface_port clock clk clk Input 1
# | 
# +-----------------------------------

# +-----------------------------------
# | connection point reset
# | 
add_interface reset reset end
set_interface_property reset associatedClock clock
set_interface_property reset synchronousEdges DEASSERT
set_interface_property reset ENABLED true
set_interface_property reset EXPORT_OF ""
set_interface_property reset PORT_NAME_MAP ""
set_interface_property reset SVD_ADDRESS_GROUP ""

add_interface_port reset reset reset Input 1
# | 
# +-----------------------------------

# +-----------------------------------
# | connection point control
# | 
add_interface control avalon end
set_interface_property control addressUnits WORDS
set_interface_property control associatedClock clock
set_interface_property control associatedReset reset
set_interface_property control bitsPerSymbol 8
set_interface_property control burstOnBurstBoundariesOnly false
set_interface_property control burstcountUnits WORDS
set_interface_property control explicitAddressSpan 0
set_interface_property control holdTime 0
set_interface_property control linewrapBursts false
set_interface_property control maximumPendingReadTransactions 0
set_interface_property control readLatency 0
set_interface_property control readWaitStates 0
set_interface_property control readWaitTime 0
set_interface_property control setupTime 0
set_interface_property control timingUnits Cycles
set_interface_property control writeWaitTime 0
set_interface_property control ENABLED true
set_interface_property control EXPORT_OF ""
set_interface_property control PORT_NAME_MAP ""
set_interface_property control SVD_ADDRESS_GROUP ""

add_interface_port control avs_control_write write Input 1
add_interface_port control avs_control_writedata writedata Input 32
set_interface_assignment control embeddedsw.configuration.isFlash 0
set_interface_assignment control embeddedsw.configuration.isMemoryDevice 0
set_interface_assignment control embeddedsw.configuration.isNonVolatileStorage 0
set_interface_assignment control embeddedsw.configuration.isPrintableDevice 0
# +-----------------------------------

