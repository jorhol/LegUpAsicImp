quit -sim;

cp onchip_mem.v.copy onchip_mem.v
cp tiger_inst.v.copy tiger_inst.v

if {[file exists work]} {
	vdel -lib work -all;
}

if {![file exists work]} {
	vlib work;
}

vlog -novopt *.v;
vsim -debugdb -voptargs="+acc" test_bench;
log -r DUT/*;
do wave.do

run 7000000000000000ns 
