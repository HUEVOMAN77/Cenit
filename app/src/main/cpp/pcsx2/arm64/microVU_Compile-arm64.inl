// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "Config.h"
#include "common/FileSystem.h"
#include "common/Path.h"


//------------------------------------------------------------------
// Messages Called at Execution Time
//------------------------------------------------------------------

static inline void mVUbadOp0  (u32 prog, u32 pc) { Console.Error("microVU0 Warning: Bad opcode [%04x] [%03d]", pc, prog); }
static inline void mVUbadOp1  (u32 prog, u32 pc) { Console.Error("microVU1 Warning: Bad opcode [%04x] [%03d]", pc, prog); }

//------------------------------------------------------------------
// Program Range Checking
//------------------------------------------------------------------

__fi void mVUcheckIsSame(mV)
{
	if (mVU.prog.isSame == -1)
		mVU.prog.isSame = !memcmp((u8*)mVUcurProg.data, mVU.regs().Micro, mVU.microMemSize);
	if (mVU.prog.isSame == 0)
	{
		mVUcacheProg(mVU, *mVU.prog.cur);
		mVU.prog.isSame = 1;
	}
}

void mVUsetupRange(microVU& mVU, s32 pc, bool isStartPC)
{
	std::deque<microRange>*& ranges = mVUcurProg.ranges;
	if (pc > (s64)mVU.microMemSize)
	{
		Console.Error("microVU%d: PC outside of VU memory PC=0x%04x", mVU.index, pc);
		pxFail("microVU: PC out of VU memory");
	}

	const s32 cur_pc = (!isStartPC && mVUrange.start > pc && pc == 0) ? mVU.microMemSize : pc;

	if (isStartPC)
	{
		for (auto it = ranges->begin(); it != ranges->end(); ++it)
		{
			if ((cur_pc >= it->start) && (cur_pc <= it->end))
			{
				if (it->start != it->end)
				{
					microRange mRange = {it->start, it->end};
					ranges->erase(it);
					ranges->push_front(mRange);
					return;
				}
			}
		}
	}
	else if (mVUrange.end >= cur_pc)
		return;

	if (doWholeProgCompare)
		mVUcheckIsSame(mVU);

	if (isStartPC)
	{
		microRange mRange = {cur_pc, -1};
		ranges->push_front(mRange);
		return;
	}

	if (mVUrange.start <= cur_pc)
	{
		mVUrange.end = cur_pc;
		s32 rStart = mVUrange.start;
		s32 rEnd = mVUrange.end;
		for (auto it = ranges->begin() + 1; it != ranges->end();)
		{
			if (((it->start >= rStart) && (it->start <= rEnd)) ||
				((it->end >= rStart) && (it->end <= rEnd)))
			{
				mVUrange.start = rStart = std::min(it->start, rStart);
				mVUrange.end = rEnd = std::max(it->end, rEnd);
				it = ranges->erase(it);
			}
			else
				it++;
		}
	}
	else
	{
		mVUrange.end = mVU.microMemSize;
		microRange mRange = {0, cur_pc};
		ranges->push_front(mRange);
	}

	if (!doWholeProgCompare)
		mVUcacheProg(mVU, *mVU.prog.cur);
}

//------------------------------------------------------------------
// Pipeline State Helpers (platform-independent)
//------------------------------------------------------------------

__fi u8 optimizeReg(u8 rState) { return (rState == 1) ? 0 : rState; }
__fi u8 calcCycles(u8 reg, u8 x) { return ((reg > x) ? (reg - x) : 0); }
__fi u8 tCycles(u8 dest, u8 src) { return std::max(dest, src); }
__fi void incP(mV) { mVU.p ^= 1; }
__fi void incQ(mV) { mVU.q ^= 1; }

// Optimizes the end pipeline state — collapses cycles-remaining==1 to 0, since
// mVU's block loop auto-decrements at entry so 1 is equivalent to 0. Without
// this, pipeline-state-hashed blocks get distinct variants for every cycle
// delta, exploding the block cache. Ported verbatim from x86 microVU_Compile.inl.
void mVUoptimizePipeState(mV)
{
	for (int i = 0; i < 32; i++)
	{
		mVUregs.VF[i].x = optimizeReg(mVUregs.VF[i].x);
		mVUregs.VF[i].y = optimizeReg(mVUregs.VF[i].y);
		mVUregs.VF[i].z = optimizeReg(mVUregs.VF[i].z);
		mVUregs.VF[i].w = optimizeReg(mVUregs.VF[i].w);
	}
	for (int i = 0; i < 16; i++)
	{
		mVUregs.VI[i] = optimizeReg(mVUregs.VI[i]);
	}
	if (mVUregs.q) { mVUregs.q = optimizeReg(mVUregs.q); if (!mVUregs.q) { incQ(mVU); } }
	if (mVUregs.p) { mVUregs.p = optimizeReg(mVUregs.p); if (!mVUregs.p) { incP(mVU); } }
	mVUregs.r = 0; // No stalls on R-reg — safe to discard.
}

// Advance pipeline cycles by x instructions. Ported verbatim from x86.
void mVUincCycles(mV, int x)
{
	mVUcycles += x;
	// VF[0] is a constant (0,0,0,1) — skip.
	for (int z = 31; z > 0; z--)
	{
		mVUregs.VF[z].x = calcCycles(mVUregs.VF[z].x, x);
		mVUregs.VF[z].y = calcCycles(mVUregs.VF[z].y, x);
		mVUregs.VF[z].z = calcCycles(mVUregs.VF[z].z, x);
		mVUregs.VF[z].w = calcCycles(mVUregs.VF[z].w, x);
	}
	// VI[0] is constant (0) — skip.
	for (int z = 15; z > 0; z--)
	{
		mVUregs.VI[z] = calcCycles(mVUregs.VI[z], x);
	}
	if (mVUregs.q)
	{
		if (mVUregs.q > 4)
		{
			mVUregs.q = calcCycles(mVUregs.q, x);
			if (mVUregs.q <= 4)
				mVUinfo.doDivFlag = 1;
		}
		else
		{
			mVUregs.q = calcCycles(mVUregs.q, x);
		}
		if (!mVUregs.q)
			incQ(mVU);
	}
	if (mVUregs.p)
	{
		mVUregs.p = calcCycles(mVUregs.p, x);
		if (!mVUregs.p || mVUregsTemp.p)
			incP(mVU);
	}
	if (mVUregs.xgkick)
	{
		mVUregs.xgkick = calcCycles(mVUregs.xgkick, x);
		if (!mVUregs.xgkick)
		{
			mVUinfo.doXGKICK = 1;
			mVUinfo.XGKICKPC = xPC;
		}
	}
	mVUregs.r = calcCycles(mVUregs.r, x);
}

// Helper: set xVar to 1 if VFreg1 and VFreg2 reference the same VF reg and
// any of the X/Y/Z/W components are touched by both. Ported from x86.
static __fi void cmpVFregs(microVFreg& VFreg1, microVFreg& VFreg2, bool& xVar)
{
	if (VFreg1.reg == VFreg2.reg)
	{
		if ((VFreg1.x && VFreg2.x) || (VFreg1.y && VFreg2.y)
		 || (VFreg1.z && VFreg2.z) || (VFreg1.w && VFreg2.w))
		{
			xVar = 1;
		}
	}
}

