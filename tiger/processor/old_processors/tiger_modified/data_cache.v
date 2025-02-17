/*
  This work by Simon Moore and Gregory Chadwick is licenced under the
  Creative Commons Attribution-Non-Commercial-Share Alike 2.0
  UK: England & Wales License.

  To view a copy of this licence, visit:
     http://creativecommons.org/licenses/by-nc-sa/2.0/uk/
  or send a letter to:
     Creative Commons,
     171 Second Street,
     Suite 300,
     San Francisco,
     California 94105,
     USA.
*/
/*
   Cache.v - Toplevel for direct mapped cache for use with the Tiger MIPS
   processor.
   
   The cache is organised into blocks, each block contains several words,
   A memory address is partitioned into a tag which is stored in the cache,
   a cache address which is used to locate a particular block in the cache,
   and a block word which is used to select a word from a block in the cache.
   
   [         tag            | cache address | block word | 0 0 ]
   
   The number of blocks in the cache and number of words per block
   is parameterised.  2^blockSize is the block size in bytes,
   2^cacheSize is the number of blocks in the cache, blockSizeBits
   must give the block size in bits (so blockSizeBits = 2^blockSize * 8).
   
   When modifying blockSize there are two bits of code that must be altered.
   The assignment of memReadDataWord, and the case statement where fetchWord
   is written in the fetch state.
   
   When we lookup an address in the cache we use the cache address portion
   of it to address the internal memory block to retrieve the cache entry,
   we then compare the tag in the cache to the tag of the address, if
   they match and the valid bit is set we have a cache hit, otherwise we
   fetch the entire block from memory (read block number bytes from memory
   starting from the address formed by the tag and cache address with 0s for
   all lower bits).
   
   On a write we immediately start writing the data to memory (write through
   cache behaviour) we also lookup the address in the cache, if we have a hit
   we write the data to the cache as well, otherwise we don't bother.
   
   If the high bit of the address is set the cache is bypassed and data is
   directly read from and written to the avalon bus.
   
   Written by Greg Chadwick, Summer 2007 
*/

//`define TIGER_CTRL_PORT 32'hf0000864


module data_cache (
	input csi_clockreset_clk,
	input csi_clockreset_reset_n,

	//Avalon Bus side signals

	//Avalon-ST interface to send and receive data from tiger_top
	input [71:0] asi_TigertoCache_data,
	output wire [39:0] aso_CachetoTiger_data,

	//Slave interface to talk to accelerator
	//to accept the address portion
	input [2:0] avs_ACCEL_address,
	input avs_ACCEL_begintransfer,
	input avs_ACCEL_read,
	input avs_ACCEL_write,
	input [127:0]avs_ACCEL_writedata,
	output wire [127:0]avs_ACCEL_readdata,
	output wire avs_ACCEL_waitrequest,
/*
	input avs_accelADDR_address,
	input avs_accelADDR_begintransfer,
	input avs_accelADDR_read,
	input avs_accelADDR_write,
	input [63:0]avs_accelADDR_writedata,
	output wire [63:0]avs_accelADDR_readdata,
	output wire avs_accelADDR_waitrequest,
	
	//to accept the data portion
	input avs_accelDATA_begintransfer,
	input avs_accelDATA_write,
	input [63:0]avs_accelDATA_writedata,
	output wire avs_accelDATA_waitrequest,
	
	//to get the size information
	input avs_accelSIZE_begintransfer,
	input avs_accelSIZE_write,
	input [7:0]avs_accelSIZE_writedata,
	output wire avs_accelSIZE_waitrequest,*/
	
	
	//Master inferface to talk to SDRAM for accelerators
	output reg avm_AccelMaster_read,
	output reg avm_AccelMaster_write,
	output reg [31:0]avm_AccelMaster_address,
	output reg [31:0]avm_AccelMaster_writedata,
	output reg [3:0]avm_AccelMaster_byteenable,
	input [31:0]avm_AccelMaster_readdata,
	output reg avm_AccelMaster_beginbursttransfer,
	output reg [2:0]avm_AccelMaster_burstcount,	
	input avm_AccelMaster_waitrequest,
	input avm_AccelMaster_readdatavalid,
	
	//Master interface to talk to SDRAM for processor
	output reg avm_dataMaster_read,
	output reg avm_dataMaster_write,
	output reg [31:0]avm_dataMaster_address,
	output reg [31:0]avm_dataMaster_writedata,
	output reg [3:0]avm_dataMaster_byteenable,
	input [31:0]avm_dataMaster_readdata,
	output reg avm_dataMaster_beginbursttransfer,
	output reg [2:0]avm_dataMaster_burstcount,	
	input avm_dataMaster_waitrequest,
	input avm_dataMaster_readdatavalid
);

	parameter stateIDLE = 0;
	parameter stateREAD = 1;
	parameter stateFETCH = 2;
	parameter stateWRITE = 3;
	parameter stateAVALON_READ = 4;
	parameter stateAVALON_WRITE = 5;
	parameter stateFLUSH = 6;
	parameter stateHOLD = 7;
	
	parameter blockSize = 4;
	parameter blockSizeBits = 128;
	parameter cacheSize = 9;
	parameter tagSizeBits = 32 - cacheSize - blockSize;
//	parameter burstCount = (2**blockSize)/32; //number of burst to main memory	
	parameter burstCount = 4; //number of burst to main memory	
	
	wire cacheHit;
	wire [cacheSize - 1 : 0]cacheAddress;
	wire [blockSize - 3 : 0]blockWord;
	wire [tagSizeBits - 1 : 0]tag;
	wire [1 : 0]byte_proc; //lower 2 bits of address
	
	wire [31:0]memReadDataWord;
	
	wire cacheWrite;
	wire cacheClkEn;
	wire [blockSizeBits + tagSizeBits : 0]cacheData;
	wire [blockSizeBits + tagSizeBits : 0]cacheQ;
	
	wire [tagSizeBits - 1 : 0]cacheTag;
	wire validBit;
	wire [blockSizeBits - 1 : 0]cacheQData;
	
	wire [blockSizeBits - 1 : 0]cacheWriteData;
	wire [31 : 0]writeData;
	
	wire [tagSizeBits - 1 : 0]savedTag;
	wire [blockSize - 3 : 0]savedBlockWord;
	wire [1 : 0]savedByte;
	
	wire fetchDone;
	
	wire bypassCache; //should we bypass the cache for the current read/write operation?
		
	reg [31:0]address;
	reg [31:0]writeDataWord;
	
	reg [blockSizeBits - 33 : 0]fetchData;
	reg [blockSize - 3 : 0]fetchWord;
	
	reg [2:0]state;
	
	//reg [2:0]state_accel;
	
	reg savedMem16;
	reg savedMem8;
	
	reg [cacheSize - 1 : 0]flushAddr;
		
	////////////new cache signals///////////////
	wire clk;
	wire reset_n;

	wire memRead;
	wire memWrite;
	wire [31:0]memAddress;
	wire [31:0]memWriteData;
	wire [31:0]memReadData;
	wire flush;

	wire mem8;
	wire mem16;

	wire canRead;
	wire canWrite;
	wire canFlush;
	wire stall;

	wire cacheClkEn_accel;
	wire cacheClkEn_proc;
