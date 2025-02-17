LIBRARY ieee;
USE ieee.std_logic_1164.all;
USE ieee.std_logic_unsigned.all;
USE ieee.std_logic_misc.all;

-- ******************************************************************************
-- * License Agreement                                                          *
-- *                                                                            *
-- * Copyright (c) 1991-2013 Altera Corporation, San Jose, California, USA.     *
-- * All rights reserved.                                                       *
-- *                                                                            *
-- * Any megafunction design, and related net list (encrypted or decrypted),    *
-- *  support information, device programming or simulation file, and any other *
-- *  associated documentation or information provided by Altera or a partner   *
-- *  under Altera's Megafunction Partnership Program may be used only to       *
-- *  program PLD devices (but not masked PLD devices) from Altera.  Any other  *
-- *  use of such megafunction design, net list, support information, device    *
-- *  programming or simulation file, or any other related documentation or     *
-- *  information is prohibited for any other purpose, including, but not       *
-- *  limited to modification, reverse engineering, de-compiling, or use with   *
-- *  any other silicon devices, unless such use is explicitly licensed under   *
-- *  a separate agreement with Altera or a megafunction partner.  Title to     *
-- *  the intellectual property, including patents, copyrights, trademarks,     *
-- *  trade secrets, or maskworks, embodied in any such megafunction design,    *
-- *  net list, support information, device programming or simulation file, or  *
-- *  any other related documentation or information provided by Altera or a    *
-- *  megafunction partner, remains with Altera, the megafunction partner, or   *
-- *  their respective licensors.  No other licenses, including any licenses    *
-- *  needed under any third party's intellectual property, are provided herein.*
-- *  Copying or modifying any file, or portion thereof, to which this notice   *
-- *  is attached violates this copyright.                                      *
-- *                                                                            *
-- * THIS FILE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR    *
-- * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
-- * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
-- * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER *
-- * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    *
-- * FROM, OUT OF OR IN CONNECTION WITH THIS FILE OR THE USE OR OTHER DEALINGS  *
-- * IN THIS FILE.                                                              *
-- *                                                                            *
-- * This agreement shall be governed in all respects by the laws of the State  *
-- *  of California and by the laws of the United States of America.            *
-- *                                                                            *
-- ******************************************************************************

-- ******************************************************************************
-- *                                                                            *
-- * This module joins video in streams for the DE-series boards.               *
-- *                                                                            *
-- ******************************************************************************

ENTITY altera_up_avalon_video_stream_merger IS 

-- *****************************************************************************
-- *                             Generic Declarations                          *
-- *****************************************************************************
	
GENERIC (
	
	DW	:INTEGER									:= 15; -- Frame's data width
	EW	:INTEGER									:= 0 -- Frame's empty width
	
);
-- *****************************************************************************
-- *                             Port Declarations                             *
-- *****************************************************************************
PORT (

	-- Inputs
	clk								:IN		STD_LOGIC;
	reset								:IN		STD_LOGIC;

`ifdef USE_SYNC
	sync_data						:IN		STD_LOGIC;
	sync_valid						:IN		STD_LOGIC;

`endif
	stream_in_data_0				:IN		STD_LOGIC_VECTOR(DW DOWNTO  0);	
	stream_in_startofpacket_0	:IN		STD_LOGIC;
	stream_in_endofpacket_0		:IN		STD_LOGIC;
	stream_in_empty_0				:IN		STD_LOGIC_VECTOR(EW DOWNTO  0);	
	stream_in_valid_0				:IN		STD_LOGIC;

	stream_in_data_1				:IN		STD_LOGIC_VECTOR(DW DOWNTO  0);	
	stream_in_startofpacket_1	:IN		STD_LOGIC;
	stream_in_endofpacket_1		:IN		STD_LOGIC;
	stream_in_empty_1				:IN		STD_LOGIC_VECTOR(EW DOWNTO  0);	
	stream_in_valid_1				:IN		STD_LOGIC;

	stream_out_ready				:IN		STD_LOGIC;

`ifndef USE_SYNC
	stream_select					:IN		STD_LOGIC;

`endif
	-- Bidirectional

	-- Outputs
`ifdef USE_SYNC
	sync_ready						:BUFFER	STD_LOGIC;

`endif
	stream_in_ready_0				:BUFFER	STD_LOGIC;

	stream_in_ready_1				:BUFFER	STD_LOGIC;

	stream_out_data				:BUFFER	STD_LOGIC_VECTOR(DW DOWNTO  0);	
	stream_out_startofpacket	:BUFFER	STD_LOGIC;
	stream_out_endofpacket		:BUFFER	STD_LOGIC;
	stream_out_empty				:BUFFER	STD_LOGIC_VECTOR(EW DOWNTO  0);	
	stream_out_valid				:BUFFER	STD_LOGIC

);

