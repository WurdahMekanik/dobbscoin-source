// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <math.h>
#include "pow.h"

#include "bignum.h"
#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"


/*This is a major hackjob, but bignum is required  because the math isn't quite handled the same
  for uint256 with Kimoto Gravity Well calculations*/


unsigned int static KimotoGravityWell(const CBlockIndex* pindexLast, const CBlockHeader *pblock, uint64_t TargetBlocksSpacingSeconds, uint64_t PastBlocksMin, uint64_t PastBlocksMax) {
    /* current difficulty formula, megacoin - kimoto gravity well */
    const CBlockIndex  *BlockLastSolved             = pindexLast;
    const CBlockIndex  *BlockReading                = pindexLast;
    const CBlockHeader *BlockCreating               = pblock;
                        BlockCreating               = BlockCreating;
    uint64_t            PastBlocksMass              = 0;
    int64_t             PastRateActualSeconds       = 0;
    int64_t             PastRateTargetSeconds       = 0;
    int64_t             LatestBlockTime             = BlockLastSolved->GetBlockTime();
    double              PastRateAdjustmentRatio     = double(1);
    CBigNum             PastDifficultyAverage;
    CBigNum             PastDifficultyAveragePrev;
    double              EventHorizonDeviation;
    double              EventHorizonDeviationFast;
    double              EventHorizonDeviationSlow;
    CBigNum bnProofOfWorkLimit(Params().ProofOfWorkLimit());
    
    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || (uint64_t)BlockLastSolved->nHeight < PastBlocksMin) { return bnProofOfWorkLimit.GetCompact(); }
    
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
        PastBlocksMass++;
        
        if (i == 1) { PastDifficultyAverage.SetCompact(BlockReading->nBits); }
        else        { PastDifficultyAverage = ((CBigNum().SetCompact(BlockReading->nBits) - PastDifficultyAveragePrev) / i) + PastDifficultyAveragePrev; }
        PastDifficultyAveragePrev = PastDifficultyAverage;
        
        if (LatestBlockTime < BlockReading->GetBlockTime()) { LatestBlockTime = BlockReading->GetBlockTime(); }
        PastRateActualSeconds           = LatestBlockTime - BlockReading->GetBlockTime();
        PastRateTargetSeconds           = TargetBlocksSpacingSeconds * PastBlocksMass;
        PastRateAdjustmentRatio         = double(1);
        if (PastRateActualSeconds < 1) { PastRateActualSeconds = 1; }
        if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
        PastRateAdjustmentRatio         = double(PastRateTargetSeconds) / double(PastRateActualSeconds);
        }
        EventHorizonDeviation           = 1 + (0.7084 * pow((double(PastBlocksMass)/double(9)), -1.228));
        EventHorizonDeviationFast       = EventHorizonDeviation;
        EventHorizonDeviationSlow       = 1 / EventHorizonDeviation;
        
        if (PastBlocksMass >= PastBlocksMin) {
            if ((PastRateAdjustmentRatio <= EventHorizonDeviationSlow) || (PastRateAdjustmentRatio >= EventHorizonDeviationFast)) { assert(BlockReading); break; }
        }
        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }
    
    CBigNum bnNew(PastDifficultyAverage);
    if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
        bnNew *= (uint64_t)PastRateActualSeconds;
        bnNew /= (uint64_t)PastRateTargetSeconds;
    }
    if (bnNew > bnProofOfWorkLimit) { bnNew = bnProofOfWorkLimit; }
    
    /// debug print
    LogPrintf("Difficulty Retarget - BOB's Wormh0le (KGW)\n");
    LogPrintf("PastRateAdjustmentRatio = %g\n", PastRateAdjustmentRatio);
    LogPrintf("Before: %08x  %s\n", BlockLastSolved->nBits, CBigNum().SetCompact(BlockLastSolved->nBits).getuint256().ToString().c_str());
    LogPrintf("After:  %08x  %s\n", bnNew.GetCompact(), bnNew.getuint256().ToString().c_str());
    
    return bnNew.GetCompact();
}

