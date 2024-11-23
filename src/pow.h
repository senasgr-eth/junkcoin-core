// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include "consensus/params.h"

#include <stdint.h>

class CBlockHeader;
class CBlockIndex;
class uint256;

// Height at which new difficulty adjustment rules activate
static const int NEW_RULES_ACTIVATION_HEIGHT = 175000;
static const int TRANSITION_WINDOW = 2000;

/** Validate block timestamps to prevent time manipulation */
bool ValidateBlockTime(const CBlockIndex* pindexLast, const CBlockHeader *pblock);

/** Calculate transition factor for new rules */
double GetTransitionFactor(int height);

/** Get difficulty adjustment limits based on height and transition factor */
void GetDifficultyLimits(int height, double transitionFactor, 
                        int64_t& nMinTimespan, int64_t& nMaxTimespan,
                        const Consensus::Params& params);

/** Check if minimum difficulty is allowed for a block */
bool AllowMinDifficultyForBlock(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params);

/** Validate difficulty transitions between blocks */
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits);

/** Get the next required proof of work for a block */
unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);

/** Calculate the next required proof of work using the difficulty adjustment algorithm */
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);

#endif // BITCOIN_POW_H
