#ifndef LIBBIGWIG_VALUES_H
#define LIBBIGWIG_VALUES_H

#include <inttypes.h>
/*! \file bwValues.h
 *
 * You should not directly use functions and structures defined here. They're really meant for internal use only.
 *
 * All of the structures here need to be destroyed or you'll leak memory! There are methods available to destroy anything that you need to take care of yourself.
 */

//N.B., coordinates are still 0-based half open!
/*!
 * @brief A node within an R-tree holding the index for data.
 *
 * Note that there are two types of nodes: leaf and twig. Leaf nodes point to where data actually is. Twig nodes point to additional index nodes, which may or may not be leaves. Each of these nodes has additional children, which may span multiple chromosomes/contigs.
 *
 * With the start/end position, these positions refer specifically to the chromosomes specified in chrIdxStart/chrIdxEnd. Any chromosomes between these are completely spanned by a given child.
 */
typedef struct bwRTreeNode_t {
    uint8_t isLeaf; /**<Is this node a leaf?*/
    //1 byte of padding
    uint16_t nChildren; /**<The number of children of this node, all lists have this length.*/
    uint32_t *chrIdxStart; /**<A list of the starting chromosome indices of each child.*/
    uint32_t *baseStart; /**<A list of the start position of each child.*/
    uint32_t *chrIdxEnd; /**<A list of the end chromosome indices of each child.*/
    uint32_t *baseEnd; /**<A list of the end position of each child.*/
    uint64_t *dataOffset; /**<For leaves, the offset to the on-disk data. For twigs, the offset to the child node.*/
    union {
        uint64_t *size; /**<Leaves only: The size of the data block.*/
        struct bwRTreeNode_t **child; /**<Twigs only: The child node(s).*/
    } x; /**<A union holding either size or child*/
} bwRTreeNode_t;

/*!
 * A header and index that points to an R-tree that in turn points to data blocks.
 */
//TODO rootOffset is pointless, it's 48bytes after the indexOffset
typedef struct {
    uint32_t blockSize; /**<The maximum number of children a node can have*/
    uint64_t nItems; /**<The total number of data blocks pointed to by the tree. This is completely redundant.*/
    uint32_t chrIdxStart; /**<The index to the first chromosome described.*/
    uint32_t baseStart; /**<The first position on chrIdxStart with a value.*/
    uint32_t chrIdxEnd; /**<The index of the last chromosome with an entry.*/
    uint32_t baseEnd; /**<The last position on chrIdxEnd with an entry.*/
    uint64_t idxSize; /**<This is actually the offset of the index rather than the size?!? Yes, it's completely redundant.*/
    uint32_t nItemsPerSlot; /**<This is always 1!*/
    //There's 4 bytes of padding in the file here
    uint64_t rootOffset; /**<The offset to the root node of the R-Tree (on disk). Yes, this is redundant.*/
    bwRTreeNode_t *root; /**<A pointer to the root node.*/
} bwRTree_t;

/*!
 * @brief This structure holds the data blocks that overlap a given interval.
 */
typedef struct {
    uint64_t n; /**<The number of blocks that overlap. This *MAY* be 0!.*/
    uint64_t *offset; /**<The offset to the on-disk position of the block.*/
    uint64_t *size; /**<The size of each block on disk (in bytes).*/
} bwOverlapBlock_t;

/*!
 * @brief The header section of a given data block.
 *
 * There are 3 types of data blocks in bigWig files, each with slightly different needs. This is all taken care of internally.
 */
typedef struct {
    uint32_t tid; /**<The chromosome ID.*/
    uint32_t start; /**<The start position of a block*/
    uint32_t end; /**<The end position of a block*/
    uint32_t step; /**<The step size of the values*/
    uint32_t span; /**<The span of each data value*/
    uint8_t type; /**<The block type: 1, bedGraph; 2, variable step; 3, fixed step.*/
    uint16_t nItems; /**<The number of values in a given block.*/
} bwDataHeader_t;

#endif // LIBBIGWIG_VALUES_H
