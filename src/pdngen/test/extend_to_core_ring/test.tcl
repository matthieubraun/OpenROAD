read_lef nangate45/NangateOpenCellLibrary.mod.lef
read_def $test_name/floorplan.def

pdngen $test_name/pdn.cfg -verbose

set def_file results/$test_name.def
write_def $def_file

diff_files $def_file $test_name.defok
