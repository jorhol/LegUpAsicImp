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
-- * This module converts video in stream between color spaces on the DE        *
-- *  boards.                                                                   *
-- *                                                                            *
-- ******************************************************************************

ENTITY altera_up_avalon_video_csc IS 

-- *****************************************************************************
-- *                             Generic Declarations                          *
-- *****************************************************************************
	
GENERIC (
	
	IW		:INTEGER									:= 23;
	OW		:INTEGER									:= 23;
	
	EIW	:INTEGER									:= 1;
	EOW	:INTEGER									:= 1
	
);
-- *****************************************************************************
-- *                             Port Declarations                             *
-- *****************************************************************************
PORT (

	-- Inputs
	clk								:IN		STD_LOGIC;
	reset								:IN		STD_LOGIC;

	stream_in_data					:IN		STD_LOGIC_VECTOR(IW DOWNTO  0);	
	stream_in_startofpacket		:IN		STD_LOGIC;
	stream_in_endofpacket		:IN		STD_LOGIC;
	stream_in_empty				:IN		STD_LOGIC_VECTOR(EIW DOWNTO  0);	
	stream_in_valid				:IN		STD_LOGIC;

	stream_out_ready				:IN		STD_LOGIC;

	-- Bidirectional

	-- Outputs
	stream_in_ready				:BUFFER	STD_LOGIC;

	stream_out_data				:BUFFER	STD_LOGIC_VECTOR(OW DOWNTO  0);	
	stream_out_startofpacket	:BUFFER	STD_LOGIC;
	stream_out_endofpacket		:BUFFER	STD_LOGIC;
	stream_out_empty				:BUFFER	STD_LOGIC_VECTOR(EOW DOWNTO  0);	
	stream_out_valid				:BUFFER	STD_LOGIC

);

END altera_up_avalon_video_csc;

ARCHITECTURE Behaviour OF altera_up_avalon_video_csc IS
-- *****************************************************************************
-- *                           Constant Declarations                           *
-- *****************************************************************************

-- *****************************************************************************
-- *                       Internal Signals Declarations                       *
-- *****************************************************************************
	
	-- Internal Wires
	SIGNAL	transfer_data				:STD_LOGIC;
	
	SIGNAL	converted_data				:STD_LOGIC_VECTOR(OW DOWNTO  0);	
	
	SIGNAL	converted_startofpacket	:STD_LOGIC;
	SIGNAL	converted_endofpacket	:STD_LOGIC;
	SIGNAL	converted_empty			:STD_LOGIC_VECTOR(EOW DOWNTO  0);	
	SIGNAL	converted_valid			:STD_LOGIC;
	
	-- Internal Registers
	SIGNAL	data							:STD_LOGIC_VECTOR(IW DOWNTO  0);	
	SIGNAL	startofpacket				:STD_LOGIC;
	SIGNAL	endofpacket					:STD_LOGIC;
	SIGNAL	empty							:STD_LOGIC_VECTOR(EIW DOWNTO  0);	
	SIGNAL	valid							:STD_LOGIC;
	
	-- State Machine Registers
	
	-- Integers
	
-- *****************************************************************************
-- *                          Component Declarations                           *
-- *****************************************************************************
`ifdef USE_Y_TO_RGB
`elsif USE_YCRCB_TO_RGB
	COMPONENT altera_up_YCrCb_to_RGB_converter
	PORT (
		-- Inputs
		clk								:IN		STD_LOGIC;
		clk_en							:IN		STD_LOGIC;
		reset								:IN		STD_LOGIC;

		Y									:IN		STD_LOGIC_VECTOR( 7 DOWNTO  0);
		Cr									:IN		STD_LOGIC_VECTOR( 7 DOWNTO  0);
		Cb									:IN		STD_LOGIC_VECTOR( 7 DOWNTO  0);
		stream_in_startofpacket		:IN		STD_LOGIC;
		stream_in_endofpacket		:IN		STD_LOGIC;
		stream_in_empty				:IN		STD_LOGIC_VECTOR(EIW DOWNTO  0);
		stream_in_valid				:IN		STD_LOGIC;

		-- Bidirectionals

		-- Outputs
		R									:BUFFER	STD_LOGIC_VECTOR( 7 DOWNTO  0);
		G									:BUFFER	STD_LOGIC_VECTOR( 7 DOWNTO  0);
		B									:BUFFER	STD_LOGIC_VECTOR( 7 DOWNTO  0);
		stream_out_startofpacket	:BUFFER	STD_LOGIC;
		stream_out_endofpacket		:BUFFER	STD_LOGIC;
		stream_out_empty				:BUFFER	STD_LOGIC_VECTOR(EOW DOWNTO  0);
		stream_out_valid				:BUFFER	STD_LOGIC
	);
	END COMPONENT;
`else
	COMPONENT altera_up_RGB_to_YCrCb_converter
	PORT (
		-- Inputs
		clk								:IN		STD_LOGIC;
		clk_en							:IN		STD_LOGIC;
		reset								:IN		STD_LOGIC;

		R									:IN		STD_LOGIC_VECTOR( 7 DOWNTO  0);
		G									:IN		STD_LOGIC_VECTOR( 7 DOWNTO  0);
		B									:IN		STD_LOGIC_VECTOR( 7 DOWNTO  0);
		stream_in_startofpacket		:IN		STD_LOGIC;
		stream_in_endofpacket		:IN		STD_LOGIC;
		stream_in_empty				:IN		STD_LOGIC_VECTOR(EIW DOWNTO  0);
		stream_in_valid				:IN		STD_LOGIC;

		-- Bidirectionals

		-- Outputs
		Y									:BUFFER	STD_LOGIC_VECTOR( 7 DOWNTO  0);
`ifdef USE_RGB_TO_YCRCB
		Cr									:BUFFER	STD_LOGIC_VECTOR( 7 DOWNTO  0);
		Cb									:BUFFER	STD_LOGIC_VECTOR( 7 DOWNTO  0);