void mVUsetCycles(mV)
{
	mVUincCycles(mVU, mVUstall);

	// If upper Op && lower Op write to same VF reg: either make Lower skip its
	// VF write (noWriteVF) or mark it a NOP entirely when Lower has no other
	// side effects.
	if ((mVUregsTemp.VFreg[0] == mVUregsTemp.VFreg[1]) && mVUregsTemp.VFreg[0])
	{
		if (mVUregsTemp.r || mVUregsTemp.VI)
			mVUlow.noWriteVF = true;
		else
			mVUlow.isNOP = true;
	}

	// If Lower reads a VF reg that Upper writes, Upper's semantic output must
	// be visible to Lower → run Lower first (swapOps).
	if ((mVUlow.VF_read[0].reg || mVUlow.VF_read[1].reg) && mVUup.VF_write.reg)
	{
		cmpVFregs(mVUup.VF_write, mVUlow.VF_read[0], mVUinfo.swapOps);
		cmpVFregs(mVUup.VF_write, mVUlow.VF_read[1], mVUinfo.swapOps);
	}

	// If swapOps is set AND Upper also reads a VF reg that Lower writes,
	// snapshot the VF reg before Lower runs so Upper sees pre-Lower
	// state (backupVF).
	if (mVUinfo.swapOps && ((mVUup.VF_read[0].reg || mVUup.VF_read[1].reg) && mVUlow.VF_write.reg))
	{
		cmpVFregs(mVUlow.VF_write, mVUup.VF_read[0], mVUinfo.backupVF);
		cmpVFregs(mVUlow.VF_write, mVUup.VF_read[1], mVUinfo.backupVF);
	}

	mVUregs.VF[mVUregsTemp.VFreg[0]].x = tCycles(mVUregs.VF[mVUregsTemp.VFreg[0]].x, mVUregsTemp.VF[0].x);
	mVUregs.VF[mVUregsTemp.VFreg[0]].y = tCycles(mVUregs.VF[mVUregsTemp.VFreg[0]].y, mVUregsTemp.VF[0].y);
	mVUregs.VF[mVUregsTemp.VFreg[0]].z = tCycles(mVUregs.VF[mVUregsTemp.VFreg[0]].z, mVUregsTemp.VF[0].z);
	mVUregs.VF[mVUregsTemp.VFreg[0]].w = tCycles(mVUregs.VF[mVUregsTemp.VFreg[0]].w, mVUregsTemp.VF[0].w);

	mVUregs.VF[mVUregsTemp.VFreg[1]].x = tCycles(mVUregs.VF[mVUregsTemp.VFreg[1]].x, mVUregsTemp.VF[1].x);
	mVUregs.VF[mVUregsTemp.VFreg[1]].y = tCycles(mVUregs.VF[mVUregsTemp.VFreg[1]].y, mVUregsTemp.VF[1].y);
	mVUregs.VF[mVUregsTemp.VFreg[1]].z = tCycles(mVUregs.VF[mVUregsTemp.VFreg[1]].z, mVUregsTemp.VF[1].z);
	mVUregs.VF[mVUregsTemp.VFreg[1]].w = tCycles(mVUregs.VF[mVUregsTemp.VFreg[1]].w, mVUregsTemp.VF[1].w);

	mVUregs.VI[mVUregsTemp.VIreg]      = tCycles(mVUregs.VI[mVUregsTemp.VIreg], mVUregsTemp.VI);

	mVUregs.q      = tCycles(mVUregs.q,      mVUregsTemp.q);
	mVUregs.p      = tCycles(mVUregs.p,      mVUregsTemp.p);
	mVUregs.r      = tCycles(mVUregs.r,      mVUregsTemp.r);
	mVUregs.xgkick = tCycles(mVUregs.xgkick, mVUregsTemp.xgkick);
	memset(&mVUregsTemp, 0, sizeof(mVUregsTemp));
}

//------------------------------------------------------------------
// Flag-Pass Analysis (ported from x86 microVU_Flags.inl)
//------------------------------------------------------------------
// Scans forward through instructions to determine which pipeline flags
// (sFlag/mFlag/cFlag) the next block reads in its first ~4 instructions.
// Sets mVUregs.needExactMatch bits (1/2/4) so block lookup can require
// an exact pipeline-state match for correctness.

#define shortBranchPass() \
	{ \
		if ((branch == 3) || (branch == 4)) /* Branches */ \
		{ \
			_mVUflagPass(mVU, aBranchAddr, sCount + found, found, v); \
			if (branch == 3) /* Non-conditional Branch */ \
				break; \
			branch = 0; \
		} \
		else if (branch == 5) /* JR/JARL */ \
		{ \
			if (sCount + found < 4) \
				mVUregs.needExactMatch |= 7; \
			break; \
		} \
		else /* E-Bit End */ \
			break; \
	}

// Scan instructions at startPC and check if they read any pipeline flags.
// Uses pass4 (recPass=3) on each Upper/Lower op to accumulate needExactMatch bits.
void _mVUflagPass(mV, u32 startPC, u32 sCount, u32 found, std::vector<u32>& v)
{
	for (u32 i = 0; i < v.size(); i++)
	{
		if (v[i] == startPC)
			return; // Prevent infinite recursion
	}
	v.push_back(startPC);

	int oldPC = iPC;
	int oldBranch = mVUbranch;
	int aBranchAddr = 0;
	iPC = startPC / 4;
	mVUbranch = 0;
	for (int branch = 0; sCount < 4; sCount += found)
	{
		mVUregs.needExactMatch &= 7;
		incPC(1);
		mVUopU(mVU, 3);
		found |= (mVUregs.needExactMatch & 8) >> 3;
		mVUregs.needExactMatch &= 7;
		if (curI & _Ebit_)
		{
			branch = 1;
		}
		if (curI & _Tbit_)
		{
			branch = 6;
		}
		if ((curI & _Dbit_) && doDBitHandling)
		{
			branch = 6;
		}
		if (!(curI & _Ibit_))
		{
			incPC(-1);
			mVUopL(mVU, 3);
			incPC(1);
		}

		if (branch >= 2)
		{
			shortBranchPass();
		}
		else if (branch == 1)
		{
			branch = 2;
		}
		if (mVUbranch)
		{
			branch = ((mVUbranch > 8) ? (5) : ((mVUbranch < 3) ? 3 : 4));
			incPC(-1);
			aBranchAddr = branchAddr(mVU);
			incPC(1);
			mVUbranch = 0;
		}
		incPC(1);
		if ((mVUregs.needExactMatch & 7) == 7)
			break;
	}
	iPC = oldPC;
	mVUbranch = oldBranch;
	mVUregs.needExactMatch &= 7;
	setCode();
}

void mVUflagPass(mV, u32 startPC, u32 sCount = 0, u32 found = 0)
{
	std::vector<u32> v;
	_mVUflagPass(mVU, startPC, sCount, found, v);
}

// Checks if the first ~4 instructions of the successor block(s) read flags,
// and sets needExactMatch bits accordingly so block lookup requires exact state.
void mVUsetFlagInfo(mV)
{
	if (noFlagOpts)
	{
		mVUregs.needExactMatch = 0x7;
		mVUregs.flagInfo = 0x0;
		return;
	}
	if (mVUbranch <= 2) // B/BAL
	{
		incPC(-1);
		mVUflagPass(mVU, branchAddr(mVU));
		incPC(1);

		mVUregs.needExactMatch &= 0x7;
	}
	else if (mVUbranch <= 8) // Conditional Branch
	{
		incPC(-1); // Branch Taken
		mVUflagPass(mVU, branchAddr(mVU));
		int backupFlagInfo = mVUregs.needExactMatch;
		mVUregs.needExactMatch = 0;

		incPC(4); // Branch Not Taken
		mVUflagPass(mVU, xPC);
		incPC(-3);

		mVUregs.needExactMatch |= backupFlagInfo;
		mVUregs.needExactMatch &= 0x7;
	}
	else // JR/JALR
	{
		if (!doConstProp || !mVUlow.constJump.isValid)
		{
			mVUregs.needExactMatch |= 0x7;
		}
		else
		{
			mVUflagPass(mVU, (mVUlow.constJump.regValue * 8) & (mVU.microMemSize - 8));
		}
		mVUregs.needExactMatch &= 0x7;
	}
}

//------------------------------------------------------------------
// Cycle Test (emits code to check remaining cycles)
//------------------------------------------------------------------

