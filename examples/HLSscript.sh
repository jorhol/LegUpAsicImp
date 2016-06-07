#!/bin/bash
rm -f HLSscript.log
LOG_FILE=HLSscript.log
exec 3>&1 1>>${LOG_FILE} 2>&1 # Print log to file, show specified echos to terminal

DESIGNNAME=designname
REMOTEIP=192.168.12.33 # IP of the computer running the LegUp VirtualBox guest
REMOTEPORT=3022 # Port that is forwarded to port 22 on VirtualBox guest
REMOTEDIR=/home/legup/legup-4.0/examples
LEGUPUSER=legup # Username of LegUp image
LEGUPPASS=letmein # Password of LegUp image
BASE_DIR=basedir
LOCALDIR=$BASE_DIR/$DESIGNNAME/ip/$DESIGNNAME #Location of source files on Linux server.

export DESIGN_NAME=$DESIGNNAME
export FILE_LIST=$DESIGNNAME
export BASE_DIR=$BASE_DIR
export VC_WORKSPACE=$BASE_DIR/$DESIGNNAME

module load icc
module load primetime

SSHCOMMANDS2="mkdir $REMOTEDIR/$DESIGNNAME; cd $REMOTEDIR/$DESIGNNAME/; libreoffice --headless --convert-to csv constraints.xlsx --outdir .; exit" # ssh commands for converting excel file to csv

if [$1 = "-s"]; then
	echo Setup started
	ssh-keygen -f id_rsa -t rsa -N ''
	spawn ssh-copy-id "$LEGUPUSER@$REMOTEIP -p $REMOTEPORT"
	expect "password:"
	send "$LEGUPPASS\n"
	expect eof
	echo Setup finished
fi

mkdir -p $LOCALDIR/hls/
ssh $LEGUPUSER@$REMOTEIP -p $REMOTEPORT "mkdir -p $REMOTEDIR/$DESIGNNAME"
scp -P $REMOTEPORT $LOCALDIR/$DESIGNNAME.c $LEGUPUSER@$REMOTEIP:$REMOTEDIR/$DESIGNNAME #Copy design file to LegUp image
scp -P $REMOTEPORT $LOCALDIR/sim/tb/test_$DESIGNNAME\_testcases.v $LEGUPUSER@$REMOTEIP:$REMOTEDIR/$DESIGNNAME #Copy testcases file to LegUp