`else
`endif
		stream_out_startofpacket	:BUFFER	STD_LOGIC;
		stream_out_endofpacket		:BUFFER	STD_LOGIC;
		stream_out_empty				:BUFFER	STD_LOGIC_VECTOR(EOW DOWNTO  0);
		stream_out_valid				:BUFFER	STD_LOGIC
	);
	END COMPONENT;
`endif

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
				stream_out_empty				<= B"00";
				stream_out_valid				<= '0';
			ELSIF (transfer_data = '1') THEN
				stream_out_data				<= converted_data;
				stream_out_startofpacket	<= converted_startofpacket;
				stream_out_endofpacket		<= converted_endofpacket;
				stream_out_empty				<= converted_empty;
				stream_out_valid				<= converted_valid;
			END IF;
		END IF;
	END PROCESS;


	-- Internal Registers
	PROCESS (clk)
	BEGIN
		IF clk'EVENT AND clk = '1' THEN
			IF (reset = '1') THEN
				data								<=	(OTHERS => '0');
				startofpacket					<= '0';
				endofpacket						<= '0';
				empty								<=  (OTHERS => '0');
				valid								<= '0';
			ELSIF (stream_in_ready = '1') THEN
				data								<= stream_in_data;
				startofpacket					<= stream_in_startofpacket;
				endofpacket						<= stream_in_endofpacket;
				empty								<= stream_in_empty;
				valid								<= stream_in_valid;
			ELSIF (transfer_data = '1') THEN
				data								<=  (OTHERS => '0');
				startofpacket					<= '0';
				endofpacket						<= '0';
				empty								<=  (OTHERS => '0');
				valid								<= '0';
			END IF;
		END IF;
	END PROCESS;


-- *****************************************************************************
-- *                            Combinational Logic                            *
-- *****************************************************************************

	-- Output Assignments
	stream_in_ready <= stream_in_valid AND ( NOT valid OR transfer_data);

	-- Internal Assignments
	transfer_data <= NOT stream_out_valid OR 
													(stream_out_ready AND stream_out_valid);

`ifdef USE_Y_TO_RGB
	converted_data(23 DOWNTO 16) 	<= data( 7 DOWNTO  0);
	converted_data(15 DOWNTO  8) 	<= data( 7 DOWNTO  0);
	converted_data( 7 DOWNTO  0) 	<= data( 7 DOWNTO  0);
	converted_startofpacket 		<= startofpacket;
	converted_endofpacket 			<= endofpacket;
	converted_empty 					<= empty;
	converted_valid 					<= valid;
`endif

-- *****************************************************************************
-- *                          Component Instantiations                         *
-- *****************************************************************************

`ifdef USE_Y_TO_RGB
`elsif USE_YCRCB_TO_RGB
	YCrCb_to_RGB : altera_up_YCrCb_to_RGB_converter 
	PORT MAP (
		-- Inputs
		clk								=> clk,
		clk_en							=> transfer_data,
		reset								=> reset,
	
		Y									=> data( 7 DOWNTO  0),
		Cr									=> data(23 DOWNTO 16),
		Cb									=> data(15 DOWNTO  8),
		stream_in_startofpacket		=> startofpacket,
		stream_in_endofpacket		=> endofpacket,
		stream_in_empty				=> empty,
		stream_in_valid				=> valid,
	
		-- Bidirectionals
	
		-- Outputs
		R									=> converted_data(23 DOWNTO 16),
		G									=> converted_data(15 DOWNTO  8),
		B									=> converted_data( 7 DOWNTO  0),
		stream_out_startofpacket	=> converted_startofpacket,
		stream_out_endofpacket		=> converted_endofpacket,
		stream_out_empty				=> converted_empty,
		stream_out_valid				=> converted_valid
	);

`else
	RGB_to_YCrCb : altera_up_RGB_to_YCrCb_converter 
	PORT MAP (
		-- Inputs
		clk								=> clk,
		clk_en							=> transfer_data,
		reset								=> reset,
	
		R									=> data(23 DOWNTO 16),
		G									=> data(15 DOWNTO  8),
		B									=> data( 7 DOWNTO  0),
		stream_in_startofpacket		=> startofpacket,
		stream_in_endofpacket		=> endofpacket,
		stream_in_empty				=> empty,
		stream_in_valid				=> valid,
	
		-- Bidirectionals
	
		-- Outputs
		Y									=> converted_data( 7 DOWNTO  0),
`ifdef USE_RGB_TO_YCRCB
		Cr									=> converted_data(23 DOWNTO 16),
		Cb									=> converted_data(15 DOWNTO  8),
`else
`endif
		stream_out_startofpacket	=> converted_startofpacket,
		stream_out_endofpacket		=> converted_endofpacket,
		stream_out_empty				=> converted_empty,
		stream_out_valid				=> converted_valid
	);

`endif


END Behaviour;