// Test remaining cycles; if insufficient, save block state via copyPLState +
// mVUendProgram(0) and exit to the dispatcher. Otherwise deduct cycles and
// continue into the block. Ported from x86 microVU_Compile.inl:449.
// The copyPLState + mVUendProgram(0) on early-exit is required so that
// a cycle-timeout block has its pipeline state saved; without it, the block
// manager would see stale pState on re-entry and create a new variant.
static void mVUtestCycles(mV, microFlagCycles& mFC)
{
	iPC = mVUstartPC;

	if (isVU0 && EmuConfig.Speedhacks.EECycleRate != 0 && (!EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Speedhacks.EECycleRate < 0))
	{
		switch (std::min(static_cast<int>(EmuConfig.Speedhacks.EECycleRate), static_cast<int>(mVUcycles)))
		{
			case -3: mVUcycles *= 2.0f;       break;
			case -2: mVUcycles *= 1.6666667f; break;
			case -1: mVUcycles *= 1.3333333f; break;
			case  1: mVUcycles /= 1.3f;       break;
			case  2: mVUcycles /= 1.8f;       break;
			case  3: mVUcycles /= 3.0f;       break;
			default: break;
		}
	}

	armMoveAddressToReg(a64::x8, &mVU.cycles);
	armAsm->Ldr(a64::w9, a64::MemOperand(a64::x8));
	if (EmuConfig.Gamefixes.VUSyncHack)
		armAsm->Subs(a64::w9, a64::w9, mVUcycles);
	else
		armAsm->Subs(a64::w9, a64::w9, 1);

	// If (cycles - check) is non-negative, there is budget — skip the early exit.
	a64::Label skip;
	armAsm->B(&skip, a64::pl); // pl = N clear = non-negative

	// Early exit path: save pipeline state then exit via mVUendProgram(0).
	armMoveAddressToReg(a64::x0, &mVUpBlock->pState);
	armEmitCall(mVU.copyPLState);
	if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack)
	{
		armAsm->Mov(a64::w9, mVUcycles);
		armAsm->Str(a64::w9, mVUstateMem(offsetof(VURegs, nextBlockCycles)));
	}
	mVUendProgram(mVU, &mFC, 0);

	armAsm->Bind(&skip);

	// Budget remains — deduct block cycles from mVU.cycles and fall through
	// into the block body. x8 still holds &mVU.cycles from the first
	// materialization above; the early-exit path that clobbers it tail-calls
	// mVUendProgram and never reaches here, so x8 is safe to reuse directly.
	armAsm->Ldr(a64::w9, a64::MemOperand(a64::x8));
	armAsm->Sub(a64::w9, a64::w9, mVUcycles);
	armAsm->Str(a64::w9, a64::MemOperand(a64::x8));
}

//------------------------------------------------------------------
// Execute VU Instruction (Upper + Lower)
//------------------------------------------------------------------

// Pre-populate NEON/GPR caches with VF/VI registers the next few ops will
// read. Ported from pcsx2/x86/microVU_Compile.inl:603-690. Runs once at
// the start of pass 2; iterates forward through mVUinfo until caches are
// nearly full, or an XGKICK / branch is encountered. Purely an
// optimization — skips pre-loaded regs on allocReg so subsequent ops
// reuse the cached data instead of re-loading from memory.
static void mvuPreloadRegisters(microVU& mVU, u32 endCount)
{
	static constexpr const int REQUIRED_FREE_NEON = 3;
	static constexpr const int REQUIRED_FREE_GPRS = 1;

	u32 vfs_loaded = 0;
	u32 vis_loaded = 0;

	for (int reg = 0; reg < mVU.regAlloc->getNeonCount(); reg++)
	{
		const int vf = mVU.regAlloc->getRegVF(reg);
		if (vf >= 0)
			vfs_loaded |= (1u << vf);
	}
	for (int reg = 0; reg < mVU.regAlloc->getGPRCount(); reg++)
	{
		const int vi = mVU.regAlloc->getRegVI(reg);
		if (vi >= 0)
			vis_loaded |= (1u << vi);
	}

	const u32 orig_pc = iPC;
	const u32 orig_code = mVU.code;
	int free_regs = mVU.regAlloc->getFreeNeonCount();
	int free_gprs = mVU.regAlloc->getFreeGPRCount();

	auto preloadVF = [&mVU, &vfs_loaded, &free_regs](u8 reg)
	{
		if (free_regs <= REQUIRED_FREE_NEON || reg == 0 || (vfs_loaded & (1u << reg)) != 0)
			return;
		mVU.regAlloc->clearNeeded(mVU.regAlloc->allocReg(reg));
		vfs_loaded |= (1u << reg);
		free_regs--;
	};

	auto preloadVI = [&mVU, &vis_loaded, &free_gprs](u8 reg)
	{
		if (free_gprs <= REQUIRED_FREE_GPRS || reg == 0 || (vis_loaded & (1u << reg)) != 0)
			return;
		mVU.regAlloc->clearNeeded(mVU.regAlloc->allocGPR(reg));
		vis_loaded |= (1u << reg);
		free_gprs--;
	};

	auto canPreload = [&free_regs, &free_gprs]() {
		return (free_regs >= REQUIRED_FREE_NEON || free_gprs >= REQUIRED_FREE_GPRS);
	};

	for (u32 x = 0; x < endCount && canPreload(); x++)
	{
		incPC(1);

		const microOp* info = &mVUinfo;
		if (info->doXGKICK)
			break;

		for (u32 i = 0; i < 2; i++)
		{
			preloadVF(info->uOp.VF_read[i].reg);
			preloadVF(info->lOp.VF_read[i].reg);
			if (info->lOp.VI_read[i].used)
				preloadVI(info->lOp.VI_read[i].reg);
		}

		const microVFreg& uvfr = info->uOp.VF_write;
		if (uvfr.reg != 0 && (!uvfr.x || !uvfr.y || !uvfr.z || !uvfr.w))
			preloadVF(uvfr.reg);

		const microVFreg& lvfr = info->lOp.VF_write;
		if (lvfr.reg != 0 && (!lvfr.x || !lvfr.y || !lvfr.z || !lvfr.w))
			preloadVF(lvfr.reg);

		if (info->lOp.branch)
			break;

		// Stop at the block's true end. endCount is the whole micro-memory size
		// (microMemSize/8), not the block length — the analysis loop above only
		// clears + populates IRinfo.info[] for the block's own instructions and
		// breaks at isEOB. Without the matching isEOB break here, an E-bit-
		// terminated block (no lOp.branch) walks the preload past its own end
		// into info[] entries left over from a PRIOR compile, preloading VF/VI
		// the program never touches. Harmless at runtime (an unused reg load),
		// but it makes the emitted shape depend on compile history — non-
		// deterministic codegen that the persisted-JIT ABI digest must not see.
		// In a cold cache those stale entries read zero (reg 0 → skipped), so
		// this only suppresses the spurious warm-state preloads; the cold shape
		// (what the digest pins) is unchanged. Diverges from x86, which has the
		// same latent over-read but no on-disk cache that needs deterministic
		// emit. Mirrors the flagInfo "clear each compile" fix in mVUinitFirstPass.
		if (info->isEOB)
			break;
	}

	iPC = orig_pc;
	mVU.code = orig_code;
}

__ri void doUpperOp(mV) { mVUopU(mVU, 1); mVUdivSet(mVU); }
__ri void doLowerOp(mV) { incPC(-1); mVUopL(mVU, 1); incPC(1); }
__ri void flushRegs(mV) { if (!doRegAlloc) mVU.regAlloc->flushAll(); }

void doIbit(mV)
{
	if (mVUup.iBit)
	{
		incPC(-1);
		u32 tempI = curI;
		if (CHECK_VU_OVERFLOW(mVU.index) && ((curI & 0x7fffffff) >= 0x7f800000))
			tempI = (0x80000000 & curI) | 0x7f7fffff;

		armAsm->Mov(a64::w9, tempI);
		armAsm->Str(a64::w9, mVUstateMem(offsetof(VURegs, VI) + REG_I * sizeof(REG_VI)));
		incPC(1);
	}
}

// Ported from x86 microVU_Compile.inl:doSwapOp — runs Lower before Upper, and
// when Upper reads a VF reg Lower writes, snapshots the pre-Lower VF value via
// an XOR-swap so Upper observes the original value.
static void doSwapOp(mV)
{
	if (mVUinfo.backupVF && !mVUlow.noWriteVF)
	{
		DevCon.WriteLn(Color_Green, "microVU%d: Backing Up VF Reg [%04x]", getIndex, xPC);

		// Alloc t1 = current value of Lower's VF_write reg (pre-Lower).
		const a64::VRegister t1 = mVU.regAlloc->allocReg(mVUlow.VF_write.reg);
		const a64::VRegister t2 = mVU.regAlloc->allocReg();
		armAsm->Mov(t2.V16B(), t1.V16B()); // t2 = pre-Lower value
		mVU.regAlloc->clearNeeded(t1);

		mVUopL(mVU, 1); // Lower writes new value to VF_write

		// XOR-swap: t2 gets new value, VF_write reg (via t3) gets old value,
		// so Upper sees the pre-Lower state.
		const a64::VRegister t3 = mVU.regAlloc->allocReg(mVUlow.VF_write.reg, mVUlow.VF_write.reg, 0xf, false);
		armAsm->Eor(t2.V16B(), t2.V16B(), t3.V16B());
		armAsm->Eor(t3.V16B(), t3.V16B(), t2.V16B());
		armAsm->Eor(t2.V16B(), t2.V16B(), t3.V16B());
		mVU.regAlloc->clearNeeded(t3);

		incPC(1);
		doUpperOp(mVU); // Upper reads VF_write reg with old value

		// Write the new value (held in t2) back to VF_write reg.
		const a64::VRegister t4 = mVU.regAlloc->allocReg(-1, mVUlow.VF_write.reg, 0xf);
		armAsm->Mov(t4.V16B(), t2.V16B());
		mVU.regAlloc->clearNeeded(t4);
		mVU.regAlloc->clearNeeded(t2);
	}
	else
	{
		mVUopL(mVU, 1);
		incPC(1);
		flushRegs(mVU);
		doUpperOp(mVU);
	}
}