scp -P $REMOTEPORT $LOCALDIR/hls/constraints.xlsx $LEGUPUSER@$REMOTEIP:$REMOTEDIR/$DESIGNNAME/ #Copy design constraint definitions to LegUp image
ssh $LEGUPUSER@$REMOTEIP -p $REMOTEPORT $SSHCOMMANDS2 #Run commands and script for generating constraint and Makefiles
scp -P $REMOTEPORT $LEGUPUSER@$REMOTEIP:$REMOTEDIR/$DESIGNNAME/constraints.csv $LOCALDIR/hls/ #Copy CSV file from LegUp image
sed 's/\"//g' -i $LOCALDIR/hls/constraints.csv
rm -r $LOCALDIR/hls/makefiles $LOCALDIR/hls/constraintfiles
mkdir $LOCALDIR/hls/makefiles $LOCALDIR/hls/constraintfiles
mkdir $LOCALDIR/reports
rm $LOCALDIR/reports/*.rpt
cd $LOCALDIR/hls/

$LOCALDIR/hls/constraintsGenerator.run $LOCALDIR/hls/constraints.csv .. $DESIGNNAME
NUMRUNS=$?
echo "Generated $NUMRUNS constraint and Makefiles" | tee /dev/fd/3
COUNTER=0
while [ $COUNTER -lt $NUMRUNS ]; do
	echo "Framework loop #$COUNTER" 1>&3
	rm $LOCALDIR/rtl/{*.tcl,*.v,*.mif}
	SSHCOMMANDS="export PATH=/home/legup/clang+llvm-3.5.0-x86_64-linux-gnu/bin:$PATH; cd $REMOTEDIR/$DESIGNNAME/; make clean; make; exit" # Commands to run on SSH session. Need to add clang to PATH as this is not present in SSH session.
	scp -P $REMOTEPORT $LOCALDIR/hls/constraintfiles/config$COUNTER.tcl $LEGUPUSER@$REMOTEIP:$REMOTEDIR/$DESIGNNAME/ #Copy design constraint file to LegUp image
	scp -P $REMOTEPORT $LOCALDIR/hls/makefiles/Makefile$COUNTER $LEGUPUSER@$REMOTEIP:$REMOTEDIR/$DESIGNNAME/Makefile #Copy design Makefile to LegUp image
	echo "Running HLS" 1>&3
	ssh $LEGUPUSER@$REMOTEIP -p $REMOTEPORT $SSHCOMMANDS #Run LegUp
	
	scp -P $REMOTEPORT $LEGUPUSER@$REMOTEIP:$REMOTEDIR/$DESIGNNAME/$DESIGNNAME.v $LOCALDIR/rtl/ #Copy Verilog file from LegUp image
	scp -P $REMOTEPORT $LEGUPUSER@$REMOTEIP:$REMOTEDIR/$DESIGNNAME/test_main.v $LOCALDIR/sim/tb/test_$DESIGNNAME.v #Copy Verilog testbench file from LegUp image
	
	find $LOCALDIR/rtl/$DESIGNNAME.v -type f -exec sed -i "s/module main/module $DESIGNNAME/g" {} \; #Replace top modulename main with designname
	
	find $LOCALDIR/sim/tb/test_$DESIGNNAME.v -type f -exec sed -i "s/module main_tb/module test_$DESIGNNAME/g" {} \; #Replace tb declaration with correct designname
	find $LOCALDIR/sim/tb/test_$DESIGNNAME.v -type f -exec sed -i "s/main main_inst/$DESIGNNAME u_$DESIGNNAME/g" {} \; #Replace top module instantiation in tb with correct designname
	
	echo "Running simulation" 1>&3
	#Run simulation
	(cd $LOCALDIR/sim/run/ && (RUN_ALL --clean) && (vcd2saif -input $LOCALDIR/sim/run/$DESIGNNAME.vcd -output $LOCALDIR/sim/run/$DESIGNNAME.saif)) 
	
	echo "Running synthesis" 1>&3
	#Run synthesis
	(cd $LOCALDIR/syn/ && (make clean) && (make compile)) #Run synthesis clean removes old data
	
	echo "Running layout" 1>&3
	#Run layout
	(cd $LOCALDIR/lay/ && (make clean) && (make outputs_cts))
	
	echo "Running power analysis" 1>&3
	#Run power estimation
	(cd $LOCALDIR/pow/ && (make clean) && (make power_analysis))
	
	#Store synthesis results to common file
	
	echo "Gathering layout results" 1>&3
	var1=$(grep "Combinational Area:" $LOCALDIR/lay/reports/clock_opt_cts_icc.qor)
	var1=${var1//  Combinational Area:/}
	var1=${var1// /}
	var1=${var1//./,}
	echo $var1 >> $LOCALDIR/reports/noncombinational_area.rpt
	var2=$(grep "Noncombinational Area:" $LOCALDIR/lay/reports/clock_opt_cts_icc.qor)
	var2=${var2//  Noncombinational Area:/}
	var2=${var2// /}
	var2=${var2//./,}
	echo $var2 >> $LOCALDIR/reports/combinational_area.rpt
	var3=$(grep "Design Area:" $LOCALDIR/lay/reports/clock_opt_cts_icc.qor)
	var3=${var3//  Design Area: /}
	var3=${var3// /}
	var3=${var3//./,}
	echo $var3 >> $LOCALDIR/reports/design_area.rpt
	var4=$(grep "Total number of registers" $LOCALDIR/syn/reports/$DESIGNNAME.mapped.clock_gating.rpt)
	var4=${var4//          |    Total number of registers          |/}
	var4=${var4// /}
	var4=${var4//|/}
	echo $var4 >> $LOCALDIR/reports/register_count.rpt
	
	echo "Gathering power analysis results" 1>&3
	
	COUNT=0
	while [ $COUNT -lt 4 ]; do
		swpow=$(grep 'Net Switching Power' $LOCALDIR/pow/reports/power_analysis_$DESIGNNAME\_ctrl$COUNT/power_summary.rpt)
		swpow=${swpow//([^)]*)/}
		swpow=${swpow//  Net Switching Power  = /}
		echo -n "$swpow\t">>$LOCALDIR/reports/net_switching_power.rpt
		intpow=$(grep 'Cell Internal Power' $LOCALDIR/pow/reports/power_analysis_$DESIGNNAME\_ctrl$COUNT/power_summary.rpt)
		intpow=${intpow//([^)]*)/}
		intpow=${intpow//  Cell Internal Power  = /}
		echo -n "$intpow\t">>$LOCALDIR/reports/cell_internal_power.rpt
		leakpow=$(grep 'Cell Leakage Power' $LOCALDIR/pow/reports/power_analysis_$DESIGNNAME\_ctrl$COUNT/power_summary.rpt)
		leakpow=${leakpow//([^)]*)/}
		leakpow=${leakpow//  Cell Leakage Power   = /}
		echo -n "$leakpow\t">>$LOCALDIR/reports/cell_leakage_power.rpt
		totpow=$(grep 'Total Power' $LOCALDIR/pow/reports/power_analysis_$DESIGNNAME\_ctrl$COUNT/power_summary.rpt)
		totpow=${totpow//([^)]*)/}
		totpow=${totpow//Total Power            = /}
		echo -n "$totpow\t">>$LOCALDIR/reports/total_power.rpt
		let COUNT=COUNT+1 
	done
	
	swpow=$(grep 'Net Switching Power' $LOCALDIR/pow/reports/power_analysis_$DESIGNNAME\_inactive/power_summary.rpt)
	swpow=${swpow//([^)]*)/}
	swpow=${swpow//  Net Switching Power  = /}
	echo $swpow>>$LOCALDIR/reports/net_switching_power.rpt
	intpow=$(grep 'Cell Internal Power' $LOCALDIR/pow/reports/power_analysis_$DESIGNNAME\_inactive/power_summary.rpt)
	intpow=${intpow//([^)]*)/}
	intpow=${intpow//  Cell Internal Power  = /}
	echo $intpow>>$LOCALDIR/reports/cell_internal_power.rpt
	leakpow=$(grep 'Cell Leakage Power' $LOCALDIR/pow/reports/power_analysis_$DESIGNNAME\_inactive/power_summary.rpt)
	leakpow=${leakpow//([^)]*)/}
	leakpow=${leakpow//  Cell Leakage Power   = /}
	echo $leakpow>>$LOCALDIR/reports/cell_leakage_power.rpt
	totpow=$(grep 'Total Power' $LOCALDIR/pow/reports/power_analysis_$DESIGNNAME\_inactive/power_summary.rpt)
	totpow=${totpow//([^)]*)/}
	totpow=${totpow//Total Power            = /}
	echo $totpow>>$LOCALDIR/reports/total_power.rpt
		
	echo "Register count\tCombinational Area\tNon-combinational Area\tDesign Area\tSwitching Power 0\tSwitching Power 1\tSwitching Power 2\tSwitching Power 3\tSwitching Power Inactive\tInternal Power 0\tInternal Power 1\tInternal Power 2\tInternal Power 3\tInternal Power Inactive\tLeakage Power 0\tLeakage Power 1\tLeakage Power 2\tLeakage Power 3\tLeakage Power Inactive\tTotal Power 0\tTotal Power 1\tTotal Power 2\tTotal Power 3\tTotal Power Inactive" > all_results.rpt
	paste register_count.rpt combinational_area.rpt noncombinational_area.rpt design_area.rpt net_switching_power.rpt cell_internal_power.rpt cell_leakage_power.rpt total_power.rpt >> all_results.rpt
	
	#Store results in dedicated folder
	rm -f $LOCALDIR/sim/run/$DESIGNNAME.vcd #VCD file can get large. Remove before storing framework run data.
	mkdir -p $LOCALDIR/hls/rtl$COUNTER/
	cp $LOCALDIR/hls/constraintfiles/config$COUNTER.tcl $LOCALDIR/rtl/ #Copy design constraint file to current rtl folder
	cp $LOCALDIR/hls/makefiles/Makefile$COUNTER $LOCALDIR/rtl/Makefile
	cp -r $LOCALDIR/{rtl/,sim/,syn/,lay/,pow/,score/} $LOCALDIR/hls/rtl$COUNTER/
	
	let COUNTER=COUNTER+1 
done
echo HLS finished
exit $?
