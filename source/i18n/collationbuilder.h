/*
*******************************************************************************
* Copyright (C) 2013, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationbuilder.h
*
* created on: 2013may06
* created by: Markus W. Scherer
*/

#ifndef __COLLATIONBUILDER_H__
#define __COLLATIONBUILDER_H__

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/unistr.h"
#include "collationrootelements.h"
#include "collationruleparser.h"
#include "collationtailoringdatabuilder.h"
#include "uvectr32.h"
#include "uvectr64.h"

struct UParseError;

U_NAMESPACE_BEGIN

struct CollationData;
struct CollationTailoring;

class Normalizer2;

class U_I18N_API CollationBuilder : public CollationRuleParser::Sink {
public:
    CollationBuilder(const CollationData *base, UErrorCode &errorCode);
    virtual ~CollationBuilder();

    void parseAndBuild(const UnicodeString &ruleString,
                       CollationRuleParser::Importer *importer,
                       CollationTailoring &tailoring,
                       UParseError *outParseError,
                       UErrorCode &errorCode);

    const char *getErrorReason() const { return errorReason; }

    UBool modifiesSettings() const { return TRUE; }  // TODO
    UBool modifiesMappings() const { return TRUE; }  // TODO

private:
    /** Implements CollationRuleParser::Sink. */
    virtual void addReset(int32_t strength, const UnicodeString &str,
                          const char *&errorReason, UErrorCode &errorCode);

    int64_t getSpecialResetPosition(const UnicodeString &str,
                                    const char *&parserErrorReason, UErrorCode &errorCode);

    /** Implements CollationRuleParser::Sink. */
    virtual void addRelation(int32_t strength, const UnicodeString &prefix,
                             const UnicodeString &str, const UnicodeString &extension,
                             const char *&errorReason, UErrorCode &errorCode);

    /**
     * Picks one of the current CEs and finds or inserts a node in the graph
     * for the CE + strength.
     */
    int32_t findOrInsertNodeForCEs(int32_t strength, const char *&parserErrorReason,
                                   UErrorCode &errorCode);
    int32_t findOrInsertNodeForRootCE(int64_t ce, int32_t strength, UErrorCode &errorCode);

    /**
     * Makes and inserts a new tailored node into the list, after the one at index.
     * Skips over nodes of weaker strength to maintain collation order
     * ("postpone insertion").
     * @return the new node's index
     */
    int32_t insertTailoredNodeAfter(int32_t index, int32_t strength, UErrorCode &errorCode);

    /**
     * Inserts a new node into the list, between list-adjacent items.
     * The node's previous and next indexes must not be set yet.
     * @return the new node's index
     */
    int32_t insertNodeBetween(int32_t index, int32_t nextIndex, int64_t node,
                              UErrorCode &errorCode);

    /**
     * Finds the node which implies or contains a common=05 weight of the given strength
     * (secondary or tertiary).
     * Skips weaker nodes and tailored nodes if the current node is stronger
     * and is followed by an explicit-common-weight node.
     * Always returns the input index if that node is no stronger than the given strength.
     */
    int32_t findCommonNode(int32_t index, int32_t strength) const;

    /** Implements CollationRuleParser::Sink. */
    virtual void suppressContractions(const UnicodeSet &set,
                                      const char *&errorReason, UErrorCode &errorCode);