// Runtime D-bit handler: if VU0/VU1 FBRST has the D-interrupt bit set, raise
// VPU_STAT and INTCINTERRUPT flags, end the program, otherwise fall through.
// Mirrors x86 microVU_Compile.inl:560-576.
static void mVUDoDBit(microVU& mVU, microFlagCycles* mFC)
{
	// Flush regalloc before the conditional skip — mVUDTendProgram's internal
	// flushAll emits the stores INSIDE the branch, so the silent path would
	// otherwise drop dirty regs (the lower op of the D-bit pair). Same pattern
	// as the branch-side D-bit handler in microVU_Branch-arm64.inl:540.
	mVU.regAlloc->flushAll(false);

	a64::Label noDBit;
	armMoveAddressToReg(a64::x8, (mVU.index && THREAD_VU1)
		? (void*)&vu1Thread.vuFBRST : (void*)&VU0.VI[REG_FBRST].UL);
	armAsm->Ldr(a64::w9, a64::MemOperand(a64::x8));
	armAsm->Tst(a64::w9, isVU1 ? 0x400 : 0x4);
	armAsm->B(&noDBit, a64::eq);

	if (!isVU1 || !THREAD_VU1)
	{
		armMoveAddressToReg(a64::x8, &VU0.VI[REG_VPU_STAT].UL);
		armAsm->Ldr(a64::w9, a64::MemOperand(a64::x8));
		armAsm->Orr(a64::w9, a64::w9, isVU1 ? 0x200 : 0x2);
		armAsm->Str(a64::w9, a64::MemOperand(a64::x8));

		armAsm->Ldr(a64::w9, mVUstateMem(offsetof(VURegs, flags)));
		armAsm->Orr(a64::w9, a64::w9, VUFLAG_INTCINTERRUPT);
		armAsm->Str(a64::w9, mVUstateMem(offsetof(VURegs, flags)));
	}

	incPC(1);
	mVUDTendProgram(mVU, mFC, 1);
	incPC(-1);

	armAsm->Bind(&noDBit);
}

// Runtime T-bit handler. Same pattern as mVUDoDBit but tests the T bit.
// Mirrors x86 microVU_Compile.inl:578-595.
static void mVUDoTBit(microVU& mVU, microFlagCycles* mFC)
{
	// Flush regalloc before the conditional skip — mVUDTendProgram's internal
	// flushAll emits the stores INSIDE the branch, so the silent path would
	// otherwise drop dirty regs (the lower op of the T-bit pair). Same pattern
	// as the branch-side T-bit handler in microVU_Branch-arm64.inl:569.
	mVU.regAlloc->flushAll(false);

	a64::Label noTBit;
	armMoveAddressToReg(a64::x8, (mVU.index && THREAD_VU1)
		? (void*)&vu1Thread.vuFBRST : (void*)&VU0.VI[REG_FBRST].UL);
	armAsm->Ldr(a64::w9, a64::MemOperand(a64::x8));
	armAsm->Tst(a64::w9, isVU1 ? 0x800 : 0x8);
	armAsm->B(&noTBit, a64::eq);

	if (!isVU1 || !THREAD_VU1)
	{
		armMoveAddressToReg(a64::x8, &VU0.VI[REG_VPU_STAT].UL);
		armAsm->Ldr(a64::w9, a64::MemOperand(a64::x8));
		armAsm->Orr(a64::w9, a64::w9, isVU1 ? 0x400 : 0x4);
		armAsm->Str(a64::w9, a64::MemOperand(a64::x8));

		armAsm->Ldr(a64::w9, mVUstateMem(offsetof(VURegs, flags)));
		armAsm->Orr(a64::w9, a64::w9, VUFLAG_INTCINTERRUPT);
		armAsm->Str(a64::w9, mVUstateMem(offsetof(VURegs, flags)));
	}

	incPC(1);
	mVUDTendProgram(mVU, mFC, 1);
	incPC(-1);

	armAsm->Bind(&noTBit);
}

void mVUexecuteInstruction(mV)
{
	if (mVUlow.isNOP)
	{
		incPC(1);
		doUpperOp(mVU);
		flushRegs(mVU);
		doIbit(mVU);
	}
	else if (!mVUinfo.swapOps)
	{
		incPC(1);
		doUpperOp(mVU);
		flushRegs(mVU);
		doLowerOp(mVU);
	}
	else
	{
		doSwapOp(mVU);
	}
	flushRegs(mVU);
}

//------------------------------------------------------------------
// Init helpers
//------------------------------------------------------------------

__fi void startLoop(mV)
{
	memset(&mVUinfo, 0, sizeof(mVUinfo));
	memset(&mVUregsTemp, 0, sizeof(mVUregsTemp));
}

__fi void mVUinitConstValues(microVU& mVU)
{
	for (int i = 0; i < 16; i++)
	{
		mVUconstReg[i].isValid  = 0;
		mVUconstReg[i].regValue = 0;
	}
	mVUconstReg[15].isValid = mVUregs.vi15v;
	mVUconstReg[15].regValue = mVUregs.vi15v ? mVUregs.vi15 : 0;
}

__fi void mVUinitFirstPass(mV, uptr pState, u8* thisPtr, int sbFrame)
{
	mVUstartPC = iPC;
	mVUbranch  = 0;
	mVUcount   = 0;
	mVUcycles  = 0;
	mVU.p      = 0;
	mVU.q      = 0;

	if ((uptr)&mVUregs != pState)
		memcpy((u8*)&mVUregs, (u8*)pState, sizeof(microRegInfo));
	if ((uptr)&mVU.prog.lpState != pState)
		memcpy((u8*)&mVU.prog.lpState, (u8*)pState, sizeof(microRegInfo));

	mVUblock.x86ptrStart = thisPtr;
	// hostEntry mirrors x86ptrStart: the JIT stores its code-cache slot in both.
	// The indirection is kept so an alternate code source can repoint hostEntry
	// at a prepared block without disturbing the code-cache slot tracking.
	mVUblock.hostEntry = thisPtr;

	// Create block manager if needed, then add this block. Both conditions are
	// invariant violations — the caller (mVUcompile) unconditionally proceeds
	// into the first-pass loop and dereferences mVUpBlock, so a bare return
	// here would just defer a NULL/stale deref into UB. Fail fast instead.
	pxAssertRel(mVU.prog.cur, "microVU: mVUinitFirstPass with NULL mVU.prog.cur");
	blockCreate(mVUstartPC / 2);
	// Cenit VU Superblock Engine — identidad de variante: el bit de rasguño se
	// hornea SOLO aqui, sobre blockType (que en una entrada de despacho es 0),
	// de modo que la copia que el gestor hace de pState en add() (memcpy del
	// microBlock entero a su propio microBlockLink) es la UNICA que lo lleva.
	// La linea original de abajo (mVUregs.blockType = 0) lo borra justo despues
	// del add, antes de que el analisis o lpState puedan verlo. Las busquedas
	// normales comparan quick64/96B completos con una clave limpia, asi que la
	// variante es inalcanzable por enlace normal: el control vive en SbResolve.
	// Ademas, el enlace del gestor es PROPIO (pStateEnd propio): el codigo
	// M-bit/JR del area escribe mVUpBlock->pStateEnd y compartirlo con el
	// bloque normal lo corromperia cruzado.
	if (sbFrame >= 0)
		mVUregs.blockType |= mVUSuperblock::kSbScratchVariant;
	mVUpBlock = mVUblocks[mVUstartPC / 2]->add(mVU, &mVUblock);
	pxAssertRel(mVUpBlock, "microVU: mVUpBlock NULL after blockManager::add");
	// Register this block (manager copy + host entry) with the VU program-cache
	// recorder so the emitted code can be persisted and reloaded across runs.
	// Con frame variante NO: un superbloque nunca pisa el disco (Fase 3 del
	// documento: la variante es codigo de validacion, no cache persistible).
	if (sbFrame < 0)
		mVUPersist::OnBlockCompiled(mVU, mVUpBlock, thisPtr, mVUstartPC * 4);
	// needExactMatch se deriva del blockType del enlace LIMPIO (& ~mask): los
	// bits de rasguño no deben forzar igualdad exacta artificial en el analisis
	// (en la variante el blockType original es 0 -> needExactMatch 0, igual que
	// el bloque normal).
	mVUregs.needExactMatch = (mVUpBlock->pState.blockType & ~mVUSuperblock::kSbScratchMask) ? 7 : 0;
	mVUregs.blockType = 0;
	mVUregs.viBackUp  = 0;
	mVUregs.flagInfo  = 0; // Must be cleared each compile: mVUsetFlags OR-updates
	                       // flagInfo at end of compile, so stale bits accumulate
	                       // across blocks, making every compile hash to a new
	                       // pipeline state and create a new variant.
	mVUsFlagHack = CHECK_VU_FLAGHACK;

	mVUinitConstValues(mVU);
}

