#!/bin/bash

DESIGNNAME=fir
REMOTEIP=192.168.12.33 # IP of the computer running the LegUp VirtualBox guest
REMOTEPORT=3022 # Port that is forwarded to port 22 on VirtualBox guest
REMOTEDIR=/home/legup/legup-4.0/examples
LEGUPUSER=legup # Username of LegUp image
LEGUPPASS=letmein # Password of LegUp image
CONSTRAINTFILE=config # Name of constraints-file
LOCALDIR=/pri/joh2/ProjectH15/$DESIGNNAME/ip/$DESIGNNAME #Location of source files on Linux server.
SSHCOMMANDS="export PATH=/home/legup/clang+llvm-3.5.0-x86_64-linux-gnu/bin:$PATH; cd $REMOTEDIR/$DESIGNNAME/; make; exit" # Commands to run on SSH session. Need to add clang to PATH as this is not preset in SSH session .
SSHCOMMANDS2="libreoffice --headless --convert-to csv constraints.xlsx --outdir ./;sed 's/\"//g' -i constraints.csv;rm -r makefiles constraintfiles;mkdir makefiles constraintfiles;X=../constraintGenerator.run constraints.csv .. $DESIGNNAME; echo $X"

if [$1 = "-setup"]; then
	echo Setup started
	ssh-keygen -f id_rsa -t rsa -N ''
	spawn ssh-copy-id "$LEGUPUSER@$REMOTEIP -p $REMOTEPORT"
	expect "password:"
	send "$LEGUPPASS\n"
	expect eof
	echo Setup finished
fi

mkdir -p $LOCALDIR/hls/
scp -P $REMOTEPORT $LOCALDIR/$DESIGNNAME.c $LEGUPUSER@$REMOTEIP:$REMOTEDIR/$DESIGNNAME #Copy design file to LegUp image

scp -P $REMOTEPORT $LOCALDIR/constraints.xlsx $LEGUPUSER@$REMOTEIP:$REMOTEDIR/$DESIGNNAME/ #Copy design constraint definitions to LegUp image
NUMRUNS=$(ssh -o StrictHostKeyChecking=no $LEGUPUSER@$REMOTEIP -p $REMOTEPORT $SSHCOMMANDS2) #Run commands and script for generating constraint and Makefiles

COUNTER=0
while [  $COUNTER -lt $NUMRUNS ]; do

	scp -P $REMOTEPORT config.tcl $LEGUPUSER@$REMOTEIP:$REMOTEDIR/$DESIGNNAME/ #Copy design constraint file to LegUp image
	ssh $LEGUPUSER@$REMOTEIP -p $REMOTEPORT $SSHCOMMANDS
	scp -P $REMOTEPORT $LEGUPUSER@$REMOTEIP:$REMOTEDIR/$DESIGNNAME/$DESIGNNAME.v $LOCALDIR/rtl/ #Copy Verilog file from LegUp image
	scp -P $REMOTEPORT $LEGUPUSER@$REMOTEIP:$REMOTEDIR/$DESIGNNAME/test_main.v $LOCALDIR/sim/tb/ #Copy Verilog testbench file from LegUp image
	$LOCALDIR/sim/run/rtl/RUN_ALL #Run simulation
	(cd $LOCALDIR/syn/ && (make compile -B)) #Run synthesis -B forces rebuild all targets
	mkdir -p $LOCALDIR/hls/rtl$COUNTER/
	cp -r $LOCALDIR/{rtl/,sim/,syn/} $LOCALDIR/hls/rtl$COUNTER/
	let COUNTER=COUNTER+1 
done
echo HLS finished
##################################################################
# Here we need to extract information from HLS and do comparison #
##################################################################
exit $?