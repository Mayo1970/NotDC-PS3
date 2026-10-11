// The cycle block of dc/sh4/rec_v2/decoder.cpp before bundle 8 (commit ad7c1ab): the reference for JIT IDLE RULE X30.
	//cycle tricks
	{
		//Small-n-simple idle loop detector :p
		if (state.info.has_readm && !state.info.has_writem && !state.info.has_fpu && block.opcodes<6)
		{
			if (block.BlockType==BET_Cond_0 || block.BlockType==BET_Cond_1)
			{
				block.cycles*=30;
				// Plan 5.25: with JIT IDLE SKIP, this backward branch may end the timeslice.
				block.idle_loop=block.BranchBlock<=block.start && idle_turn_is_pure(block);
			}

			if (block.BranchBlock==block.start)
			{
				block.cycles*=10;
			}
		}

		//if in syscalls area (ip.bin etc) skip fast :p
		if ((block.start&0x1FFF0000)==0x0C000000)
		{
			if (block.start&0x8000)
			{
				//ip.bin (boot loader/img etc)
				block.cycles*=15;
			}
			else
			{
				//syscalls
				block.cycles*=5;
			}
			
		}
		//make sure we don't use wayy-too-many cycles ;p
		block.cycles=min(block.cycles,max_cycles);
	}