    /**
     * Encodes "temporary CE" data into a CE that fits into the CE32 data structure,
     * with 2-byte primary, 1-byte secondary and 6-bit tertiary,
     * with valid CE byte values.
     *
     * The impossible case bits combination 11 is used to distinguish
     * temporary CEs from real CEs.
     */
    static inline int64_t tempCEFromIndexAndStrength(int32_t index, int32_t strength) {
        return
            // CE byte offsets, to ensure valid CE bytes, and case bits 11
            0x808000008000e000 |
            // index bits 17..12 -> primary byte 1 = CE bits 63..56 (byte values 80..BF)
            ((int64_t)(index & 0x3f000) << 44) |
            // index bits 11..6 -> primary byte 2 = CE bits 55..48 (byte values 80..BF)
            ((int64_t)(index & 0xfc0) << 42) |
            // index bits 5..0 -> seconary byte 1 = CE bits 31..24 (byte values 80..BF)
            ((index & 0x3f) << 24) |
            // strength bits 1..0 -> tertiary byte 1 = CE bits 13..8 (byte values 20..23)
            (strength << 8);
    }
    static inline int32_t indexFromTempCE(int64_t tempCE) {
        return
            ((int32_t)(tempCE >> 44) & 0x3f000) |
            ((int32_t)(tempCE >> 42) & 0xfc0) |
            ((int32_t)(tempCE >> 24) & 0x3f);
    }
    static inline int32_t strengthFromTempCE(int64_t tempCE) {
        return ((int32_t)tempCE >> 8) & 3;
    }
    static inline UBool isTempCE(int64_t ce) {
        return ((uint32_t)ce & 0xc000) == 0xc000;
    }

    static int32_t ceStrength(int64_t ce);

    /** The secondary/tertiary lower limit for tailoring before the common weight. */
    static const uint32_t BEFORE_WEIGHT16 = Collation::MERGE_SEPARATOR_WEIGHT16;

    /** At most 1M nodes, limited by the 20 bits in node bit fields. */
    static const int32_t MAX_INDEX = 0xfffff;
    /**
     * Node bit 6 is set on a primary node if there are tailored nodes
     * with secondary values below the common secondary weight (05),
     * from a reset-secondary-before (&[before 2]).
     */
    static const int32_t HAS_BEFORE2 = 0x40;
    /**
     * Node bit 5 is set on a primary or secondary node if there are tailored nodes
     * with tertiary values below the common tertiary weight (05),
     * from a reset-tertiary-before (&[before 3]).
     */
    static const int32_t HAS_BEFORE3 = 0x20;
    /**
     * Node bit 3 distinguishes a tailored node, which has no weight value,
     * from a node with an explicit (root or default) weight.
     */
    static const int32_t IS_TAILORED = 8;

    static inline int64_t nodeFromWeight32(uint32_t weight32) {
        return (int64_t)weight32 << 32;
    }
    static inline int64_t nodeFromWeight16(uint32_t weight16) {
        return (int64_t)weight16 << 48;
    }
    static inline int64_t nodeFromPreviousIndex(int32_t previous) {
        return (int64_t)previous << 28;
    }
    static inline int64_t nodeFromNextIndex(int32_t next) {
        return next << 8;
    }
    static inline int64_t nodeFromStrength(int32_t strength) {
        return strength;
    }

    static inline uint32_t weight32FromNode(int64_t node) {
        return (uint32_t)(node >> 32);
    }
    static inline uint32_t weight16FromNode(int64_t node) {
        return (uint32_t)(node >> 48) & 0xffff;
    }
    static inline int32_t previousIndexFromNode(int64_t node) {
        return (int32_t)(node >> 28) & MAX_INDEX;
    }
    static inline int32_t nextIndexFromNode(int64_t node) {
        return ((int32_t)node >> 8) & MAX_INDEX;
    }
    static inline int32_t strengthFromNode(int64_t node) {
        return (int32_t)node & 3;
    }

    static inline int32_t nodeHasBefore2(int64_t node) {
        return (node & HAS_BEFORE2) != 0;
    }
    static inline int32_t nodeHasBefore3(int64_t node) {
        return (node & HAS_BEFORE3) != 0;
    }
    static inline int32_t isTailoredNode(int64_t node) {
        return (node & IS_TAILORED) != 0;
    }

    static inline int64_t changeNodePreviousIndex(int64_t node, int32_t previous) {
        return (node & 0xffff00000fffffff) | nodeFromPreviousIndex(previous);
    }
    static inline int64_t changeNodeNextIndex(int64_t node, int32_t next) {
        return (node & 0xfffffffff00000ff) | nodeFromNextIndex(next);
    }