unsigned int static GetNextWorkRequired_V2(const CBlockIndex* pindexLast, const CBlockHeader *pblock)
{
    static const int64_t    BlocksTargetSpacing     = 10 * 60; // 10 minutes
    unsigned int            TimeDaySeconds          = 60 * 60 * 24;
    int64_t                 PastSecondsMin          = TimeDaySeconds * 0.0625;
    int64_t                 PastSecondsMax          = TimeDaySeconds * 1.75;
    uint64_t                PastBlocksMin           = PastSecondsMin / BlocksTargetSpacing;
    uint64_t                PastBlocksMax           = PastSecondsMax / BlocksTargetSpacing; 
    
    return KimotoGravityWell(pindexLast, pblock, BlocksTargetSpacing, PastBlocksMin, PastBlocksMax);
}

unsigned int GetNextWorkRequired_V1(const CBlockIndex* pindexLast, const CBlockHeader *pblock)
{
    unsigned int nProofOfWorkLimit = Params().ProofOfWorkLimit().GetCompact();

    // Genesis block
    if (pindexLast == NULL)
        return nProofOfWorkLimit;

    // Only change once per interval
    if ((pindexLast->nHeight+1) % Params().Interval() != 0)
    {
        if (Params().AllowMinDifficultyBlocks())
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + Params().TargetSpacing()*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % Params().Interval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    const CBlockIndex* pindexFirst = pindexLast;
    for (int i = 0; pindexFirst && i < Params().Interval()-1; i++)
        pindexFirst = pindexFirst->pprev;
    assert(pindexFirst);

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindexFirst->GetBlockTime();
    LogPrintf("  nActualTimespan = %d  before bounds\n", nActualTimespan);
    if (nActualTimespan < Params().TargetTimespan()/4)
        nActualTimespan = Params().TargetTimespan()/4;
    if (nActualTimespan > Params().TargetTimespan()*4)
        nActualTimespan = Params().TargetTimespan()*4;

    // Retarget
    uint256 bnNew;
    uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    bnNew *= nActualTimespan;
    bnNew /= Params().TargetTimespan();

    if (bnNew > Params().ProofOfWorkLimit())
        bnNew = Params().ProofOfWorkLimit();

    /// debug print
    LogPrintf("GetNextWorkRequired RETARGET\n");
    LogPrintf("Params().TargetTimespan() = %d    nActualTimespan = %d\n", Params().TargetTimespan(), nActualTimespan);
    LogPrintf("Before: %08x  %s\n", pindexLast->nBits, bnOld.ToString());
    LogPrintf("After:  %08x  %s\n", bnNew.GetCompact(), bnNew.ToString());

    return bnNew.GetCompact();
}

//digishield
unsigned int GetNextWorkRequired_V3(const CBlockIndex* pindexLast, const CBlockHeader *pblock)
{

    static const int64_t nTargetTimespan_V3 = 10*60 ; // dobbscoin: every 10 minutes
    static const int64_t nTargetSpacing_V3 = 10*60; // dobbscoin: 10 minutes
    static const int64_t nInterval_V3 = nTargetTimespan_V3 / nTargetSpacing_V3;

    unsigned int nProofOfWorkLimit = Params().ProofOfWorkLimit().GetCompact();

    
    int64_t retargetTimespan = nTargetTimespan_V3; //Params().TargetTimespan();
    int64_t retargetInterval = nInterval_V3; //Params().Interval();
    
    //retargetInterval = Params().TargetTimespan() / Params().TargetSpacing();
    //retargetTimespan = Params().TargetTimespan();
    
    // Genesis block
    if (pindexLast == NULL)
        return nProofOfWorkLimit;

    // Only change once per interval
    if ((pindexLast->nHeight+1) % retargetInterval != 0)
    {
        if (Params().AllowMinDifficultyBlocks())
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* nTargetSpacing minutes
            // then allow mining of a min-difficulty block.
            if (pblock->nTime > pindexLast->nTime + nTargetSpacing_V3 * 30) //Params().TargetSpacing()*30)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % retargetInterval != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Fractalcoin: This fixes an issue where a 51% attack can change difficulty at will.
    // Go back the full period unless it's the first retarget after genesis. Code courtesy of Art Forz
    int blockstogoback = retargetInterval-1;
    if ((pindexLast->nHeight+1) != retargetInterval)
        blockstogoback = retargetInterval;

    // Go back by what we want to be 14 days worth of blocks
    const CBlockIndex* pindexFirst = pindexLast;
    for (int i = 0; pindexFirst && i < blockstogoback; i++)
        pindexFirst = pindexFirst->pprev;
    assert(pindexFirst);

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindexFirst->GetBlockTime();
    LogPrintf("  nActualTimespan = %d  before bounds\n", nActualTimespan);
    
    //DigiShield implementation - thanks to RealSolid & WDC for this code
    // amplitude filter - thanks to daft27 for this code
    nActualTimespan = retargetTimespan + (nActualTimespan - retargetTimespan)/8;

    if (nActualTimespan < (retargetTimespan - (retargetTimespan/4)) ) nActualTimespan = (retargetTimespan - (retargetTimespan/4));
    if (nActualTimespan > (retargetTimespan + (retargetTimespan/2)) ) nActualTimespan = (retargetTimespan + (retargetTimespan/2));

    // Retarget
    uint256 bnNew;
    uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    //scale up for millisecond granularity
    bnNew *= nActualTimespan;
    //slingshield effectively works by making the target block time longer temporarily
    bnNew /= (retargetTimespan); 



    if (bnNew > Params().ProofOfWorkLimit())
        bnNew = Params().ProofOfWorkLimit();

    /// debug print
    LogPrintf("GetNextWorkRequired DIGISHIELD RETARGET\n");
    LogPrintf("Params().TargetTimespan() = %d    nActualTimespan = %d\n", Params().TargetTimespan(), nActualTimespan);
    LogPrintf("Before: %08x  %s\n", pindexLast->nBits, bnOld.ToString());
    LogPrintf("After:  %08x  %s\n", bnNew.GetCompact(), bnNew.ToString());
    return bnNew.GetCompact();
}

//normal digishield, but with 1 minute blocks
unsigned int GetNextWorkRequired_V4(const CBlockIndex* pindexLast, const CBlockHeader *pblock)
{

    static const int64_t nTargetTimespan_V4 = 2*60 ; // dobbscoin: every 2 minute
    static const int64_t nTargetSpacing_V4 = 2*60; // dobbscoin: 2 minute
    static const int64_t nInterval_V4 = nTargetTimespan_V4 / nTargetSpacing_V4;

    unsigned int nProofOfWorkLimit = Params().ProofOfWorkLimit().GetCompact();

    
    int64_t retargetTimespan = nTargetTimespan_V4; //Params().TargetTimespan();
    int64_t retargetInterval = nInterval_V4; //Params().Interval();
    
    //retargetInterval = Params().TargetTimespan() / Params().TargetSpacing();
    //retargetTimespan = Params().TargetTimespan();
    
    // Genesis block
    if (pindexLast == NULL)
        return nProofOfWorkLimit;

    // Only change once per interval
    if ((pindexLast->nHeight+1) % retargetInterval != 0)
    {
        if (Params().AllowMinDifficultyBlocks())
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* nTargetSpacing minutes
            // then allow mining of a min-difficulty block.
            if (pblock->nTime > pindexLast->nTime + nTargetSpacing_V4 * 30) //Params().TargetSpacing()*30)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % retargetInterval != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // This fixes an issue where a 51% attack can change difficulty at will.
    // Go back the full period unless it's the first retarget after genesis. Code courtesy of Art Forz
    int blockstogoback = retargetInterval-1;
    if ((pindexLast->nHeight+1) != retargetInterval)
        blockstogoback = retargetInterval;

    // Go back by what we want to be 14 days worth of blocks
    const CBlockIndex* pindexFirst = pindexLast;
    for (int i = 0; pindexFirst && i < blockstogoback; i++)
        pindexFirst = pindexFirst->pprev;
    assert(pindexFirst);

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindexFirst->GetBlockTime();
    LogPrintf("  nActualTimespan = %d  before bounds\n", nActualTimespan);
    
    //DigiShield implementation - thanks to RealSolid & WDC for this code
    // amplitude filter - thanks to daft27 for this code
    nActualTimespan = retargetTimespan + (nActualTimespan - retargetTimespan)/8;

    if (nActualTimespan < (retargetTimespan - (retargetTimespan/4)) ) nActualTimespan = (retargetTimespan - (retargetTimespan/4));
    if (nActualTimespan > (retargetTimespan + (retargetTimespan/2)) ) nActualTimespan = (retargetTimespan + (retargetTimespan/2));

    // Retarget
    uint256 bnNew;
    uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    //scale up for millisecond granularity
    bnNew *= nActualTimespan;
    bnNew /= (retargetTimespan); 



    if (bnNew > Params().ProofOfWorkLimit())
        bnNew = Params().ProofOfWorkLimit();

    /// debug print
    LogPrintf("GetNextWorkRequired DIGISHIELD RETARGET 2 MINUTES\n");
    LogPrintf("Params().TargetTimespan() = %d    nActualTimespan = %d\n", Params().TargetTimespan(), nActualTimespan);
    LogPrintf("Before: %08x  %s\n", pindexLast->nBits, bnOld.ToString());
    LogPrintf("After:  %08x  %s\n", bnNew.GetCompact(), bnNew.ToString());
    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock)
{
    int DiffMode = 1;
    if (Params().AllowMinDifficultyBlocks()) {
        if (pindexLast->nHeight+1 >= 20) { DiffMode = 2; }
        if (pindexLast->nHeight+1 >= 50) { DiffMode = 3; }
        if(pindexLast->nHeight+1 >= 200) { DiffMode = 4; }
    }
    else 
    {
        if (pindexLast->nHeight+1 >= 13579) { DiffMode = 2; } // KGW @ Block 13579
        if(pindexLast->nHeight+1 >= 31597) { DiffMode = 3; } //switch to dobbshield @ block 31597
        if(pindexLast->nHeight+1 >= 68425) {DiffMode = 4; } //switch to 2 minute blocks with dobbshield
    }
    if      (DiffMode == 1) { return GetNextWorkRequired_V1(pindexLast, pblock); }
    else if (DiffMode == 2) { return GetNextWorkRequired_V2(pindexLast, pblock); }
    else if (DiffMode == 3) { return GetNextWorkRequired_V3(pindexLast, pblock); }
    else if (DiffMode == 4) { return GetNextWorkRequired_V4(pindexLast, pblock); }
    return GetNextWorkRequired_V3(pindexLast, pblock); //never reached..
}


bool CheckProofOfWork(uint256 hash, unsigned int nBits)
{
    bool fNegative;
    bool fOverflow;
    uint256 bnTarget;

    if(hash==HASHGENESISBLOCKPOW) //ignore genesis block
    {
        return true;
    }

    if (Params().SkipProofOfWorkCheck())
       return true;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > Params().ProofOfWorkLimit())
        return error("CheckProofOfWork() : nBits below minimum work");

    // Check proof of work matches claimed amount
    if (hash > bnTarget)
        return error("CheckProofOfWork() : hash doesn't match nBits");

    return true;
}

uint256 GetBlockProof(const CBlockIndex& block)
{
    uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}