/////////for accelerator ////////////////
	wire memRead_accel;
	wire memWrite_accel;
	wire [31:0]memAddress_accel;
	wire [31:0]memWriteData_accel;
	wire [31:0]memReadData_accel;
	//wire flush;

	wire mem8_accel;
	wire mem16_accel;
	wire mem64_accel;

	wire cacheHit_accel;
	wire [cacheSize - 1 : 0]cacheAddress_accel;
	wire [blockSize - 3 : 0]blockWord_accel;
	wire [tagSizeBits - 1 : 0]tag_accel;
	wire [1 : 0]byte_accel; //lower 2 bits of address
	
	wire [31:0]memReadDataWord_accel;
	
	wire cacheWrite_accel;
	wire [blockSizeBits + tagSizeBits : 0]cacheData_accel;
	wire [blockSizeBits + tagSizeBits : 0]cacheQ_accel;
	
	wire [tagSizeBits - 1 : 0]cacheTag_accel;
	wire validBit_accel;
	wire [blockSizeBits - 1 : 0]cacheQData_accel;
	
	wire [blockSizeBits - 1 : 0]cacheWriteData_accel;
	wire [31 : 0]writeData_accel;
	
	wire [tagSizeBits - 1 : 0]savedTag_accel;
	wire [blockSize - 3 : 0]savedBlockWord_accel;
	wire [1 : 0]savedByte_accel;
	
	wire fetchDone_accel;
	wire stall_accel_for_read;
	wire stall_accel_for_write;
	wire stall_accel;
	//wire bypassCache; //should we bypass the cache for the current read/write operation?
		
	reg [31:0]address_accel;
	reg [31:0]writeDataWord_accel;
	
	reg [blockSizeBits - 33 : 0]fetchData_accel;
	reg [blockSize - 3 : 0]fetchWord_accel;
	
	reg [2:0]state_accel;
	
//	reg [2:0]state_accel;
	
	reg savedMem64_accel;
	reg savedMem16_accel;
	reg savedMem8_accel;
	
	
	reg [cacheSize - 1 : 0]flushAddr_accel;
		
	wire mem_accelerator;
	reg stall_cpu_for_accel_reg;
	wire stall_cpu_for_accel;
	wire stall_cpu_nodly;
	wire stall_cpu;
	reg stall_accel_until_fetchDone;
	
	reg write_after_read;
				

//	reg [1:0] counter_64;	
//	wire second_write_flag;
////////////////////////////////	
	assign clk = csi_clockreset_clk;
	assign reset_n = csi_clockreset_reset_n;

	assign clk = csi_clockreset_clk;
	assign reset_n = csi_clockreset_reset_n;


	assign memRead = asi_TigertoCache_data[0];
	assign memWrite = asi_TigertoCache_data[1];
	assign memAddress = asi_TigertoCache_data[33:2];
	assign memWriteData = asi_TigertoCache_data[65:34];
	assign flush = asi_TigertoCache_data[66];
	assign mem8 = asi_TigertoCache_data[67];
	assign mem16 = asi_TigertoCache_data[68];

	assign aso_CachetoTiger_data[31:0] = memReadData;
	assign aso_CachetoTiger_data[32] = canRead;
	assign aso_CachetoTiger_data[33] = canWrite;
	assign aso_CachetoTiger_data[34] = canFlush;
	assign aso_CachetoTiger_data[35] = stall;
	assign aso_CachetoTiger_data[36] = stall_cpu;
	assign aso_CachetoTiger_data[39:37] = 3'b0;
	/////////////////////////////////////////////////////////////
	assign blockWord = memAddress[blockSize  - 1: 2];
	assign tag = memAddress[31 : cacheSize + blockSize];
	assign byte_proc = memAddress[1 : 0];
	
	assign cacheTag = cacheQ[tagSizeBits : 1];
