/*! \file bwCommon.h
 *
 * You have no reason to use these functions. They may change without warning because there's no reason for them to be used outside of libBigWig's internals.
 *
 * These are structures and functions from a variety of files that are used across files internally but don't need to be see by libBigWig users.
 */

/*!
 * @brief Like fsetpos, but for local or remote bigWig files.
 * This will set the file position indicator to the specified point. For local files this literally is `fsetpos`, while for remote files it fills a memory buffer with data starting at the desired position.
 * @param fp A valid opened bigWigFile_t.
 * @param pos The position within the file to seek to.
 * @return 0 on success and -1 on error.
 */
int bwSetPos(bigWigFile_t *fp, size_t pos);

/*!
 * @brief A local/remote version of `fread`.
 * Reads data from either local or remote bigWig files.
 * @param data An allocated memory block big enough to hold the data.
 * @param sz The size of each member that should be copied.
 * @param nmemb The number of members to copy.
 * @param fp The bigWigFile_t * from which to copy the data.
 * @see bwSetPos
 * @return For nmemb==1, the size of the copied data. For nmemb>1, the number of members fully copied (this is equivalent to `fread`).
 */
size_t bwRead(void *data, size_t sz, size_t nmemb, bigWigFile_t *fp);

/*!
 * @brief Determine what the file position indicator say.
 * This is equivalent to `ftell` for local or remote files.
 * @param fp The file.
 * @return The position in the file.
 */
long bwTell(bigWigFile_t *fp);

/*!
 * @brief Reads a data index (either full data or a zoom level) from a bigWig file.
 * There is little reason for end users to use this function. This must be freed with `bwDestroyIndex`
 * @param fp A valid bigWigFile_t pointer
 * @param offset The file offset where the index begins
 * @return A bwRTree_t pointer or NULL on error.
 */
bwRTree_t *bwReadIndex(bigWigFile_t *fp, uint64_t offset);

/*!
 * @brief Destroy an bwRTreeNode_t and all of its children.
 * @param node The node to destroy.
 */
void bwDestroyIndexNode(bwRTreeNode_t *node);

/*!
 * @brief Frees space allocated by `bwReadIndex`
 * There is generally little reason to use this, since end users should typically not need to run `bwReadIndex` themselves.
 * @param idx A bwRTree_t pointer allocated by `bwReadIndex`.
 */
void bwDestroyIndex(bwRTree_t *idx);

/// @cond SKIP
bwOverlapBlock_t *walkRTreeNodes(bigWigFile_t *bw, bwRTreeNode_t *root, uint32_t tid, uint32_t start, uint32_t end);
void destroyBWOverlapBlock(bwOverlapBlock_t *b);
/// @endcond

/*!
 * @brief Finishes what's needed to write a bigWigFile
 * Flushes the buffer, converts the index linked list to a tree, writes that to disk, handles zoom level stuff, writes magic at the end
 * @param fp A valid bigWigFile_t pointer
 * @return 0 on success
 */
int bwFinalize(bigWigFile_t *fp);

/// @cond SKIP
char *bwStrdup(const char *s);
/// @endcond
