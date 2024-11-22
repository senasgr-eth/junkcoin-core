// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"
#include "auxpow.h"
#include "arith_uint256.h"
#include "chain.h"
#include "junkcoin.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"

// Validate block timestamps to prevent time manipulation
bool ValidateBlockTime(const CBlockIndex* pindexLast, const CBlockHeader *pblock)
{
    if (!pblock) return true;

    // Check if block time is too old compared to previous block's median time
    if (pblock->GetBlockTime() <= pindexLast->GetMedianTimePast())
        return false;

    // Check if block is too far in the future (max 2 hours)
    if (pblock->GetBlockTime() > GetAdjustedTime() + 2 * 60 * 60)
        return false;

    return true;
}

// Determine if the for the given block, a min difficulty setting applies
bool AllowMinDifficultyForBlock(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    // check if the chain allows minimum difficulty blocks
    if (!params.fPowAllowMinDifficultyBlocks)
        return false;

    // junkcoin: Magic number at which reset protocol switches
    // check if we allow minimum difficulty at this block-height
    if ((unsigned)pindexLast->nHeight < params.nHeightEffective) {
        return false;
    }

    // Only allow min difficulty if block time is significantly delayed
    // Using 6x target spacing   instead of just 2x
    return (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 6);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Genesis block
    if (pindexLast == NULL)
        return nProofOfWorkLimit;

    // Validate block timestamps
    if (!ValidateBlockTime(pindexLast, pblock)) {
        return nProofOfWorkLimit; // Force high difficulty for invalid timestamps
    }

    // junkcoin: Special rules for minimum difficulty blocks with Digishield
    if (AllowDigishieldMinDifficultyForBlock(pindexLast, pblock, params))
    {
        return nProofOfWorkLimit;
    }

    // Only change once per difficulty adjustment interval
    bool fNewDifficultyProtocol = (pindexLast->nHeight+1 >= 69360);
    const int64_t nTargetTimespanCurrent = fNewDifficultyProtocol ? params.nPowTargetTimespan : (params.nPowTargetTimespan*12);
    const int64_t difficultyAdjustmentInterval = nTargetTimespanCurrent / params.nPowTargetSpacing;

    if ((pindexLast->nHeight+1) % difficultyAdjustmentInterval != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 6* target spacing
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 6)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int blockstogoback = difficultyAdjustmentInterval-1;
    if ((pindexLast->nHeight+1) != difficultyAdjustmentInterval)
        blockstogoback = difficultyAdjustmentInterval;

    int nHeightFirst = pindexLast->nHeight - blockstogoback;
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateDogecoinNextWorkRequired(fNewDifficultyProtocol, nTargetTimespanCurrent, pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;

    // Target timespan is 4 hours, limit adjustment to 1/4th and 4x
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);

    // Use actual timespan to adjust difficulty
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    // Make sure we do not exceed the proof of work limit
    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit)) {
        return false;
    }

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget) {
        LogPrintf("WARN: Check proof of work matches claimed amount %s | %s > %s\n", hash.ToString().c_str(), UintToArith256(hash).ToString().c_str(), bnTarget.ToString().c_str());
        return false;
    }

    return true;
}
