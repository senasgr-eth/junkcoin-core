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
#include "timedata.h"

// Height at which new difficulty adjustment rules activate
static const int NEW_RULES_ACTIVATION_HEIGHT = 175000; // About 2.5 days from current height
static const int TRANSITION_WINDOW = 2000; // Gradual transition over 2000 blocks (~1.4 days)

// Calculate transition factor (0.0 to 1.0) based on block height
double GetTransitionFactor(int height) {
    if (height < NEW_RULES_ACTIVATION_HEIGHT)
        return 0.0;
    if (height >= NEW_RULES_ACTIVATION_HEIGHT + TRANSITION_WINDOW)
        return 1.0;
    
    return (double)(height - NEW_RULES_ACTIVATION_HEIGHT) / TRANSITION_WINDOW;
}

// Get difficulty adjustment limits based on height and transition factor
void GetDifficultyLimits(int height, double transitionFactor, 
                        int64_t& nMinTimespan, int64_t& nMaxTimespan,
                        const Consensus::Params& params) {
    
    // Default/old limits
    nMinTimespan = params.nPowTargetTimespan/4;
    nMaxTimespan = params.nPowTargetTimespan*4;

    if (height < NEW_RULES_ACTIVATION_HEIGHT)
        return;

    // Calculate target limits based on height
    int64_t targetMin, targetMax;
    if (height > NEW_RULES_ACTIVATION_HEIGHT + 10000) {
        targetMin = params.nPowTargetTimespan/4;
        targetMax = params.nPowTargetTimespan*4;
    } else if (height > NEW_RULES_ACTIVATION_HEIGHT + 5000) {
        targetMin = params.nPowTargetTimespan/8;
        targetMax = params.nPowTargetTimespan*4;
    } else {
        targetMin = params.nPowTargetTimespan/16;
        targetMax = params.nPowTargetTimespan*4;
    }

    // Interpolate between old and new limits
    nMinTimespan = nMinTimespan + (targetMin - nMinTimespan) * transitionFactor;
    nMaxTimespan = nMaxTimespan + (targetMax - nMaxTimespan) * transitionFactor;
}

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
    // Using 6x target spacing instead of just 2x
    return (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 6);
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks)
        return true;

    if (height % params.DifficultyAdjustmentInterval() == 0) {
        double transitionFactor = GetTransitionFactor(height);
        int64_t nMinTimespan, nMaxTimespan;
        GetDifficultyLimits(height, transitionFactor, nMinTimespan, nMaxTimespan, params);

        const arith_uint256 pow_limit = UintToArith256(params.powLimit);
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        // Calculate the largest difficulty value possible
        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        largest_difficulty_target *= nMaxTimespan;
        largest_difficulty_target /= params.nPowTargetTimespan;

        if (largest_difficulty_target > pow_limit) {
            largest_difficulty_target = pow_limit;
        }

        // Round and compare to observed value
        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target)
            return false;

        // Calculate the smallest difficulty value possible
        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        smallest_difficulty_target *= nMinTimespan;
        smallest_difficulty_target /= params.nPowTargetTimespan;

        if (smallest_difficulty_target > pow_limit) {
            smallest_difficulty_target = pow_limit;
        }

        // Round and compare to observed value
        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target)
            return false;

    } else if (old_nbits != new_nbits) {
        return false;
    }
    return true;
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

    unsigned int nBits = CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
    
    // Validate difficulty transition
    if (!PermittedDifficultyTransition(params, pindexLast->nHeight + 1, pindexLast->nBits, nBits)) {
        return pindexLast->nBits; // Keep previous difficulty if transition is not permitted
    }

    return nBits;
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Calculate transition factor for new rules
    double transitionFactor = GetTransitionFactor(pindexLast->nHeight + 1);

    // Get difficulty limits based on height and transition factor
    int64_t nMinTimespan, nMaxTimespan;
    GetDifficultyLimits(pindexLast->nHeight + 1, transitionFactor, nMinTimespan, nMaxTimespan, params);

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    int64_t nModulatedTimespan = nActualTimespan;

    // Apply limits
    if (nModulatedTimespan < nMinTimespan)
        nModulatedTimespan = nMinTimespan;
    else if (nModulatedTimespan > nMaxTimespan)
        nModulatedTimespan = nMaxTimespan;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);

    // Use modulated timespan to adjust difficulty
    bnNew *= nModulatedTimespan;
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