__fi void mVUcheckBadOp(mV)
{
	if (mVUinfo.isBadOp && mVU.code != 0x8000033c)
	{
		mVUinfo.isEOB = true;
		DevCon.Warning("microVU Warning: Block contains an illegal opcode...");
	}
}

__fi void eBitPass1(mV, int& branch)
{
	if (mVUregs.blockType != 1)
	{
		branch = 1;
		mVUup.eBit = true;
	}
}

__fi void branchWarning(mV)
{
	incPC(-2);
	if (mVUup.eBit && mVUbranch)
	{
		incPC(2);
		mVUlow.isNOP = true;
	}
	else
		incPC(2);

	if (mVUinfo.isBdelay && !mVUlow.evilBranch)
	{
		if (mVUlow.VI_write.reg && mVUlow.VI_write.used && !mVUlow.readFlags)
		{
			mVUlow.backupVI = true;
			mVUregs.viBackUp = mVUlow.VI_write.reg;
		}
	}
}

__ri void eBitWarning(mV)
{
	incPC(2);
	if (curI & _Ebit_)
		mVUregs.blockType = 1;
	incPC(-2);
}

void mVUdebugPrintBlocks(mV, bool isEndPC) {}

//------------------------------------------------------------------
// Main Compile Function
//------------------------------------------------------------------