    const Normalizer2 &nfd;

    const CollationData *baseData;
    const CollationRootElements rootElements;
    uint32_t variableTop;
    int64_t firstImplicitCE;

    CollationTailoringDataBuilder dataBuilder;
    const char *errorReason;

    int64_t ces[Collation::MAX_EXPANSION_LENGTH];
    int32_t cesLength;

    /**
     * Indexes of nodes with root primary weights, sorted by primary.
     * Compact form of a TreeMap from root primary to node index.
     *
     * This is a performance optimization for finding reset positions.
     * Without this, we would have to search through the entire nodes list.
     * It also allows storing root primary weights as list heads,
     * without previous index, leaving room for 32-bit primary weights.
     */
    UVector32 rootPrimaryIndexes;
    /**
     * Data structure for assigning tailored weights and CEs.
     * Doubly-linked lists of nodes in mostly collation order.
     *
     * Each list starts with a root primary node.
     * Each list ends with either a nextIndex of 0, or with a root primary node
     * which contains the primary weight limit for this list.
     *
     * Root primary nodes do not have previous indexes.
     * All other nodes do.
     *
     * Nodes with explicit weights store root collator weights,
     * or default weak weights (e.g., secondary 05) for stronger nodes.
     * "Tailored" nodes, with the IS_TAILORED bit set,
     * create a difference of a certain strength from the preceding node.
     *
     * A root node is followed by either
     * - a root/default node of the same strength, or
     * - a root/default node of the next-weaker strength, or
     * - a tailored node of the same strength.
     *
     * A node of a given strength normally implies "common" weights on weaker levels.
     *
     * A node with HAS_BEFORE2 must be immediately followed by
     * a secondary node with BEFORE_WEIGHT16, then a secondary tailored node,
     * and later an explicit common-secondary node.
     * All secondary tailored nodes between these explicit ones
     * will be assigned lower-than-common secondary weights.
     * If the flag is not set, then there are no explicit secondary node
     * with the common or lower weights.
     *
     * Same for HAS_BEFORE3 for tertiary nodes and weights.
     * A node must not have both flags set.
     *
     * Tailored CEs are initially represented in CollationData as temporary CEs
     * which point to stable indexes in this list,
     * and temporary CEs only point to tailored nodes.
     *
     * At the end, the tailored weights are allocated as necessary,
     * then the tailored nodes are replaced with final CEs,
     * and the CollationData is rewritten by replacing temporary CEs with final ones.
     *
     * We cannot simply insert new nodes in the middle of the array
     * because that would invalidate the indexes stored in existing temporary CEs.
     * We need to use a linked graph with stable indexes to existing nodes.
     * A doubly-linked list seems easiest to maintain.
     *
     * Each node is stored as an int64_t, with its fields stored as bit fields.
     *
     * Root primary node:
     * - primary weight: 32 bits 63..32
     * - reserved/unused/zero: 4 bits 31..28
     *
     * Weaker root nodes & tailored nodes:
     * - a weight: 16 bits 63..48
     *   + a root or default weight
     *   + unused/zero for a tailored node
     * - index to the previous node: 20 bits 47..28
     *
     * All types of nodes:
     * - index to the next node: 20 bits 27..8
     *   + nextIndex=0 in last node per root-primary list
     * - HAS_BEFORE2: bit 6
     * - HAS_BEFORE3: bit 5
     * - IS_TAILORED: bit 3
     * - the difference strength (primary/secondary/tertiary/quaternary): 2 bits 1..0
     *
     * - reserved/unused/zero bits: bits 7, 4, 2
     *
     * We could allocate structs with pointers, but we would have to store them
     * in a pointer list so that they can be indexed from temporary CEs,
     * and they would require more memory allocations.
     */
    UVector64 nodes;
};

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
#endif  // __COLLATIONBUILDER_H__