END altera_up_avalon_video_stream_merger;

ARCHITECTURE Behaviour OF altera_up_avalon_video_stream_merger IS
-- *****************************************************************************
-- *                           Constant Declarations                           *
-- *****************************************************************************

-- *****************************************************************************
-- *                       Internal Signals Declarations                       *
-- *****************************************************************************
	
	-- Internal Wires
	SIGNAL	enable_setting_stream_select	:STD_LOGIC;
	
	-- Internal Registers
	SIGNAL	between_frames						:STD_LOGIC;
	SIGNAL	stream_select_reg					:STD_LOGIC;
	
	-- State Machine Registers
	
	-- Integers
	
-- *****************************************************************************
-- *                          Component Declarations                           *
-- *****************************************************************************
BEGIN
-- *****************************************************************************
-- *                         Finite State Machine(s)                           *
-- *****************************************************************************


-- *****************************************************************************
-- *                             Sequential Logic                              *
-- *****************************************************************************

	-- Output Registers
	PROCESS (clk)
	BEGIN
		IF clk'EVENT AND clk = '1' THEN
			IF (reset = '1') THEN
				stream_out_data				<=  (OTHERS => '0');
				stream_out_startofpacket	<= '0';
				stream_out_endofpacket		<= '0';
				stream_out_empty				<=  (OTHERS => '0');
				stream_out_valid				<= '0';
			ELSIF (stream_in_ready_0 = '1') THEN
				stream_out_data				<= stream_in_data_0;
				stream_out_startofpacket	<= stream_in_startofpacket_0;
				stream_out_endofpacket		<= stream_in_endofpacket_0;
				stream_out_empty				<= stream_in_empty_0;
				stream_out_valid				<= stream_in_valid_0;
			ELSIF (stream_in_ready_1 = '1') THEN
				stream_out_data				<= stream_in_data_1;
				stream_out_startofpacket	<= stream_in_startofpacket_1;
				stream_out_endofpacket		<= stream_in_endofpacket_1;
				stream_out_empty				<= stream_in_empty_1;
				stream_out_valid				<= stream_in_valid_1;
			ELSIF (stream_out_ready = '1') THEN
				stream_out_valid				<= '0';
			END IF;
		END IF;
	END PROCESS;


	-- Internal Registers
	PROCESS (clk)
	BEGIN
		IF clk'EVENT AND clk = '1' THEN
			IF (reset = '1') THEN
				between_frames <= '1';
			ELSIF ((stream_in_ready_0 = '1') AND (stream_in_endofpacket_0 = '1')) THEN
				between_frames <= '1';
			ELSIF ((stream_in_ready_1 = '1') AND (stream_in_endofpacket_1 = '1')) THEN
				between_frames <= '1';
			ELSIF ((stream_in_ready_0 = '1') AND (stream_in_startofpacket_0 = '1')) THEN
				between_frames <= '0';
			ELSIF ((stream_in_ready_1 = '1') AND (stream_in_startofpacket_1 = '1')) THEN
				between_frames <= '0';
			END IF;
		END IF;
	END PROCESS;


	PROCESS (clk)
	BEGIN
		IF clk'EVENT AND clk = '1' THEN
			IF (reset = '1') THEN
				stream_select_reg <= '0';
`ifdef USE_SYNC
			ELSIF ((enable_setting_stream_select = '1') AND (sync_valid = '1')) THEN
				stream_select_reg <= sync_data;
`else
			ELSIF (enable_setting_stream_select = '1') THEN
				stream_select_reg <= stream_select;
`endif
			END IF;
		END IF;
	END PROCESS;


-- *****************************************************************************
-- *                            Combinational Logic                            *
-- *****************************************************************************

	-- Output Assignments
`ifdef USE_SYNC
	sync_ready <= enable_setting_stream_select;

`endif
	stream_in_ready_0 <= '0' WHEN (stream_select_reg = '1') ELSE 
				'1' WHEN (stream_in_valid_0 = '1') AND 
				((stream_out_valid = '0') OR (stream_out_ready = '1')) ELSE
				'0';

	stream_in_ready_1 <= stream_in_valid_1 AND ( NOT stream_out_valid OR stream_out_ready) WHEN 
								(stream_select_reg = '1') ELSE '0';

	-- Internal Assignments
	enable_setting_stream_select <= 
			  (stream_in_ready_0 AND stream_in_endofpacket_0) OR 
			  (stream_in_ready_1 AND stream_in_endofpacket_1) OR 
			(NOT (stream_in_ready_0 AND stream_in_startofpacket_0) AND between_frames) OR 
			(NOT (stream_in_ready_1 AND stream_in_startofpacket_1) AND between_frames);

-- *****************************************************************************
-- *                          Component Instantiations                         *
-- *****************************************************************************



END Behaviour;