void* mVUcompile(microVU& mVU, u32 startPC, uptr pState)
{
	microFlagCycles mFC;
	// armAsm is managed by mVUexecute/mVUcompileJIT — must be active here.
	pxAssert(armAsm);
	u8* thisPtr = armGetCurrentCodePointer();

	// === Cenit VU Superblock Engine — frame de compilación variante ===
	// mVU.sbSlot >= 0 SOLO cuando este mVUcompile es la compilación variante
	// armada por SbArmCompile (el hook de mVUexecute<1> lo deposita antes de
	// reentrar). Es una local de esta llamada: ninguna compilación recursiva
	// (normBranchCompile de un corte terminal, sub-bloques M-bit) puede ver
	// el frame — se anida sbSlot a -1 para las recursiones y se restaura al
	// volver. sbSpanCycles[] acumula los ciclos cobrados por tramo: el paso 2
	// de una región fusionada necesita reproducir el valor de mVU.cycles que
	// la cadena normal tendría en cada punto de unión (el contador global del
	// episodio alimenta los contadores XGKICK en código generado), y cada
	// tramo consume exactamente los ciclos que su análisis contabilizó.
	const int sbFrame = mVU.sbSlot;
	const u32 sbOuterActive = mVU.sbActive;
	mVU.sbSlot = -1;
	// sbActive cubre ESTE mVUcompile y solo este: paso 2 y emision terminal
	// incluidos. La sonda (forma/contadores de bloque/aristas) no debe ver
	// nunca el area fusionada, o contaminaria los datos de elegibilidad del
	// bloque normal. Las compilaciones recursivas desde el corte terminal
	// (normBranchCompile de un sucesor aun sin compilar, continuaciones M-bit)
	// son bloques NORMALES: se anidan con sbActive=0, registran su propia
	// forma/aristas y caminan las banderas por PC; al volver restauran el 1.
	mVU.sbActive = (sbFrame >= 0) ? 1 : 0;
	u32 sbSpanCycles[mVUSuperblock::kSbMaxJunctions + 2];
	u32 sbSpanCount = 0;
	if (sbFrame >= 0)
	{
		mVU.sbJunctions = 0;
		mVU.sbOpCount   = 0;
		mVU.sbKick      = 0;
	}

	const u32 endCount = (((microRegInfo*)pState)->blockType) ? 1 : (mVU.microMemSize / 8);

	// === First Pass (Analysis) ===
	iPC = startPC / 4;
	mVUsetupRange(mVU, startPC, 1);
	mVU.regAlloc->reset(false);
	mVUinitFirstPass(mVU, pState, thisPtr, sbFrame);
	mVUbranch = 0;

	// Fase 1.5 (sonda ON): motivo por el que el primer paso corta el bloque.
	// 4 = cayo del for por endCount (fin del programa); los breaks lo pisan
	// con 1/2/3. Coste: un par de stores a una local por COMPILACION de
	// bloque (no por ejecucion) — el unico lector esta tras el guard de la
	// sonda, y con OFF el compilador se queda solo con los stores muertos.
	u16 probeCut = 4;

	for (int branch = 0; mVUcount < endCount;)
	{
		incPC(1);
		startLoop(mVU);
		mVUincCycles(mVU, 1);
		mVUopU(mVU, 0); // Upper analysis
		mVUcheckBadOp(mVU);

		if (curI & _Ebit_)
		{
			eBitPass1(mVU, branch);
			// VU0 end of program MAC results can be read by COP2, so best to
			// make sure the last instance is valid. Needed for State of Emergency 2
			// and Driving Emotion Type-S (mirrors x86 microVU_Compile.inl:711-717).
			if (isVU0)
				mVUregs.needExactMatch |= 7;
		}

		// M-bit: VU0 sync point with EE. If the previous instruction was also
		// M-bit, skip — no need to re-sync. Mirrors x86 microVU_Compile.inl:720-735.
		if ((curI & _Mbit_) && isVU0)
		{
			if (xPC > 0)
			{
				incPC(-2);
				if (!(curI & _Mbit_))
				{
					incPC(2);
					mVUup.mBit = true;
				}
				else
				{
					incPC(2);
				}
			}
			else
			{
				mVUup.mBit = true;
			}
		}

		if (curI & _Ibit_)
		{
			mVUlow.isNOP = true;
			mVUup.iBit = true;
			if (EmuConfig.Gamefixes.IbitHack)
			{
				mVUsetupRange(mVU, xPC, false);
				if (branch < 2)
					mVUsetupRange(mVU, xPC + 4, true);
			}
		}
		else
		{
			incPC(-1);
			if (EmuConfig.Gamefixes.IbitHack)
			{
				// Ignore IADDI/IADDIU/ISUBU/ILW/ISW/LQ/SQ on the lower slot when
				// IbitHack is active. Matches x86 microVU_Compile.inl:751-765.
				const u32 upper = (mVU.code >> 25);
				if (upper == 0x1 || upper == 0x0 || upper == 0x4 || upper == 0x5
					|| upper == 0x8 || upper == 0x9
					|| (upper == 0x40 && (mVU.code & 0x3F) == 0x32))
				{
					incPC(1);
					mVUsetupRange(mVU, xPC, false);
					if (branch < 2)
						mVUsetupRange(mVU, xPC + 2, true);
					incPC(-1);
				}
			}
			mVUopL(mVU, 0);
			incPC(1);
		}

		if (curI & _Dbit_) { mVUup.dBit = true; }
		if (curI & _Tbit_) { mVUup.tBit = true; }
		mVUsetCycles(mVU);

		if (!mVUlow.isKick)
		{
			mVUregs.xgkickcycles += 1 + mVUstall;
			if (mVUlow.isMemWrite) { mVUlow.kickcycles = mVUregs.xgkickcycles; mVUregs.xgkickcycles = 0; }
		}
		else
		{
			mVUregs.xgkickcycles = 1;
			mVUlow.kickcycles = 0;
		}

		// Superblock (variante): cualquier kick dentro del area es observable
		// por VIF1/host -> el area entera se repele (regla del documento). El
		// countdown doXGKICK (efecto real del kick) y el opcode XGKICK entran
		// ambos aqui; el T/D-bit NO: fuera del par rama/delay se emiten op a op
		// igual que en cualquier bloque normal (la union limpia ya los excluye).
		if (sbFrame >= 0 && (mVUlow.isKick || mVUinfo.doXGKICK))
			mVU.sbKick = 1;

		mVUinfo.readQ = mVU.q;
		mVUinfo.writeQ = !mVU.q;
		mVUinfo.readP = mVU.p && isVU1;
		mVUinfo.writeP = !mVU.p && isVU1;
		// Superblock (variante): tabla de region — slot info[] de cada op
		// analizada, en orden de region. El area fusionada NO es contigua en
		// PC, y tanto las caminatas de banderas (Flags.inl) como la re-anclada
		// del paso 2 se indexan por esta tabla.
		if (sbFrame >= 0)
			mVU.sbOpSlot[mVU.sbOpCount++] = iPC / 2;
		mVUcount++;

		if (branch >= 2)
		{
			// === Superblock: consulta de union (continue-analysis) ===
			// En modo variante, un corte por rama incondicional limpia NO corta
			// el analisis: se continua a traves del objetivo con mVUregs tal
			// como normBranchCompile se lo habria entregado al sucesor (el
			// mVUsetFlagInfo de la iteracion de la rama ya ajusto
			// needExactMatch, y el enlace normal no normaliza nada mas de ahi).
			// El area entera se emitira despues como UN bloque: cero flushAll,
			// cero mVUsetupFlags, cero rotacion P/Q, cero salto por union.
			bool fused = false;
			if (sbFrame >= 0)
			{
				// Ventana de la rama: los dos pares se inspeccionan con incPC.
				// Se leen AMBOS slots de info (no solo el del delay slot): la
				// semantica del corte normal vive en la rama (bad/evil/eBit) y en
				// el delay (bits, VI_write), y la emision fusionada tiene que
				// reproducir la del par exacto de la cadena.
				incPC(-2);
				const u32  jBranch = mVUlow.branch;
				const u32  jCode   = mVU.code;
				const u32  jTarget = branchAddr(mVU);
				const bool jBadR   = mVUlow.badBranch;
				const bool jEvilR  = mVUlow.evilBranch;
				const bool jEBr    = mVUup.eBit;
				const bool jTBr    = mVUup.tBit;
				const bool jDBr    = mVUup.dBit;
				incPC(2);
				const u32 jDelay = curI;
				const bool clean =
					jBranch == 1 &&                                        // solo B (no BAL)
					!(jCode & (_Ebit_ | _Mbit_ | _Dbit_ | _Tbit_ | _Ibit_)) &&
					!(jDelay & (_Ebit_ | _Mbit_ | _Dbit_ | _Tbit_ | _Ibit_)) &&
					!jBadR && !jEvilR && !mVUlow.badBranch && !mVUlow.evilBranch &&
					// jEBr/jTBr/jDBr son el PAR de la rama (leidos con incPC(-2));
					// los ultimos tres son el delay slot (donde estamos). El corte
					// normal tolera un eBit acumulado en la rama poniendo
					// isNOP en el delay (:829) — aqui eso significaria emitir un
					// NOP por la mitad de la region: ni falta hace, se repele.
					!(jEBr || jTBr || jDBr) &&
					!mVUup.eBit && !mVUup.tBit && !mVUup.dBit &&
					mVUregs.xgkickcycles == 0;                             // sin kick pendiente
				// REVISITAS: si el objetivo cae sobre un slot info[] ya analizado
				// en esta region, el analisis continuaria por codigo que
				// startLoop() esta por memsetear de nuevo (los slots son
				// compartidos: la region dejaria de ser una secuencia definida y
				// sbOpSlot perderia el orden). Barato: la caminata solo ocurre en
				// uniones candidatas del modo variante.
				// E-bit en el PRIMER par del objetivo: la cadena lo resuelve
				// con blockType=1 (el sucesor se compila como bloque de un par
				// que termina por endCount). Dentro de la region eBitPass1 NO
				// pone branch (su guard ve blockType==1 del estado heredado) y
				// nada cortaria el analisis: el E-bit se emitiria como un op
				// normal y el programa seguiria mas alla del fin real. Se
				// repele la union (corte normal, que si enlaza con el bloque
				// monobloque de siempre).
				const u32 jTgtUp = ((const u32*)mVU.regs().Micro)[(jTarget / 4) + 1];
				bool reenter = false;
				for (u32 si = 0; !reenter && si < mVU.sbOpCount; si++)
					reenter = (mVU.sbOpSlot[si] == jTarget / 8);
				if (clean && !(jTgtUp & _Ebit_) && !reenter &&
						mVUSuperblock::SbAskJunction(sbFrame, jTarget, true,
							mVUcount, mVUcycles) >= 0)
				{
					fused = true;
					// El corte normal terminaria AQUI; la continue-analysis debe
					// reproducir el arranque FRESCO del sucesor: el bucle normal del
					// bloque sucesor entra con branch = 0 (la delay slot consumida),
					// mVUbranch = 0 (la del par ya fue consumida en :1116) y su
					// propio contador de uniones. Sin el branch = 0 el siguiente par
					// caeria de nuevo en el corte (branch==3) y rompiria el area tras
					// una sola instruccion del tramo nuevo.
					branch = 0;
					mVUbranch = 0;
					mVU.sbJunctions++;
					// Flush del kick pendiente al delay slot: IDENTICO al corte
					// normal (:964-968) — deja mVUlow.kickcycles del delay con
					// los ciclos acumulados, asi que la emision XGKICK_SYNC del
					// paso 2 sobre ese par sale en la misma posicion y con los
					// mismos valores que en la cadena normal.
					mVUlow.kickcycles = mVUregs.xgkickcycles;
					mVUregs.xgkickcycles = 0;
					// El sucesor normal reseteaba needExactMatch/viBackUp en su
					// mVUinitFirstPass (blockType limpio); la continue-analysis
					// debe reproducir ese arranque para que los scans de las
					// uniones siguientes y el enlace terminal calculen exactly
					// lo mismo que calcularia el bloque sucesor suelto.
					mVUregs.needExactMatch = 0;
					mVUregs.viBackUp = 0;
					// Tabla de tramos para el paso 2: indice de op del delay
					// slot (el siguiente op abre el tramo nuevo) y ciclos
					// acumulados del area hasta aqui (reintegro de presupuesto).
					mVU.sbJDelayOp[sbSpanCount] = mVU.sbOpCount - 1;
					sbSpanCycles[sbSpanCount] = mVUcycles;
					sbSpanCount++;
					// Continuar el analisis en el objetivo (lo que haria
					// normBranchCompile -> mVUcompile del sucesor, salvo que
					// NO se toca el gestor de bloques ni el registro del tramo).
					mVUsetupRange(mVU, jTarget, false);
					iPC = jTarget / 4; // palabra inferior del primer par objetivo
					setCode();
					continue;
				}
			}
			if (!fused)
			{
				mVUinfo.isEOB = true;
				if (branch == 3)
					mVUinfo.isBdelay = true;
				branchWarning(mVU);
				probeCut = 1; // rama/eBit: el bloque corta en delay slot
				if (mVUregs.xgkickcycles)
				{
					mVUlow.kickcycles = mVUregs.xgkickcycles;
					mVUregs.xgkickcycles = 0;
				}
				break;
			}
		}
		else if (branch == 1)
		{
			branch = 2;
		}

		if (mVUbranch) { mVUsetFlagInfo(mVU); eBitWarning(mVU); branch = 3; mVUbranch = 0; }

		if (mVUup.mBit && !branch && !mVUup.eBit)
		{
			mVUregs.needExactMatch |= 7;
			probeCut = 2; // M-bit: punto de sincronia con el EE
			if (mVUregs.xgkickcycles)
			{
				mVUlow.kickcycles = mVUregs.xgkickcycles;
				mVUregs.xgkickcycles = 0;
			}
			break;
		}

		if (mVUinfo.isEOB)
		{
			probeCut = 3; // EOB (opcode ilegal u otra senal de fin de bloque)
			if (mVUregs.xgkickcycles)
			{
				mVUlow.kickcycles = mVUregs.xgkickcycles;
				mVUregs.xgkickcycles = 0;
			}
			break;
		}

		incPC(1);
	}

	// Variante: cierra la tabla de prefijos de presupuesto (sbSpanCycles[j] es
	// el acumulado del area al terminar el tramo j; el ultimo tramo no tiene
	// union, su acumulado es el total). Indexado 0..sbSpanCount.
	if (sbFrame >= 0)
		sbSpanCycles[sbSpanCount] = mVUcycles;

	mVUregs.vi15 = 0;
	mVUregs.vi15v = 0;
	mVUsetFlags(mVU, mFC);
	mVUoptimizePipeState(mVU);

	// === Fase 1 VU block probe (VU1 only, sonda ON) ===
	// Tres instrucciones como PRIMERAS del bloque: ldr/add/str sobre el slot
	// (pc>>3)&2047 del array vuBlkExecCnt, direccionado SOLO con
	// [x24, #inm] (el pin macFlag del dispatcher; x8 = RXSCRATCH, fuera del
	// pool del allocator). No materializa ninguna dirección absoluta, así
	// que el recorder de persistencia no ve nada nuevo; solo se emite con la
	// sonda ON, que a la vez pausa la caché en disco (guards en
	// microVU_ProgCache-arm64.inl + recording forzado OFF en mVUinit/
	// mVUreset). ANTES de mVUtestCycles para que también cuenten las
	// entradas por salida de presupuesto (Remove() del BaseblockEx pisa la
	// primera palabra de 4 bytes del bloque: aquí es aceptable — Fase 1 es
	// medición, y OFF reconstruye todo vía ClearCPUExecutionCaches).
	if (mVUTraceProbe::IsEnabled() && isVU1 && sbFrame < 0)
	{
		// (sbFrame >= 0: el area fusionada NUNCA registra forma ni contador —
		// pisaria la forma del bloque normal yéndose a su tamaño real e
		// inflaría el contador de entradas del PC de entrada con despachos
		// resueltos por la variante; la elegibilidad se mide solo en la
		// cadena normal.)
		// Fase 1.5: forma del bloque (tabla C++, cero instrucciones emitidas).
		// mVUcount/mVUcycles salen del primer paso tal cual; probeCut es el
		// motivo de corte. Va aqui, y no junto al ldr/add/str, para no mezclar
		// datos de compilacion con la secuencia emitida.
		mVUTraceProbe::RecordBlockShape(1, startPC,
			static_cast<u16>(std::min<u32>(mVUcount, 0xffffu)),
			static_cast<u16>(std::min<u32>(mVUcycles, 0xffffu)),
			probeCut);
		const u32 off = mVUTraceProbe::SlotMemOffset(startPC);
		armAsm->Ldr(a64::x8, a64::MemOperand(gprMVUFlag, off));
		armAsm->Add(a64::x8, a64::x8, 1);
		armAsm->Str(a64::x8, a64::MemOperand(gprMVUFlag, off));
	}

	mVUtestCycles(mVU, mFC);

	// === Second Pass (Codegen) ===
	iPC = mVUstartPC;
	setCode();
	mVUbranch = 0;
	u32 x = 0;

	mvuPreloadRegisters(mVU, endCount);

	// Superblock (variante) — devolucion de presupuesto (parte 1 de 2). El
	// area fusionada NO es un bloque de la cadena normal: mVUtestCycles acabo
	// de cobrar los ciclos de TODA el area de una sola vez, mientras que la
	// cadena los deduce a la ENTRADA de cada bloque. Para que el valor que el
	// codigo generado lee de mVU.cycles (contadores XGKICK: xgkicklastcycle =
	// totalCycles - mVU.cycles + VU1.cycle) sea el MISMO que tendria la cadena
	// en cada instruccion, durante el tramo 0 hay que devolver todo lo cobrado
	// de mas: areas_total - ciclos_del_tramo_0 (= sbSpanCycles[0], prefijo
	// acumulado del area al terminar el tramo 0). sbSpanCycles es tabla de
	// PREFIJOS (el acumulado justo al cruzar cada union; la ultima entrada,
	// escrita tras el paso 1, es el total del area), indexada 0..sbSpanCount.
	// La parte 2 de la correccion se emite en la frontera de cada union, dentro
	// del bucle. x8/w9: el mismo scratch que usa mVUtestCycles, fuera del pool
	// del allocator.
	if (sbFrame >= 0 && sbSpanCount > 0)
	{
		armMoveAddressToReg(a64::x8, &mVU.cycles);
		armAsm->Ldr(a64::w9, a64::MemOperand(a64::x8));
		armAsm->Add(a64::w9, a64::w9, mVUcycles - sbSpanCycles[0]);
		armAsm->Str(a64::w9, a64::MemOperand(a64::x8));
	}

	// Correccion de presupuesto (parte 2 de 2): al cruzar cada union, el
	// contador baja lo que la cadena habria deducido al entrar al bloque del
	// tramo siguiente (sbSpanCycles[j+1] - sbSpanCycles[j] = ciclos de ese
	// tramo). Se emite al ARRANQUE de la primera instruccion del tramo nuevo
	// — el punto exacto donde la cadena pondria la cabecera de su bloque,
	// salvo que aqui NO hay guarda: SbResolve ya filtro por presupuesto, y la
	// guarda de entrada del superbloque comparo el area entera. Con esto el
	// valor durante el tramo j es B - sbSpanCycles[j] para toda la region,
	// incluida la ultima (donde sbSpanCycles[sbSpanCount] == mVUcycles, o sea
	// sin correccion: la salida terminal ve exactamente lo mismo que veria la
	// cadena). x8 se re-materializa en cada frontera: el emisor de ops lo
	// pisa igual que el resto del scratch.
	u32 sbNextJ = 0;
	for (; x < endCount; x++)
	{
		// Superblock (variante): el area NO es contigua en PC, y el bucle
		// normal avanza iPC linealmente (incPC dentro de
		// mVUexecuteInstruction/doUpperOp/doLowerOp). Cada iteracion variante
		// se re-ancla al par que le toca segun la tabla de region del paso 1
		// (sbOpSlot[x] = indice de par; *2 = palabra inferior, que es donde
		// empieza una iteracion del bucle normal). Las salidas por isEOB y el
		// corte terminal conservan su semantica intacta: son posiciones de la
		// tabla como cualquier otra.
		if (sbFrame >= 0)
		{
			if (x >= mVU.sbOpCount)
				break; // region terminada (no debe darse: el corte terminal marca isEOB)
			iPC = mVU.sbOpSlot[x] * 2;
			setCode();
			while (sbNextJ < sbSpanCount && x == mVU.sbJDelayOp[sbNextJ] + 1)
			{
				armMoveAddressToReg(a64::x8, &mVU.cycles);
				armAsm->Ldr(a64::w9, a64::MemOperand(a64::x8));
				armAsm->Sub(a64::w9, a64::w9, sbSpanCycles[sbNextJ + 1] - sbSpanCycles[sbNextJ]);
				armAsm->Str(a64::w9, a64::MemOperand(a64::x8));
				sbNextJ++;
			}
		}
		if (mVUinfo.isEOB) { x = 0xffff; }

		// M-bit: signal the EE-visible M-flag so VU0 micro-mode can break/sync
		// to the EE (VU0.cpp gates the M-bit Break on VURegs.flags & MFLAGSET).
		// Mirrors x86 microVU_Compile.inl:890-893; VURegs.flags is always
		// memory-resident, so no regalloc flush is needed (same as x86's
		// direct memory xOR). Matches the VUFLAG_INTCINTERRUPT D/T-bit pattern.
		if (mVUup.mBit)
		{
			armAsm->Ldr(a64::w9, mVUstateMem(offsetof(VURegs, flags)));
			armAsm->Orr(a64::w9, a64::w9, VUFLAG_MFLAGSET);
			armAsm->Str(a64::w9, mVUstateMem(offsetof(VURegs, flags)));
		}

		if (isVU1 && mVUlow.kickcycles && CHECK_XGKICKHACK)
			mVU_XGKICK_SYNC(mVU, false);

		mVUexecuteInstruction(mVU);

#ifdef PCSX2_RECOMPILER_TESTS
		// Per-op state-snapshot hook (test builds only). When enabled, flush all
		// dirty allocator state to vuRegs[N] memory and emit a brk whose imm16
		// encodes the op index; a test harness's SIGTRAP handler captures
		// vuRegs[N] for that index and skips the brk. Release builds emit no
		// per-op probe into the block.
		if (mvu_divtrace::g_enabled.load(std::memory_order_relaxed))
		{
			mvu_divtrace::OpMeta meta{};
			meta.op_idx     = static_cast<u16>(mvu_divtrace::g_meta.size());
			meta.microvu_pc = xPC;
			// Raw 64-bit microvu instruction (lower word + upper word) at xPC.
			std::memcpy(&meta.opcode, &mVU.regs().Micro[xPC], sizeof(u32));
			meta.host_lo = armGetCurrentCodePointer();
			meta.alloc   = mVU.regAlloc->snapshotMaps();

			mVU.regAlloc->flushAll(true);

			// Flush qmmPQ (host-resident Q/P pipeline) to vuRegs.VI[REG_Q]/[REG_P]
			// + pending_q/pending_p so vi22/vi23 are meaningful at the brk.
			// The current lane is not known at codegen-of-op-N (mVU.q is
			// the post-analyze final value, not the per-op value), so dump both:
			// VI[REG_Q] := qmmPQ[0], pending_q := qmmPQ[1]. The driver compares
			// JIT and interp Q as a multiset {VI[Q], pending_q} to tolerate the
			// JIT/interp lane-index disagreement.
			armAsm->Add(a64::x8, gprVUState, offsetof(VURegs, VI) + REG_Q * sizeof(REG_VI));
			armAsm->St1(qmmPQ.V4S(), 0, a64::MemOperand(a64::x8));
			armAsm->Add(a64::x8, gprVUState, offsetof(VURegs, pending_q));
			armAsm->St1(qmmPQ.V4S(), 1, a64::MemOperand(a64::x8));
			if (isVU1)
			{
				armAsm->Add(a64::x8, gprVUState, offsetof(VURegs, VI) + REG_P * sizeof(REG_VI));
				armAsm->St1(qmmPQ.V4S(), 2, a64::MemOperand(a64::x8));
				armAsm->Add(a64::x8, gprVUState, offsetof(VURegs, pending_p));
				armAsm->St1(qmmPQ.V4S(), 3, a64::MemOperand(a64::x8));
			}

			armAsm->Brk(meta.op_idx);
			meta.host_hi = armGetCurrentCodePointer();
			mvu_divtrace::g_meta.push_back(meta);
		}
#endif

		// T/D/M-bit per-instruction handling (excluding branch delay slots;
		// those are handled after the branch itself emits). Mirrors x86
		// microVU_Compile.inl:901-923.
		if (!mVUinfo.isBdelay && !mVUlow.branch)
		{
			if (mVUup.tBit)
			{
				mVUDoTBit(mVU, &mFC);
			}
			else if (mVUup.dBit && doDBitHandling)
			{
				mVUDoDBit(mVU, &mFC);
			}
			else if (mVUup.mBit && !mVUup.eBit && !mVUinfo.isEOB)
			{
				// Flags must be exact: Gungrave does FCAND/FMAND with M-bit
				// back-to-back. setupBranch sorts flag instances.
				mVUsetupBranch(mVU, mFC);
				// Emit a runtime snapshot of the current pipeline state into
				// mVU.prog.lpState. Matches x86's xMOV(ptr32[lpS], cpS[0])
				// loop: the values are compile-time constants (from mVUregs)
				// baked into the emitted store instructions.
				{
					const u32* cpS = reinterpret_cast<const u32*>(&mVUregs);
					const size_t nWords = (sizeof(microRegInfo) - 4) / 4;
					armMoveAddressToReg(a64::x8, &mVU.prog.lpState);
					for (size_t i = 0; i < nWords; i++)
					{
						armAsm->Mov(a64::w9, cpS[i]);
						armAsm->Str(a64::w9, a64::MemOperand(a64::x8, static_cast<s32>(i * 4)));
					}
				}
				incPC(2);
				mVUsetupRange(mVU, xPC, false);
				// VUSyncHack: clear nextBlockCycles (mVUendProgram only emits
				// this for the E-bit/isEbit!=0 paths, so the M-bit case must
				// store it explicitly). Mirrors x86 microVU_Compile.inl:926-927.
				if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack)
					armAsm->Str(a64::wzr, mVUstateMem(offsetof(VURegs, nextBlockCycles)));
				// endProgram + normBranchCompile run at iPC+2 so the saved TPC is
				// the continuation PC (not the already-executed M-bit instruction)
				// and the continuation block is compiled/linked into the cache.
				// incPC(-2) is deferred until after, matching x86:928-930.
				mVUendProgram(mVU, &mFC, 0);
				normBranchCompile(mVU, xPC);
				incPC(-2);
				goto perf_and_return;
			}
		}

		if (mVUinfo.doXGKICK)
			mVU_XGKICK_DELAY(mVU);

		if (isEvilBlock)
		{
			mVUsetupRange(mVU, xPC + 8, false);
			normJumpCompile(mVU, mFC, true);
			goto perf_and_return;
		}
		else if (!mVUinfo.isBdelay)
		{
			if ((xPC + 8) == mVU.microMemSize)
			{
				mVUsetupRange(mVU, xPC + 8, false);
				mVUsetupRange(mVU, 0, 1);
			}
			incPC(1);
		}
		else
		{
			incPC(1);
			mVUsetupRange(mVU, xPC, false);
			incPC(-4); // Go back to branch opcode

			switch (mVUlow.branch)
			{
				case 1: case 2: // B/BAL
					normBranch(mVU, mFC);
					goto perf_and_return;
				case 9: case 10: // JR/JALR
					normJump(mVU, mFC);
					goto perf_and_return;
				case 3: // IBEQ
					condBranch(mVU, mFC, a64::eq);
					goto perf_and_return;
				case 4: // IBGEZ
					condBranch(mVU, mFC, a64::ge);
					goto perf_and_return;
				case 5: // IBGTZ
					condBranch(mVU, mFC, a64::gt);
					goto perf_and_return;
				case 6: // IBLEQ
					condBranch(mVU, mFC, a64::le);
					goto perf_and_return;
				case 7: // IBLTZ
					condBranch(mVU, mFC, a64::lt);
					goto perf_and_return;
				case 8: // IBNE
					condBranch(mVU, mFC, a64::ne);
					goto perf_and_return;
			}
		}
	}

	// E-bit end
	mVUsetupRange(mVU, xPC, false);
	mVUendProgram(mVU, &mFC, 1);

