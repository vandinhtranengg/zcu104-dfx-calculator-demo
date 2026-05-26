###############################################################################
# DFX pblock constraint for calculator_rp
# RP hierarchy from your Vivado design:
#   dfx_demo_i/calculator_rp
###############################################################################

# Mark calculator_rp as Reconfigurable Partition
set_property HD.RECONFIGURABLE true [get_cells dfx_demo_i/calculator_rp]

# Create pblock for the RP
create_pblock pblock_calculator_rp

# Add only the reconfigurable partition cell
add_cells_to_pblock [get_pblocks pblock_calculator_rp] [get_cells dfx_demo_i/calculator_rp]

# Safe smaller region
resize_pblock [get_pblocks pblock_calculator_rp] -add {SLICE_X60Y120:SLICE_X79Y159}

# DFX pblock properties
set_property CONTAIN_ROUTING true [get_pblocks pblock_calculator_rp]
set_property SNAPPING_MODE ON [get_pblocks pblock_calculator_rp]