//	assign validBit = cacheQ[0];
	assign validBit = (^cacheQ[0] === 1'bX || cacheQ[0] == 0) ? 0 : 1; 	//to take care of undefined case
	assign cacheQData = cacheQ[blockSizeBits + tagSizeBits : tagSizeBits + 1];
	
	assign savedTag = address[31 : cacheSize + blockSize];
	assign savedBlockWord = address[blockSize  - 1 : 2];
	assign savedByte = address[1 : 0];
	
	//If we're in the fetch state, the data is valid and we've fetched
	//all but the last word we've done the fetch
//	assign fetchDone = (state == stateFETCH	&& avm_dataMaster_readdatavalid && fetchWord == blockSize - 1);
	assign fetchDone = (state == stateFETCH && avm_dataMaster_readdatavalid && fetchWord == burstCount - 1);
	
	//If the fetched data from the cache has the valid bit set
	//and the tag is the one we want we have a hit
	assign cacheHit = validBit && savedTag == cacheTag;
	
	//Stall the pipeline if we're fetching data from memory, or if we've
	//just had a cache miss or if we're trying to write and not in the idle
	//state or if we're bypassing the cache and reading from the avalon bus
	//and the read hasn't completed yet or if we're flushing the cache
	assign stall = state == stateFETCH || (state == stateREAD && !cacheHit) 
		|| (memWrite && state != stateIDLE) || (state != stateIDLE && state != stateREAD && memRead)
		|| (state == stateAVALON_READ && !avm_dataMaster_readdatavalid)
		|| (state == stateFLUSH)
		|| (flush && !canFlush)
		|| (state == stateHOLD);
//		|| stall_cpu_nodly
//		|| stall_cpu_for_accel_reg;
	
	//We can start a read in the idle state or the read state if we've had a cache hit
	assign canRead = state == stateIDLE || (state == stateREAD && cacheHit);
	//We can start a write in the idle state
	assign canWrite = state == stateIDLE;
	//We can start a flush in the idle state
	assign canFlush = state == stateIDLE || (state == stateREAD && cacheHit);
	
	//assign readDataValid = state == stateREAD && cacheHit;
		
	//If we've just done a fetch we want to write to the correct cache address
	//or if we're writing data we want to write to the correct cache address,
	//if if we're flushing the cache we want to write to the current address we're flushing
	//otherwise we want the address given by memAddress.

//only assign address when we want to read
assign cacheAddress = fetchDone ? address[cacheSize + blockSize - 1 : blockSize] :
		state == stateWRITE && cacheHit ? address[cacheSize + blockSize - 1 : blockSize] :
		state == stateFLUSH  ? flushAddr :
			memAddress[cacheSize + blockSize - 1 : blockSize];
	
	//If we've just finished fetching, enable write so we can write the fetched
	//data to the cache next cycle or if we need to write data to the cache
	//enable writing

reg memWrite_reg;
always @(posedge clk)
begin
	memWrite_reg <= memWrite;
end

	assign cacheWrite = fetchDone || (state == stateWRITE && cacheHit && memWrite_reg) || state == stateFLUSH;
	
	//If we want to read and we're either idle or reading and just had a hit or if we've just finised
	//a fetch then enable the cache memory block clock.  If we want to write and we're idle or we've
	//had a hit (so we can write the data into the cache) enable the cache memory block clock.
	
	// MLA -- this is a bug in simulation where the cache line written (fetched from sdram) doesn't update read line
	 //////////////// SYNTHESIS-ONLY CONTENTS
	// synthesis read_comments_as_HDL on
	//	assign cacheClkEn_accel = (memRead_accel && (state_accel == stateIDLE || (state_accel == stateREAD && cacheHit_accel)))
	//	|| fetchDone_accel || (memWrite_accel && state_accel == stateIDLE) || (state_accel == stateWRITE && cacheHit_accel)
	//	|| state_accel == stateFLUSH;
	//	assign cacheClkEn_proc = (memRead && (state == stateIDLE || (state == stateREAD && cacheHit)) && !bypassCache)
	//	|| fetchDone || (memWrite && state == stateIDLE && !bypassCache) || (state == stateWRITE && cacheHit)
	//	|| state == stateFLUSH;
	// synthesis read_comments_as_HDL off
	//////////////// END SYNTHESIS-ONLY CONTENTS
	
        //synthesis translate_off
        //////////////// SIMULATION-ONLY CONTENTS	
	assign cacheClkEn_accel = 1'b1;
	assign cacheClkEn_proc = 1'b1;
        //////////////// END SIMULATION-ONLY CONTENTS
        //synthesis translate_on

	assign cacheData = 
		(state == stateWRITE && cacheHit) ? 
			{cacheWriteData, avm_dataMaster_address[31 : cacheSize + blockSize], 1'b1} :
		state == stateFLUSH ? {(blockSizeBits + tagSizeBits + 1){1'b0}} :
			{avm_dataMaster_readdata, fetchData, avm_dataMaster_address[31 : cacheSize + blockSize], 1'b1};
	
	//If we're reading or writing 8 or 16 bits rather than a full 32-bit word, we need to only write
	//the bits we want to write in the word and keep the rest as they were
	assign writeData = savedMem8 ? (savedByte == 2'b00 ? {memReadDataWord[31 : 8], writeDataWord[7 : 0]}
				: savedByte == 2'b01 ? {memReadDataWord[31 : 16], writeDataWord[7 : 0], memReadDataWord[7 : 0]}
				: savedByte == 2'b10 ? {memReadDataWord[31 : 24], writeDataWord[7 : 0], memReadDataWord[15 : 0]}
				: savedByte == 2'b11 ? {writeDataWord[7 : 0], memReadDataWord[23 : 0]}
				: 32'hx)
			: savedMem16 ? (savedByte[1] == 1'b0 ? {memReadDataWord[31 : 16], writeDataWord[15 : 0]}
				: savedByte[1] == 1'b1 ? {writeDataWord[15 : 0], memReadDataWord[15 : 0]}
				: 32'hx)
			: writeDataWord; 
	
	//When writing data to the cache we need to overwrite only the word we are writing and
	//preserve the rest
	assign cacheWriteData = savedBlockWord == 2'b00 ? {cacheQData[127 : 32], writeData} :
		savedBlockWord == 2'b01 ? {cacheQData[127 : 64], writeData, cacheQData[31 : 0]} :
		savedBlockWord == 2'b10 ? {cacheQData[127 : 96], writeData, cacheQData[63 : 0]} :
		savedBlockWord == 2'b11 ? {writeData, cacheQData[95 : 0]} : 32'bx;
	
	//Multiplexer to select which word of the cache block we want
	//if we're reading from the avalon bus, bypass cache and give
	//data read directly from avalon
	//assign memReadData = cacheQData[(savedBlockWord + 1) * 32 - 1 : savedBlockWord * 32];
	assign memReadDataWord = state == stateAVALON_READ ? avm_dataMaster_readdata
		: savedBlockWord == 2'b00 ? cacheQData[31 : 0]
		: savedBlockWord == 2'b01 ? cacheQData[63 : 32]
		: savedBlockWord == 2'b10 ? cacheQData[95 : 64]
		: savedBlockWord == 2'b11 ? cacheQData[127 : 96]
		: 32'bx;
		
	//If mem8 or mem16 are asserted we only want part of the read word,
	//if neither are asserted we want the entire word
	assign memReadData = savedMem8 ? (savedByte == 2'b11 ? memReadDataWord[31 : 24]
							: savedByte == 2'b10 ? memReadDataWord[23 : 16] 
							: savedByte == 2'b01 ? memReadDataWord[15 : 8]
							: savedByte == 2'b00 ? memReadDataWord[7 : 0]
							: 32'hx)
						: savedMem16 ? (savedByte[1] == 1'b1 ? memReadDataWord[31 : 16]
							: savedByte[1] == 1'b0 ? memReadDataWord[15 : 0]
							: 32'hx)
						: memReadDataWord; 
		
	assign bypassCache = memAddress[31]; //If high bit of address is set we bypass the cache
	
	dcacheMem dcacheMemIns(
						.address_a(cacheAddress),
						.address_b(cacheAddress_accel),
						.clock_a(clk),
						.clock_b(clk),
						.data_a(cacheData),
						.data_b(cacheData_accel),
						.enable_a(cacheClkEn_proc),
						.enable_b(cacheClkEn_accel),
						.wren_a(cacheWrite),
						.wren_b(cacheWrite_accel),
						.q_a(cacheQ),
						.q_b(cacheQ_accel));		


//state machine for processor
	always @(posedge clk, negedge reset_n) begin
		if(!reset_n) begin
			state <= stateIDLE;
			avm_dataMaster_read <= 0;
			avm_dataMaster_write <= 0;
			address <= 0;
			avm_dataMaster_burstcount <= 0;
			avm_dataMaster_beginbursttransfer <= 0;
		end else begin
			case(state)
				stateIDLE: begin
					avm_dataMaster_burstcount <= 1;
					avm_dataMaster_beginbursttransfer <= 0;
					fetchWord <= 0;
					if(memRead) begin //If we want a read start a read
						if(bypassCache) begin
							avm_dataMaster_read <= 1;
							avm_dataMaster_address <= {memAddress[31:2], 2'b0};
							state <= stateAVALON_READ;
						end else begin
							state <= stateREAD;
							avm_dataMaster_address <= {tag, cacheAddress, {blockSize{1'b0}}};
							avm_dataMaster_byteenable <= 4'b1111;
						end
						
						address <= memAddress;
						savedMem8 <= mem8;
						savedMem16 <= mem16;
					end else if(memWrite) begin //If we want a write start a write
						if(bypassCache) begin
							state <= stateAVALON_WRITE;
						end else begin
							state <= stateWRITE;
							address <= memAddress;
							writeDataWord <= memWriteData;
						end
						
						savedMem8 <= mem8;
						savedMem16 <= mem16;
						
						avm_dataMaster_write <= 1;
						avm_dataMaster_writedata <= 
							mem8 ? (byte_proc == 2'b00 ? {24'b0, memWriteData[7 : 0]}
								: byte_proc == 2'b01 ? {16'b0, memWriteData[7 : 0], 8'b0}
								: byte_proc == 2'b10 ? {8'b0, memWriteData[7 : 0], 16'b0}
								: byte_proc == 2'b11 ? {memWriteData[7 : 0], 24'b0}
								: 32'hx)
							: mem16 ? (byte_proc[1] == 1'b0 ? {16'b0, memWriteData[15 : 0]}
								: byte_proc[1] == 1'b1 ? {memWriteData[15 : 0], 16'b0}
								: 32'hx)
							: memWriteData;
							
						avm_dataMaster_address <= {memAddress[31:2], 2'b0};
					end else if(flush) begin
						state <= stateFLUSH;
						flushAddr <= 0;
					end
					
					//If we're reading and bypassing the cache
					//or performing a write we may need to set
					//byte_proc enable to something other than
					//reading/writing the entire word
					if((memRead && bypassCache) || memWrite) begin
						if(mem8) begin
							avm_dataMaster_byteenable <= byte_proc == 2'b11 ? 4'b1000 
								: byte_proc == 2'b10 ? 4'b0100
								: byte_proc == 2'b01 ? 4'b0010
								: byte_proc == 2'b00 ? 4'b0001
								: 4'bxxxx;
						end else if(mem16) begin
							avm_dataMaster_byteenable <= byte_proc[1] == 1'b1 ? 4'b1100
								: byte_proc[1] == 1'b0 ? 4'b0011
								: 4'bxxxx;
						end else begin
							avm_dataMaster_byteenable <= 4'b1111;
						end
					end
				end
				stateREAD: begin
					avm_dataMaster_burstcount <= 1'b1;
					//If we've had a cache hit either go back to idle
					//or if we want another read continue in the read state
					//or if we want to flush go to the flush state
					if(cacheHit) begin 
						if(flush) begin
							state <= stateFLUSH;
							flushAddr <= 0;
						end else if(!memRead) begin
							state <= stateIDLE;
						end else begin
							if(bypassCache) begin
								avm_dataMaster_read <= 1;
								avm_dataMaster_address <= memAddress;
								state <= stateAVALON_READ;
								
								if(mem8) begin
									avm_dataMaster_byteenable <= byte_proc == 2'b11 ? 4'b1000 
										: byte_proc == 2'b10 ? 4'b0100
										: byte_proc == 2'b01 ? 4'b0010
										: byte_proc == 2'b00 ? 4'b0001
										: 4'bxxxx;
								end else if(mem16) begin
									avm_dataMaster_byteenable <= byte_proc[1] == 1'b1 ? 4'b1100
										: byte_proc[1] == 1'b0 ? 4'b0011
										: 4'bxxxx;
								end else begin
									avm_dataMaster_byteenable <= 4'b1111;
								end
							end else begin
								state <= stateREAD;
								avm_dataMaster_address <= {tag, cacheAddress, {blockSize{1'b0}}};
								avm_dataMaster_byteenable <= 4'b1111;
							end
							
							savedMem8 <= mem8;
							savedMem16 <= mem16;
							
							address <= memAddress;
						end
					end else begin //otherwise fetch data from memory
						state <= stateHOLD;
						avm_dataMaster_read <= 1;
						avm_dataMaster_burstcount <= burstCount;
						avm_dataMaster_beginbursttransfer <= 1;
					end
				end
				stateFETCH: begin
					//If wait request is low we can give another address to read from
/*					if(!avm_dataMaster_waitrequest) begin
						//If we've given address for all the blocks we want, stop reading
						if(avm_dataMaster_address[blockSize - 1 : 0] == {{(blockSize - 2){1'b1}}, 2'b0})
							avm_dataMaster_read <= 0;
						else //Otherwise give address of the next block
							avm_dataMaster_address <= avm_dataMaster_address + 4;
					end
	*/				
					//If we have valid data
					if(avm_dataMaster_readdatavalid) begin
						//store it in the fetchData register if it's not the last word
						//(the last word is fed straight into the data register of the memory
						// block)
						case(fetchWord)
							2'b00:
								fetchData[31:0] <= avm_dataMaster_readdata;
							2'b01:
								fetchData[63:32] <= avm_dataMaster_readdata;
							2'b10:
								fetchData[95:64] <= avm_dataMaster_readdata;
						endcase
						
						fetchWord <= fetchWord + 1;
						//If this is the last word go back to the read state
//						if(fetchWord == blockSize - 1)
//							state <= stateREAD;
						if(fetchWord == burstCount - 1) begin
								state <= stateREAD;
						end
					end
				end
				stateHOLD: begin //extra state to begin fetch before it goes into stateFETCH
					avm_dataMaster_beginbursttransfer <= 0;
					if(!avm_dataMaster_waitrequest) begin
						avm_dataMaster_read <= 0;
						state <= stateFETCH;
					end
				end
				stateWRITE: begin					
					//Once the memory write has completed either go back to idle
					//and stop writing or continue with another write
					if(!avm_dataMaster_waitrequest) begin
						avm_dataMaster_write <= 0;
						state <= stateIDLE;
					end
				end
				stateAVALON_READ: begin
					//No more wait request so address has been captured
					if(!avm_dataMaster_waitrequest) begin
						avm_dataMaster_read <= 0; //So stop asserting read
					end
					
					if(avm_dataMaster_readdatavalid) begin //We have our data
						state <= stateIDLE; //so go back to the idle state
					end
				end
				stateAVALON_WRITE: begin
					if(!avm_dataMaster_waitrequest) begin //if the write has finished
						state <= stateIDLE; //then go back to the idle state
						avm_dataMaster_write <= 0;
					end
				end
				stateFLUSH: begin
					flushAddr <= flushAddr + 1;
					if(flushAddr == {cacheSize{1'b1}})
						state <= stateIDLE;
				end
			endcase
		end
	end 			 
			

	reg [1:0]state_64;
//	reg count_64;
	reg memRead_accel64;
	reg memWrite_accel64;
	reg stall_accel_64;

	//assign accel_write = avs_accelADDR_write & avs_accelDATA_write;
	wire memRead_64;
	wire memWrite_64;

	//flag to indicate first write is done and second write data needs to be asserted (for 64bit writes)
	reg memReadData_accel_lo;
	wire stall_cpu_from_accel;
	wire unstall_cpu_from_accel;

//	assign mem_accelerator = (avs_accelADDR_address == 1'b0);
//	assign memRead_accel = (mem_accelerator) ? avs_accelADDR_read : 0;
	assign memRead_accel = avs_ACCEL_read;
//	assign memWrite_accel = (avs_accelADDR_address == 1'b0)? avs_accelADDR_write : 0;
	assign memWrite_accel =  (!stall_cpu_from_accel && !unstall_cpu_from_accel)? avs_ACCEL_write : 0;
	assign memAddress_accel = (state_64[1] == 1)? avs_ACCEL_writedata[99:68] + 4 : avs_ACCEL_writedata[99:68];
	assign memWriteData_accel = (state_64[1] == 1)? avs_ACCEL_writedata[63:32] : avs_ACCEL_writedata[31:0];
//	assign memAddress_accel = (state_64 == 2)? avs_ACCEL_writedata[99:68] + 4 : avs_ACCEL_writedata[99:68];
//	assign memWriteData_accel = (state_64 == 2)? avs_ACCEL_writedata[63:32] : avs_ACCEL_writedata[31:0];
//	assign avs_accelADDR_readdata = mem_accelerator ? (mem64_accel? {memReadData_accel,memReadData_accel_lo} : {32'b0, memReadData_accel} ): 0;
	assign avs_ACCEL_readdata = mem64_accel? {64'd0, memReadData_accel,memReadData_accel_lo} : {96'd0, memReadData_accel};

/*
	assign mem8_accel = ((avs_ACCEL_write | avs_ACCEL_read) & (avs_ACCEL_address == 3'b001)) ? 1'b1 : 1'b0;

	assign mem16_accel = ((avs_ACCEL_write | avs_ACCEL_read) & (avs_ACCEL_address == 3'b010)) ? 1'b1 : 1'b0;
	
	assign mem64_accel = ((avs_ACCEL_write | avs_ACCEL_read) & (avs_ACCEL_address == 3'b011)) ? 1'b1 : 1'b0;
*/
	
	assign mem8_accel = !avs_ACCEL_writedata[65] && !avs_ACCEL_writedata[64];
	assign mem16_accel = !avs_ACCEL_writedata[65] && avs_ACCEL_writedata[64];
	assign mem64_accel = avs_ACCEL_writedata[65] && avs_ACCEL_writedata[64];

	//cpu stalling logic for when stalling is enabled instead of polling
	assign stall_cpu = stall_cpu_for_accel_reg || stall_cpu_nodly;
	assign stall_cpu_from_accel = avs_ACCEL_writedata[66];
	assign unstall_cpu_from_accel = avs_ACCEL_writedata[67];

	//cpu stalling logic for when stalling is enabled instead of polling
	always @(posedge clk)
	begin
		if (!reset_n)
			stall_cpu_for_accel_reg <= 0;
		else if (avs_ACCEL_write && stall_cpu_from_accel)
			stall_cpu_for_accel_reg <= 1'b1;
		else if (avs_ACCEL_write && unstall_cpu_from_accel)
			stall_cpu_for_accel_reg <= 1'b0;
	end

	//assign stall_cpu_for_accel = (!reset_n) ? 0 : stall_cpu_for_accel_reg;
//	assign stall_cpu_nodly = (!reset_n) ? 0 : (avs_accelADDR_address == 1'b1 && avs_accelADDR_write == 1'b1) ? 1 : 0;
//	assign stall_cpu_nodly = (!reset_n) ? 0 : (avs_ACCEL_writedata[65:64] == 2'b11 && avs_ACCEL_write == 1'b1) ? 1 : 0;

	//for when processor is stalled during the execution of accelerator,
	//when the accelerator begins execution it will send a stall signal on stall_cpu_from_accel
	assign stall_cpu_nodly = (avs_ACCEL_write == 1'b1)? stall_cpu_from_accel: 0;
//	assign stall_cpu_nodly = !unstall_cpu_from_accel && stall_cpu_from_accel;
	
	always @(posedge clk, negedge reset_n) 
	begin
		if(!reset_n) 
		begin
			memReadData_accel_lo <= 0;
		end 
		else if (state_64 == 1) 
		begin
			memReadData_accel_lo <= memReadDataWord_accel;
		end
	end
					
	//state machine for controlling 64 bit operations
	//need to do two consecutive 32 bit reads or writes
	always @(posedge clk, negedge reset_n) 
	begin
		if(!reset_n) 
		begin
			state_64 <= 0;
		end 
		else if (mem64_accel) 
		begin
			case(state_64)
			0 : begin
					//if first write or read
					if (memWrite_accel || memRead_accel)
					begin
						state_64 <= 1;
					end
			    end
			1 : begin 
					if (memRead_accel)//if read
					begin
						if (cacheHit_accel)
						begin	
							state_64 <= 2;								
						end
						else if (!avm_AccelMaster_waitrequest)
						begin
							//if done first write
							state_64 <= 2;
						end							
					end
					else	//if write
					begin
						if (!avm_AccelMaster_waitrequest)
						begin
							//if done first write
							state_64 <= 2;
						end
					end
			    end
			2 : begin	
					//assert second address and data 
					//for extra state in between two consecutive operations
					state_64 <= 3;
					
			    end
			3 : begin	
					//if second write or read

					if (memRead_accel)//if read
					begin
						if (cacheHit_accel)
						begin	
							state_64 <= 0;								
						end
						else if (!avm_AccelMaster_waitrequest)
						begin
							//if done first write
							state_64 <= 0;
						end							
					end
					else	//if write
					begin
						if (!avm_AccelMaster_waitrequest)
						begin
							//if done first write
							state_64 <= 0;
						end
					end
			    end
			endcase
		end
	end


	//waitrequest logic for accelerator

	//when the accelerator is reading, this signal latch the begintranfer signal and go low after cache hit
	//since it is on posedge clk, it will be high for one cycle longer than we actually need it to, hence we create a state machine below to get around this issue
	always @(posedge clk)
	begin
		if (avs_ACCEL_begintransfer)
			stall_accel_until_fetchDone <= 1'b1;
		else if (cacheHit_accel)
			stall_accel_until_fetchDone <= 1'b0;
	end

	//on a write, stall condition will be from begintransfer, to when waitrequest from main memory goes low
//	assign stall_accel_for_write = avs_ACCEL_begintransfer || avm_AccelMaster_waitrequest;
	assign stall_accel_for_write = (avs_ACCEL_begintransfer || avm_AccelMaster_waitrequest) && (!stall_cpu_from_accel && !unstall_cpu_from_accel);

	//on a read, stall condition will be from begintransfer, to when fetch is done from main memory
	assign stall_accel_for_read = avs_ACCEL_begintransfer || stall_accel_until_fetchDone;

	//selecting the right stall condition depending on read or write
	assign stall_accel =  memRead_accel? stall_accel_for_read: memWrite_accel? stall_accel_for_write : 0;

	reg waitrequest_32;	
	wire stall_condition;
	assign stall_condition = stall_accel || write_after_read;

//	assign avs_ACCEL_waitrequest = (state_64 == 1 || state_64 == 2 || state_accel == stateFETCH) ? 1 : waitrequest_32;

	//stall accel for first 32 bit memory access of 64 bit accesses or on normal stalls for 32 bits accesses
	assign avs_ACCEL_waitrequest = state_64 == 1 || state_64 == 2 || state_accel == stateFETCH || waitrequest_32;

	//stall logic for 32 bit memory accesses
	//Since stall_condition is on posedge clk, it will be high for one cycle longer than we actually need it to be, hence we create this combinational state machine to make waitrequest go low as soon as cachehit
	always @(*)
	begin
		begin
			case ({stall_condition, cacheHit_accel})
			2'b10: begin
				waitrequest_32 <= 1'b1; //stall when stall condition is high and cache miss
			end
			2'b11: begin //if stall
				if (memWrite_accel)
				begin
					//check for back to back writes or pending writes to SDRAM
					if (avs_ACCEL_begintransfer || avm_AccelMaster_waitrequest)
					begin
						waitrequest_32 <= 1'b1;
					end
					else
					begin
						waitrequest_32 <= 1'b0;
					end
				end
				else
				begin
					//check for back to back reads
					if (avs_ACCEL_begintransfer )
					begin
						waitrequest_32 <= 1'b1;					
					end
					else
					begin
						waitrequest_32 <= 1'b0;	
					end
				end
			end
			default: begin
				waitrequest_32 <= 1'b0;
			end			
			endcase
		end
	end
	
	
	assign blockWord_accel = memAddress_accel[blockSize  - 1: 2];
	assign tag_accel = memAddress_accel[31 : cacheSize + blockSize];
	assign byte_accel = memAddress_accel[1 : 0];
	
	assign cacheTag_accel = cacheQ_accel[tagSizeBits : 1];
//	assign validBit_accel = cacheQ_accel[0];
	assign validBit_accel = (^cacheQ_accel[0] === 1'bX || cacheQ_accel[0] == 0) ? 0 : 1; 	//to take care of undefined case
	assign cacheQData_accel = cacheQ_accel[blockSizeBits + tagSizeBits : tagSizeBits + 1];
	
	assign savedTag_accel = address_accel[31 : cacheSize + blockSize];
	assign savedBlockWord_accel = address_accel[blockSize  - 1 : 2];
	assign savedByte_accel = address_accel[1 : 0];
	
	//If we're in the fetch state_accel, the data is valid and we've fetched
	//all but the last word we've done the fetch
//	assign fetchDone_accel = (state_accel == stateFETCH && avm_AccelMaster_readdatavalid && fetchWord_accel == blockSize - 1);
	assign fetchDone_accel = (state_accel == stateFETCH && avm_AccelMaster_readdatavalid && fetchWord_accel == burstCount - 1);
	
	//If the fetched data from the cache has the valid bit set
	//and the tag is the one we want we have a hit
	assign cacheHit_accel = validBit_accel && savedTag_accel == cacheTag_accel;
	
	//Stall the pipeline if we're fetching data from memory, or if we've
	//just had a cache miss or if we're trying to write and not in the idle
	//state_accel or if we're bypassing the cache and reading from the avalon bus
	//and the read hasn't completed yet or if we're flushing the cache

		
	//If we've just done a fetch we want to write to the correct cache address_accel
	//or if we're writing data we want to write to the correct cache address_accel,
	//if if we're flushing the cache we want to write to the current address_accel we're flushing
	//otherwise we want the address_accel given by memAddress_accel.

	assign cacheAddress_accel = fetchDone_accel ? address_accel[cacheSize + blockSize - 1 : blockSize] :
		state_accel == stateWRITE && cacheHit_accel ? address_accel[cacheSize + blockSize - 1 : blockSize] :
		state_accel == stateFLUSH  ? flushAddr_accel :
			memAddress_accel[cacheSize + blockSize - 1 : blockSize];
	
	//If we've just finished fetching, enable write so we can write the fetched
	//data to the cache next cycle or if we need to write data to the cache
	//enable writing
	assign cacheWrite_accel = (state_64 == 1 && !avm_AccelMaster_waitrequest)? 0 : fetchDone_accel || (state_accel == stateWRITE && cacheHit_accel) || state_accel == stateFLUSH;
	
	//If we want to read and we're either idle or reading and just had a hit or if we've just finised
	//a fetch then enable the cache memory block clock.  If we want to write and we're idle or we've
	//had a hit (so we can write the data into the cache) enable the cache memory block clock.
	
	
	//Data to write to the cache, in the format
	//[            data                 | tag | v ]
	//Where v is the valid bit (set if valid)
	//If we're writing and we've had a cache hit we can overwrite what's there
	//with new data, if we're flushing then write 0s, otherwise we just give 
	//data we may have just fetched (The write enable will only be asserted 
	//if we have just fetched this data and want it written)
	assign cacheData_accel = 
		(state_accel == stateWRITE && cacheHit_accel) ? 
			{cacheWriteData_accel, avm_AccelMaster_address[31 : cacheSize + blockSize], 1'b1} :
		state_accel == stateFLUSH ? {(blockSizeBits + tagSizeBits + 1){1'b0}} :
			{avm_AccelMaster_readdata, fetchData_accel, avm_AccelMaster_address[31 : cacheSize + blockSize], 1'b1};
	
	//If we're reading or writing 8 or 16 bits rather than a full 32-bit word, we need to only write
	//the bits we want to write in the word and keep the rest as they were
	assign writeData_accel = savedMem8_accel ? (savedByte_accel == 2'b00 ? {memReadDataWord_accel[31 : 8], writeDataWord_accel[7 : 0]}
				: savedByte_accel == 2'b01 ? {memReadDataWord_accel[31 : 16], writeDataWord_accel[7 : 0], memReadDataWord_accel[7 : 0]}
				: savedByte_accel == 2'b10 ? {memReadDataWord_accel[31 : 24], writeDataWord_accel[7 : 0], memReadDataWord_accel[15 : 0]}
				: savedByte_accel == 2'b11 ? {writeDataWord_accel[7 : 0], memReadDataWord_accel[23 : 0]}
				: 32'hx)
			: savedMem16_accel ? (savedByte_accel[1] == 1'b0 ? {memReadDataWord_accel[31 : 16], writeDataWord_accel[15 : 0]}
				: savedByte_accel[1] == 1'b1 ? {writeDataWord_accel[15 : 0], memReadDataWord_accel[15 : 0]}
				: 32'hx)
			: writeDataWord_accel; 
	
	//When writing data to the cache we need to overwrite only the word we are writing and
	//preserve the rest
	assign cacheWriteData_accel = savedBlockWord_accel == 2'b00 ? {cacheQData_accel[127 : 32], writeData_accel} :
		savedBlockWord_accel == 2'b01 ? {cacheQData_accel[127 : 64], writeData_accel, cacheQData_accel[31 : 0]} :
		savedBlockWord_accel == 2'b10 ? {cacheQData_accel[127 : 96], writeData_accel, cacheQData_accel[63 : 0]} :
		savedBlockWord_accel == 2'b11 ? {writeData_accel, cacheQData_accel[95 : 0]} : 32'bx;
	
	//Multiplexer to select which word of the cache block we want
	//if we're reading from the avalon bus, bypass cache and give
	//data read directly from avalon
	//assign memReadData_accel = cacheQData_accel[(savedBlockWord_accel + 1) * 32 - 1 : savedBlockWord_accel * 32];
	assign memReadDataWord_accel = state_accel == stateAVALON_READ ? avm_AccelMaster_readdata
		: savedBlockWord_accel == 2'b00 ? cacheQData_accel[31 : 0]
		: savedBlockWord_accel == 2'b01 ? cacheQData_accel[63 : 32]
		: savedBlockWord_accel == 2'b10 ? cacheQData_accel[95 : 64]
		: savedBlockWord_accel == 2'b11 ? cacheQData_accel[127 : 96]
		: 32'bx;
		
	//If mem8_accel or mem16_accel are asserted we only want part of the read word,
	//if neither are asserted we want the entire word
	assign memReadData_accel = savedMem8_accel ? (savedByte_accel == 2'b11 ? memReadDataWord_accel[31 : 24]
							: savedByte_accel == 2'b10 ? memReadDataWord_accel[23 : 16] 
							: savedByte_accel == 2'b01 ? memReadDataWord_accel[15 : 8]
							: savedByte_accel == 2'b00 ? memReadDataWord_accel[7 : 0]
							: 32'hx)
						: savedMem16_accel ? (savedByte_accel[1] == 1'b1 ? memReadDataWord_accel[31 : 16]
							: savedByte_accel[1] == 1'b0 ? memReadDataWord_accel[15 : 0]
							: 32'hx)
						: memReadDataWord_accel; 		


	//state_accel machine for accelerators						
	always @(posedge clk, negedge reset_n) begin
		if(!reset_n) begin
			state_accel <= stateIDLE;
			avm_AccelMaster_read <= 0;
			avm_AccelMaster_write <= 0;
			address_accel <= 0;
			write_after_read <= 0;
			avm_AccelMaster_burstcount <= 0;
			avm_AccelMaster_beginbursttransfer <= 0;

		end else begin
			case(state_accel)
				stateIDLE: begin
					write_after_read <= 0;
					avm_AccelMaster_burstcount <= 1;
					avm_AccelMaster_beginbursttransfer <= 0;
					fetchWord_accel <= 0;
					
					if(memRead_accel) begin //If we want a read start a read
						begin
							state_accel <= stateREAD;
							avm_AccelMaster_address <= {tag_accel, cacheAddress_accel, {blockSize{1'b0}}};
							avm_AccelMaster_byteenable <= 4'b1111;
						end
						
						address_accel <= memAddress_accel;
						savedMem8_accel <= mem8_accel;
						savedMem16_accel <= mem16_accel;
					end else if(memWrite_accel) begin //If we want a write start a write
						
						state_accel <= stateWRITE;
						address_accel <= memAddress_accel;
						writeDataWord_accel <= memWriteData_accel[31:0];

						
						savedMem8_accel <= mem8_accel;
						savedMem16_accel <= mem16_accel;
						
						avm_AccelMaster_write <= 1;
						avm_AccelMaster_writedata <= 
							mem8_accel ? (byte_accel == 2'b00 ? {24'b0, memWriteData_accel[7 : 0]}
								: byte_accel == 2'b01 ? {16'b0, memWriteData_accel[7 : 0], 8'b0}
								: byte_accel == 2'b10 ? {8'b0, memWriteData_accel[7 : 0], 16'b0}
								: byte_accel == 2'b11 ? {memWriteData_accel[7 : 0], 24'b0}
								: 32'hx)
							: mem16_accel ? (byte_accel[1] == 1'b0 ? {16'b0, memWriteData_accel[15 : 0]}
								: byte_accel[1] == 1'b1 ? {memWriteData_accel[15 : 0], 16'b0}
								: 32'hx)
							: memWriteData_accel[31:0];
							
						avm_AccelMaster_address <= {memAddress_accel[31:2], 2'b0};
					end 
					//If we're reading and bypassing the cache
					//or performing a write we may need to set
					//byte_accel enable to something other than
					//reading/writing the entire word
					
					//if((memRead_accel && bypassCache) || memWrite_accel) begin
					if (memWrite_accel) begin
						if (mem8_accel) begin
							avm_AccelMaster_byteenable <= byte_accel == 2'b11 ? 4'b1000 
								: byte_accel == 2'b10 ? 4'b0100
								: byte_accel == 2'b01 ? 4'b0010
								: byte_accel == 2'b00 ? 4'b0001
								: 4'bxxxx;
						end else if(mem16_accel) begin
							avm_AccelMaster_byteenable <= byte_accel[1] == 1'b1 ? 4'b1100
								: byte_accel[1] == 1'b0 ? 4'b0011
								: 4'bxxxx;
						end else begin
							avm_AccelMaster_byteenable <= 4'b1111;
						end
					end
				end
				stateREAD: begin
					avm_AccelMaster_burstcount <= 1'b1;
					//If we've had a cache hit either go back to idle
					//or if we want another read continue in the read state_accel
					//or if we want to flush go to the flush state_accel
					if(cacheHit_accel) begin 
						if(!memRead_accel) begin
							//when there is a write right after a read
							if (memWrite_accel)
							begin
								//$display("write after read!");
								state_accel <= stateWRITE;
								write_after_read <= 1;			
								//state_accel <= stateWRITE;
								
								address_accel <= memAddress_accel;
								writeDataWord_accel <= memWriteData_accel[31:0];						
								savedMem8_accel <= mem8_accel;
								savedMem16_accel <= mem16_accel;
								avm_AccelMaster_write <= 1;
								avm_AccelMaster_writedata <= 
									mem8_accel ? (byte_accel == 2'b00 ? {24'b0, memWriteData_accel[7 : 0]}
									: byte_accel == 2'b01 ? {16'b0, memWriteData_accel[7 : 0], 8'b0}
									: byte_accel == 2'b10 ? {8'b0, memWriteData_accel[7 : 0], 16'b0}
									: byte_accel == 2'b11 ? {memWriteData_accel[7 : 0], 24'b0}
									: 32'hx)
								: mem16_accel ? (byte_accel[1] == 1'b0 ? {16'b0, memWriteData_accel[15 : 0]}
									: byte_accel[1] == 1'b1 ? {memWriteData_accel[15 : 0], 16'b0}
									: 32'hx)
								: memWriteData_accel[31:0];
							
								avm_AccelMaster_address <= {memAddress_accel[31:2], 2'b0};
								
								if (mem8_accel) begin
									avm_AccelMaster_byteenable <= byte_accel == 2'b11 ? 4'b1000 
										: byte_accel == 2'b10 ? 4'b0100
										: byte_accel == 2'b01 ? 4'b0010
										: byte_accel == 2'b00 ? 4'b0001
										: 4'bxxxx;
								end else if(mem16_accel) begin
									avm_AccelMaster_byteenable <= byte_accel[1] == 1'b1 ? 4'b1100
										: byte_accel[1] == 1'b0 ? 4'b0011
										: 4'bxxxx;
								end else begin
									avm_AccelMaster_byteenable <= 4'b1111;
								end
								
							end
							else
							begin
								state_accel <= stateIDLE;
							end			
						//if memread is still high
						end else begin
								state_accel <= stateREAD;
								avm_AccelMaster_address <= {tag_accel, cacheAddress_accel, {blockSize{1'b0}}};
								avm_AccelMaster_byteenable <= 4'b1111;
							//end
							
							savedMem8_accel <= mem8_accel;
							savedMem16_accel <= mem16_accel;
							
							address_accel <= memAddress_accel;
						end
					//if not cachehit
					end else begin //otherwise fetch data from memory
						state_accel <= stateHOLD;
						avm_AccelMaster_read <= 1;
						avm_AccelMaster_burstcount <= burstCount;
						avm_AccelMaster_beginbursttransfer <= 1;

					end
				end
				stateFETCH: begin
					write_after_read <= 0;
					//If wait request is low we can give another address_accel to read from
/*					if(!avm_AccelMaster_waitrequest) begin
						//If we've given address_accel for all the blocks we want, stop reading
						if(avm_AccelMaster_address[blockSize - 1 : 0] == {{(blockSize - 2){1'b1}}, 2'b0})
							avm_AccelMaster_read <= 0;
						else //Otherwise give address_accel of the next block
							avm_AccelMaster_address <= avm_AccelMaster_address + 4;
					end
*/					
					//If we have valid data
					if(avm_AccelMaster_readdatavalid) begin
						//store it in the fetchData_accel register if it's not the last word
						//(the last word is fed straight into the data register of the memory
						// block)
						case(fetchWord_accel)
							2'b00:
								fetchData_accel[31:0] <= avm_AccelMaster_readdata;
							2'b01:
								fetchData_accel[63:32] <= avm_AccelMaster_readdata;
							2'b10:
								fetchData_accel[95:64] <= avm_AccelMaster_readdata;
						endcase
						
						fetchWord_accel <= fetchWord_accel + 1;
						//If this is the last word go back to the read state_accel
						if(fetchWord_accel == burstCount - 1) begin
							state_accel <= stateREAD;
						end
					end
				end

				stateHOLD: begin //extra state to begin fetch before it goes into stateFETCH
					avm_AccelMaster_beginbursttransfer <= 0;
					if(!avm_AccelMaster_waitrequest) begin					
						avm_AccelMaster_read <= 0;
						state_accel <= stateFETCH;
					end
				end

				stateWRITE: begin		

					write_after_read <= 0;
					//Once the memory write has completed either go back to idle
					//and stop writing or continue with another write
					if(!avm_AccelMaster_waitrequest) begin
						begin
							avm_AccelMaster_write <= 0;
							state_accel <= stateIDLE;
						end
					end
				end
				stateAVALON_READ: begin
					//No more wait request so address_accel has been captured
					if(!avm_AccelMaster_waitrequest) begin
						avm_AccelMaster_read <= 0; //So stop asserting read
					end
					
					if(avm_AccelMaster_readdatavalid) begin //We have our data
						state_accel <= stateIDLE; //so go back to the idle state_accel
					end
				end
				stateAVALON_WRITE: begin
					if(!avm_AccelMaster_waitrequest) begin //if the write has finished
						state_accel <= stateIDLE; //then go back to the idle state_accel
						avm_AccelMaster_write <= 0;
					end
				end
				stateFLUSH: begin
					flushAddr_accel <= flushAddr_accel + 1;
					if(flushAddr_accel == {cacheSize{1'b1}})
						state_accel <= stateIDLE;
				end
			endcase
		end
	end 			 
endmodule