perf_and_return:
	// === Cenit VU Superblock: cierre de la compilacion variante ===
	// Unica salida del paso 2 (los terminales M-bit/evil/branch/E-bit caen
	// todos aqui). Se ENROLLA el frame (sbSlot/sbActive al valor del marco
	// exterior — con esto las compilaciones recursivas del corte terminal ya
	// no veian el frame, pero el marco de arriba, si lo hubiera, lo
	// recupera) y se entrega el parte al motor: junctions, kick (areaBad),
	// ops y ciclos del area. Con junctions == 0 no hay superbloque (solo una
	// compilacion duplicada que se tira); SbCloseCompile libera el slot en
	// ese caso. El hostEntry registrado en el gestor (mVUblock, copia propia
	// del pState con el bit de rasguino horneado) es la entrada que el motor
	// guardara para SbResolve.
	if (sbFrame >= 0)
	{
		mVUSuperblock::SbCloseCompile(sbFrame, startPC, thisPtr, mVU.sbJunctions,
			mVU.sbKick != 0, mVUcount, mVUcycles);
		mVU.sbSlot   = sbFrame;
		mVU.sbActive = sbOuterActive;
	}
	// Register the program-entry compile only, not every continuation block
	// compiled at a mid-program PC (start_pc stays the program entry for
	// branch-target sub-blocks; it's only re-pointed on the dispatch/indirect-
	// jump path). Mirrors x86 microVU_Compile.inl (a5ed24ca8) — one clean
	// jitdump symbol per VU program rather than a symbol per linked sub-block.
	if (mVU.regs().start_pc == startPC)
	{
		u8* endPtr = armGetCurrentCodePointer();
		if (mVU.index)
			Perf::vu1.RegisterPC(thisPtr, static_cast<u32>(endPtr - thisPtr), startPC);
		else
			Perf::vu0.RegisterPC(thisPtr, static_cast<u32>(endPtr - thisPtr), startPC);
	}
	return thisPtr;
}
