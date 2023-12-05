#include "bigWig.h"
#include "bwCommon.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <zlib.h>
#include <errno.h>

static uint32_t roundup(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

//Returns the root node on success and NULL on error
static bwRTree_t *readRTreeIdx(bigWigFile_t *fp, uint64_t offset) {
    uint32_t magic;
    bwRTree_t *node;

    if(!offset) {
        if(bwSetPos(fp, fp->hdr->indexOffset)) return NULL;
    } else {
        if(bwSetPos(fp, offset)) return NULL;
    }

    if(bwRead(&magic, sizeof(uint32_t), 1, fp) != 1) return NULL;
    if(magic != IDX_MAGIC) {
        fprintf(stderr, "[readRTreeIdx] Mismatch in the magic number!\n");
        return NULL;
    }

    node = calloc(1, sizeof(bwRTree_t));
    if(!node) return NULL;

    if(bwRead(&(node->blockSize), sizeof(uint32_t), 1, fp) != 1) goto error;
    if(bwRead(&(node->nItems), sizeof(uint64_t), 1, fp) != 1) goto error;
    if(bwRead(&(node->chrIdxStart), sizeof(uint32_t), 1, fp) != 1) goto error;
    if(bwRead(&(node->baseStart), sizeof(uint32_t), 1, fp) != 1) goto error;
    if(bwRead(&(node->chrIdxEnd), sizeof(uint32_t), 1, fp) != 1) goto error;
    if(bwRead(&(node->baseEnd), sizeof(uint32_t), 1, fp) != 1) goto error;
    if(bwRead(&(node->idxSize), sizeof(uint64_t), 1, fp) != 1) goto error;
    if(bwRead(&(node->nItemsPerSlot), sizeof(uint32_t), 1, fp) != 1) goto error;
    //Padding
    if(bwRead(&(node->blockSize), sizeof(uint32_t), 1, fp) != 1) goto error;
    node->rootOffset = bwTell(fp);

    //For remote files, libCurl sometimes sets errno to 115 and doesn't clear it
    errno = 0;

    return node;

error:
    free(node);
    return NULL;
}

//Returns a bwRTreeNode_t on success and NULL on an error
//For the root node, set offset to 0
static bwRTreeNode_t *bwGetRTreeNode(bigWigFile_t *fp, uint64_t offset) {
    bwRTreeNode_t *node = NULL;
    uint8_t padding;
    uint16_t i;
    if(offset) {
        if(bwSetPos(fp, offset)) return NULL;
    } else {
        //seek
        if(bwSetPos(fp, fp->idx->rootOffset)) return NULL;
    }

    node = calloc(1, sizeof(bwRTreeNode_t));
    if(!node) return NULL;

    if(bwRead(&(node->isLeaf), sizeof(uint8_t), 1, fp) != 1) goto error;
    if(bwRead(&padding, sizeof(uint8_t), 1, fp) != 1) goto error;
    if(bwRead(&(node->nChildren), sizeof(uint16_t), 1, fp) != 1) goto error;

    node->chrIdxStart = malloc(sizeof(uint32_t)*(node->nChildren));
    if(!node->chrIdxStart) goto error;
    node->baseStart = malloc(sizeof(uint32_t)*(node->nChildren));
    if(!node->baseStart) goto error;
    node->chrIdxEnd = malloc(sizeof(uint32_t)*(node->nChildren));
    if(!node->chrIdxEnd) goto error;
    node->baseEnd = malloc(sizeof(uint32_t)*(node->nChildren));
    if(!node->baseEnd) goto error;
    node->dataOffset = malloc(sizeof(uint64_t)*(node->nChildren));
    if(!node->dataOffset) goto error;
    if(node->isLeaf) {
        node->x.size = malloc(node->nChildren * sizeof(uint64_t));
        if(!node->x.size) goto error;
    } else {
        node->x.child = calloc(node->nChildren, sizeof(struct bwRTreeNode_t *));
        if(!node->x.child) goto error;
    }
    for(i=0; i<node->nChildren; i++) {
        if(bwRead(&(node->chrIdxStart[i]), sizeof(uint32_t), 1, fp) != 1) goto error;
        if(bwRead(&(node->baseStart[i]), sizeof(uint32_t), 1, fp) != 1) goto error;
        if(bwRead(&(node->chrIdxEnd[i]), sizeof(uint32_t), 1, fp) != 1) goto error;
        if(bwRead(&(node->baseEnd[i]), sizeof(uint32_t), 1, fp) != 1) goto error;
        if(bwRead(&(node->dataOffset[i]), sizeof(uint64_t), 1, fp) != 1) goto error;
        if(node->isLeaf) {
            if(bwRead(&(node->x.size[i]), sizeof(uint64_t), 1, fp) != 1) goto error;
        }
    }

    return node;

error:
    if(node->chrIdxStart) free(node->chrIdxStart);
    if(node->baseStart) free(node->baseStart);
    if(node->chrIdxEnd) free(node->chrIdxEnd);
    if(node->baseEnd) free(node->baseEnd);
    if(node->dataOffset) free(node->dataOffset);
    if(node->isLeaf && node->x.size) free(node->x.size);
    else if((!node->isLeaf) && node->x.child) free(node->x.child);
    free(node);
    return NULL;
}

void destroyBWOverlapBlock(bwOverlapBlock_t *b) {
    if(!b) return;
    if(b->size) free(b->size);
    if(b->offset) free(b->offset);
    free(b);
}

//Returns a bwOverlapBlock_t * object or NULL on error.
static bwOverlapBlock_t *overlapsLeaf(bwRTreeNode_t *node, uint32_t tid, uint32_t start, uint32_t end) {
    uint16_t i, idx = 0;
    bwOverlapBlock_t *o = calloc(1, sizeof(bwOverlapBlock_t));
    if(!o) return NULL;

    for(i=0; i<node->nChildren; i++) {
        if(tid < node->chrIdxStart[i]) break;
        if(tid > node->chrIdxEnd[i]) continue;

        /*
          The individual blocks can theoretically span multiple contigs.
          So if we treat the first/last contig in the range as special
          but anything in the middle is a guaranteed match
        */
        if(node->chrIdxStart[i] != node->chrIdxEnd[i]) {
            if(tid == node->chrIdxStart[i]) {
                if(node->baseStart[i] >= end) break;
            } else if(tid == node->chrIdxEnd[i]) {
                if(node->baseEnd[i] <= start) continue;
            }
        } else {
            if(node->baseStart[i] >= end || node->baseEnd[i] <= start) continue;
        }
        o->n++;
    }

    if(o->n) {
        o->offset = malloc(sizeof(uint64_t) * (o->n));
        if(!o->offset) goto error;
        o->size = malloc(sizeof(uint64_t) * (o->n));
        if(!o->size) goto error;

        for(i=0; i<node->nChildren; i++) {
            if(tid < node->chrIdxStart[i]) break;
            if(tid < node->chrIdxStart[i] || tid > node->chrIdxEnd[i]) continue;
            if(node->chrIdxStart[i] != node->chrIdxEnd[i]) {
                if(tid == node->chrIdxStart[i]) {
                    if(node->baseStart[i] >= end) continue;
                } else if(tid == node->chrIdxEnd[i]) {
                    if(node->baseEnd[i] <= start) continue;
                }
            } else {
                if(node->baseStart[i] >= end || node->baseEnd[i] <= start) continue;
            }
            o->offset[idx] = node->dataOffset[i];
            o->size[idx++] = node->x.size[i];
            if(idx >= o->n) break;
        }
    }

    if(idx != o->n) { //This should never happen
        fprintf(stderr, "[overlapsLeaf] Mismatch between number of overlaps calculated and found!\n");
        goto error;
    }

    return o;

error:
    if(o) destroyBWOverlapBlock(o);
    return NULL;
}

//This will free l2 unless there's an error!
//Returns NULL on error, otherwise the merged lists
static bwOverlapBlock_t *mergeOverlapBlocks(bwOverlapBlock_t *b1, bwOverlapBlock_t *b2) {
    uint64_t i,j;
    if(!b2) return b1;
    if(!b2->n) {
        destroyBWOverlapBlock(b2);
        return b1;
    }
    if(!b1->n) {
        destroyBWOverlapBlock(b1);
        return b2;
    }
    j = b1->n;
    b1->n += b2->n;
    b1->offset = realloc(b1->offset, sizeof(uint64_t) * (b1->n+b2->n));
    if(!b1->offset) goto error;
    b1->size = realloc(b1->size, sizeof(uint64_t) * (b1->n+b2->n));
    if(!b1->size) goto error;

    for(i=0; i<b2->n; i++) {
        b1->offset[j+i] = b2->offset[i];
        b1->size[j+i] = b2->size[i];
    }
    destroyBWOverlapBlock(b2);
    return b1;

error:
    destroyBWOverlapBlock(b1);
    return NULL;
}

//Returns NULL and sets nOverlaps to >0 on error, otherwise nOverlaps is the number of file offsets returned
//The output needs to be free()d if not NULL (likewise with *sizes)
static bwOverlapBlock_t *overlapsNonLeaf(bigWigFile_t *fp, bwRTreeNode_t *node, uint32_t tid, uint32_t start, uint32_t end) {
    uint16_t i;
    bwOverlapBlock_t *nodeBlocks, *output = calloc(1, sizeof(bwOverlapBlock_t));
    if(!output) return NULL;

    for(i=0; i<node->nChildren; i++) {
        if(tid < node->chrIdxStart[i]) break;
        if(tid < node->chrIdxStart[i] || tid > node->chrIdxEnd[i]) continue;
        if(node->chrIdxStart[i] != node->chrIdxEnd[i]) { //child spans contigs
            if(tid == node->chrIdxStart[i]) {
                if(node->baseStart[i] >= end) continue;
            } else if(tid == node->chrIdxEnd[i]) {
                if(node->baseEnd[i] <= start) continue;
            }
        } else {
            if(end <= node->baseStart[i] || start >= node->baseEnd[i]) continue;
        }

        //We have an overlap!
        if(!node->x.child[i])
          node->x.child[i] = bwGetRTreeNode(fp, node->dataOffset[i]);
        if(!node->x.child[i]) goto error;

        if(node->x.child[i]->isLeaf) { //leaf
            nodeBlocks = overlapsLeaf(node->x.child[i], tid, start, end);
        } else { //non-leaf
            nodeBlocks = overlapsNonLeaf(fp, node->x.child[i], tid, start, end);
        }

        //The output is processed the same regardless of leaf/non-leaf
        if(!nodeBlocks) goto error;
        else {
            output = mergeOverlapBlocks(output, nodeBlocks);
            if(!output) {
                destroyBWOverlapBlock(nodeBlocks);
                goto error;
            }
        }
    }

    return output;

error:
    destroyBWOverlapBlock(output);
    return NULL;
}

//Returns NULL and sets nOverlaps to >0 on error, otherwise nOverlaps is the number of file offsets returned
//The output must be free()d
bwOverlapBlock_t *walkRTreeNodes(bigWigFile_t *bw, bwRTreeNode_t *root, uint32_t tid, uint32_t start, uint32_t end) {
    if(root->isLeaf) return overlapsLeaf(root, tid, start, end);
    return overlapsNonLeaf(bw, root, tid, start, end);
}

//In reality, a hash or some sort of tree structure is probably faster...
//Return -1 (AKA 0xFFFFFFFF...) on "not there", so we can hold (2^32)-1 items.
uint32_t bwGetTid(const bigWigFile_t *fp, const char *chrom) {
    uint32_t i;
    if(!chrom) return -1;
    for(i=0; i<fp->cl->nKeys; i++) {
        if(strcmp(chrom, fp->cl->chrom[i]) == 0) return i;
    }
    return -1;
}

static bwOverlapBlock_t *bwGetOverlappingBlocks(bigWigFile_t *fp, const char *chrom, uint32_t start, uint32_t end) {
    uint32_t tid = bwGetTid(fp, chrom);

    if(tid == (uint32_t) -1) {
        fprintf(stderr, "[bwGetOverlappingBlocks] Non-existent contig: %s\n", chrom);
        return NULL;
    }

    //Get the info if needed
    if(!fp->idx) {
        fp->idx = readRTreeIdx(fp, fp->hdr->indexOffset);
        if(!fp->idx) {
            return NULL;
        }
    }

    if(!fp->idx->root) fp->idx->root = bwGetRTreeNode(fp, 0);
    if(!fp->idx->root) return NULL;

    return walkRTreeNodes(fp, fp->idx->root, tid, start, end);
}

void bwFillDataHdr(bwDataHeader_t *hdr, void *b) {
    hdr->tid = ((uint32_t*)b)[0];
    hdr->start = ((uint32_t*)b)[1];
    hdr->end = ((uint32_t*)b)[2];
    hdr->step = ((uint32_t*)b)[3];
    hdr->span = ((uint32_t*)b)[4];
    hdr->type = ((uint8_t*)b)[20];
    hdr->nItems = ((uint16_t*)b)[11];
}

void bwDestroyOverlappingIntervals(bwOverlappingIntervals_t *o) {
    if(!o) return;
    if(o->start) free(o->start);
    if(o->end) free(o->end);
    if(o->value) free(o->value);
    free(o);
}

void bbDestroyOverlappingEntries(bbOverlappingEntries_t *o) {
    uint32_t i;
    if(!o) return;
    if(o->start) free(o->start);
    if(o->end) free(o->end);
    if(o->str) {
        for(i=0; i<o->l; i++) {
            if(o->str[i]) free(o->str[i]);
        }
        free(o->str);
    }
    free(o);
}

//Returns NULL on error, in which case o has been free()d
static bwOverlappingIntervals_t *pushIntervals(bwOverlappingIntervals_t *o, uint32_t start, uint32_t end, float value) {
    if(o->l+1 >= o->m) {
        o->m = roundup(o->l+1);
        o->start = realloc(o->start, o->m * sizeof(uint32_t));
        if(!o->start) goto error;
        o->end = realloc(o->end, o->m * sizeof(uint32_t));
        if(!o->end) goto error;
        o->value = realloc(o->value, o->m * sizeof(float));
        if(!o->value) goto error;
    }
    o->start[o->l] = start;
    o->end[o->l] = end;
    o->value[o->l++] = value;
    return o;

error:
    bwDestroyOverlappingIntervals(o);
    return NULL;
}

static bbOverlappingEntries_t *pushBBIntervals(bbOverlappingEntries_t *o, uint32_t start, uint32_t end, char *str, int withString) {
    if(o->l+1 >= o->m) {
        o->m = roundup(o->l+1);
        o->start = realloc(o->start, o->m * sizeof(uint32_t));
        if(!o->start) goto error;
        o->end = realloc(o->end, o->m * sizeof(uint32_t));
        if(!o->end) goto error;
        if(withString) {
            o->str = realloc(o->str, o->m * sizeof(char**));
            if(!o->str) goto error;
        }
    }
    o->start[o->l] = start;
    o->end[o->l] = end;
    if(withString) o->str[o->l] = bwStrdup(str);
    o->l++;
    return o;

error:
    bbDestroyOverlappingEntries(o);
    return NULL;
}

//Returns NULL on error
bwOverlappingIntervals_t *bwGetOverlappingIntervalsCore(bigWigFile_t *fp, bwOverlapBlock_t *o, uint32_t tid, uint32_t ostart, uint32_t oend) {
    uint64_t i;
    uint16_t j;
    int compressed = 0, rv;
    uLongf sz = fp->hdr->bufSize, tmp;
    void *buf = NULL, *compBuf = NULL;
    uint32_t start = 0, end , *p;
    float value;
    bwDataHeader_t hdr;
    bwOverlappingIntervals_t *output = calloc(1, sizeof(bwOverlappingIntervals_t));

    if(!output) goto error;

    if(!o) return output;
    if(!o->n) return output;

    if(sz) {
        compressed = 1;
        buf = malloc(sz);
    }
    sz = 0; //This is now the size of the compressed buffer

    for(i=0; i<o->n; i++) {
        if(bwSetPos(fp, o->offset[i])) goto error;

        if(sz < o->size[i]) {
            compBuf = realloc(compBuf, o->size[i]);
            sz = o->size[i];
        }
        if(!compBuf) goto error;

        if(bwRead(compBuf, o->size[i], 1, fp) != 1) goto error;
        if(compressed) {
            tmp = fp->hdr->bufSize; //This gets over-written by uncompress
            rv = uncompress(buf, (uLongf *) &tmp, compBuf, o->size[i]);
            if(rv != Z_OK) goto error;
        } else {
            buf = compBuf;
        }

        //TODO: ensure that tmp is large enough!
        bwFillDataHdr(&hdr, buf);

        p = ((uint32_t*) buf);
        p += 6;
        if(hdr.tid != tid) continue;

        if(hdr.type == 3) start = hdr.start - hdr.step;

        //FIXME: We should ensure that sz is large enough to hold nItems of the given type
        for(j=0; j<hdr.nItems; j++) {
            switch(hdr.type) {
            case 1:
                start = *p;
                p++;
                end = *p;
                p++;
                value = *((float *)p);
                p++;
                break;
            case 2:
                start = *p;
                p++;
                end = start + hdr.span;
                value = *((float *)p);
                p++;
                break;
            case 3:
                start += hdr.step;
                end = start+hdr.span;
                value = *((float *)p);
                p++;
                break;
            default :
                goto error;
                break;
            }

            if(end <= ostart || start >= oend) continue;
            //Push the overlap
            if(!pushIntervals(output, start, end, value)) goto error;
        }
    }

    if(compressed && buf) free(buf);
    if(compBuf) free(compBuf);
    return output;

error:
    fprintf(stderr, "[bwGetOverlappingIntervalsCore] Got an error\n");
    if(output) bwDestroyOverlappingIntervals(output);
    if(compressed && buf) free(buf);
    if(compBuf) free(compBuf);
    return NULL;
}

bbOverlappingEntries_t *bbGetOverlappingEntriesCore(bigWigFile_t *fp, bwOverlapBlock_t *o, uint32_t tid, uint32_t ostart, uint32_t oend, int withString) {
    uint64_t i;
    int compressed = 0, rv, slen;
    uLongf sz = fp->hdr->bufSize, tmp = 0;
    void *buf = NULL, *bufEnd = NULL, *compBuf = NULL;
    uint32_t entryTid = 0, start = 0, end;
    char *str;
    bbOverlappingEntries_t *output = calloc(1, sizeof(bbOverlappingEntries_t));

    if(!output) goto error;

    if(!o) return output;
    if(!o->n) return output;

    if(sz) {
        compressed = 1;
        buf = malloc(sz);
    }
    sz = 0; //This is now the size of the compressed buffer

    for(i=0; i<o->n; i++) {
        if(bwSetPos(fp, o->offset[i])) goto error;

        if(sz < o->size[i]) {
            compBuf = realloc(compBuf, o->size[i]);
            sz = o->size[i];
        }
        if(!compBuf) goto error;

        if(bwRead(compBuf, o->size[i], 1, fp) != 1) goto error;
        if(compressed) {
            tmp = fp->hdr->bufSize; //This gets over-written by uncompress
            rv = uncompress(buf, (uLongf *) &tmp, compBuf, o->size[i]);
            if(rv != Z_OK) goto error;
        } else {
            buf = compBuf;
            tmp = o->size[i]; //TODO: Is this correct? Do non-gzipped bigBeds exist?
        }

        bufEnd = buf + tmp;
        while(buf < bufEnd) {
            entryTid = ((uint32_t*)buf)[0];
            start = ((uint32_t*)buf)[1];
            end = ((uint32_t*)buf)[2];
            buf += 12;
            str = (char*)buf;
            slen = strlen(str) + 1;
            buf += slen;

            if(entryTid < tid) continue;
            if(entryTid > tid) break;
            if(end <= ostart) continue;
            if(start >= oend) break;

            //Push the overlap
            if(!pushBBIntervals(output, start, end, str, withString)) goto error;
        }

        buf = bufEnd - tmp; //reset the buffer pointer
    }

    if(compressed && buf) free(buf);
    if(compBuf) free(compBuf);
    return output;

error:
    fprintf(stderr, "[bbGetOverlappingEntriesCore] Got an error\n");
    buf = bufEnd - tmp;
    if(output) bbDestroyOverlappingEntries(output);
    if(compressed && buf) free(buf);
    if(compBuf) free(compBuf);
    return NULL;
}

//Returns NULL on error OR no intervals, which is a bad design...
bwOverlappingIntervals_t *bwGetOverlappingIntervals(bigWigFile_t *fp, const char *chrom, uint32_t start, uint32_t end) {
    bwOverlappingIntervals_t *output;
    uint32_t tid = bwGetTid(fp, chrom);
    if(tid == (uint32_t) -1) return NULL;
    bwOverlapBlock_t *blocks = bwGetOverlappingBlocks(fp, chrom, start, end);
    if(!blocks) return NULL;
    output = bwGetOverlappingIntervalsCore(fp, blocks, tid, start, end);
    destroyBWOverlapBlock(blocks);
    return output;
}

//Like above, but for bigBed files
bbOverlappingEntries_t *bbGetOverlappingEntries(bigWigFile_t *fp, const char *chrom, uint32_t start, uint32_t end, int withString) {
    bbOverlappingEntries_t *output;
    uint32_t tid = bwGetTid(fp, chrom);
    if(tid == (uint32_t) -1) return NULL;
    bwOverlapBlock_t *blocks = bwGetOverlappingBlocks(fp, chrom, start, end);
    if(!blocks) return NULL;
    output = bbGetOverlappingEntriesCore(fp, blocks, tid, start, end, withString);
    destroyBWOverlapBlock(blocks);
    return output;
}

//Returns NULL on error
bwOverlapIterator_t *bwOverlappingIntervalsIterator(bigWigFile_t *fp, const char *chrom, uint32_t start, uint32_t end, uint32_t blocksPerIteration) {
    bwOverlapIterator_t *output = NULL;
    uint64_t n;
    uint32_t tid = bwGetTid(fp, chrom);
    if(tid == (uint32_t) -1) return output;
    output = calloc(1, sizeof(bwOverlapIterator_t));
    if(!output) return output;
    bwOverlapBlock_t *blocks = bwGetOverlappingBlocks(fp, chrom, start, end);

    output->bw = fp;
    output->tid = tid;
    output->start = start;
    output->end = end;
    output->blocks = blocks;
    output->blocksPerIteration = blocksPerIteration;

    if(blocks) {
        n = blocks->n;
        if(n>blocksPerIteration) blocks->n = blocksPerIteration;
        output->intervals = bwGetOverlappingIntervalsCore(fp, blocks,tid, start, end);
        blocks->n = n;
        output->offset = blocksPerIteration;
    }
    output->data = output->intervals;
    return output;
}

//Returns NULL on error
bwOverlapIterator_t *bbOverlappingEntriesIterator(bigWigFile_t *fp, const char *chrom, uint32_t start, uint32_t end, int withString, uint32_t blocksPerIteration) {
    bwOverlapIterator_t *output = NULL;
    uint64_t n;
    uint32_t tid = bwGetTid(fp, chrom);
    if(tid == (uint32_t) -1) return output;
    output = calloc(1, sizeof(bwOverlapIterator_t));
    if(!output) return output;
    bwOverlapBlock_t *blocks = bwGetOverlappingBlocks(fp, chrom, start, end);

    output->bw = fp;
    output->tid = tid;
    output->start = start;
    output->end = end;
    output->blocks = blocks;
    output->blocksPerIteration = blocksPerIteration;
    output->withString = withString;

    if(blocks) {
        n = blocks->n;
        if(n>blocksPerIteration) blocks->n = blocksPerIteration;
        output->entries = bbGetOverlappingEntriesCore(fp, blocks,tid, start, end, withString);
        blocks->n = n;
        output->offset = blocksPerIteration;
    }
    output->data = output->entries;
    return output;
}

void bwIteratorDestroy(bwOverlapIterator_t *iter) {
    if(!iter) return;
    if(iter->blocks) destroyBWOverlapBlock((bwOverlapBlock_t*) iter->blocks);
    if(iter->intervals) bwDestroyOverlappingIntervals(iter->intervals);
    if(iter->entries) bbDestroyOverlappingEntries(iter->entries);
    free(iter);
}

//On error, points to NULL and destroys the input
bwOverlapIterator_t *bwIteratorNext(bwOverlapIterator_t *iter) {
    uint64_t n, *offset, *size;
    bwOverlapBlock_t *blocks = iter->blocks;

    if(iter->intervals) {
        bwDestroyOverlappingIntervals(iter->intervals);
        iter->intervals = NULL;
    }
    if(iter->entries) {
        bbDestroyOverlappingEntries(iter->entries);
        iter->entries = NULL;
    }
    iter->data = NULL;

    if(iter->offset < blocks->n) {
        //store the previous values
        n = blocks->n;
        offset = blocks->offset;
        size = blocks->size;

        //Move the start of the blocks
        blocks->offset += iter->offset;
        blocks->size += iter->offset;
        if(iter->offset + iter->blocksPerIteration > n) {
            blocks->n = blocks->n - iter->offset;
        } else {
            blocks->n = iter->blocksPerIteration;
        }

        //Get the intervals or entries, as appropriate
        if(iter->bw->type == 0) {
            //bigWig
            iter->intervals = bwGetOverlappingIntervalsCore(iter->bw, blocks, iter->tid, iter->start, iter->end);
            iter->data = iter->intervals;
        } else {
            //bigBed
            iter->entries = bbGetOverlappingEntriesCore(iter->bw, blocks, iter->tid, iter->start, iter->end, iter->withString);
            iter->data = iter->entries;
        }
        iter->offset += iter->blocksPerIteration;

        //reset the values in iter->blocks
        blocks->n = n;
        blocks->offset = offset;
        blocks->size = size;

        //Check for error
        if(!iter->intervals && !iter->entries) goto error;
    }

    return iter;

error:
    bwIteratorDestroy(iter);
    return NULL;
}

//This is like bwGetOverlappingIntervals, except it returns 1 base windows. If includeNA is not 0, then a value will be returned for every position in the range (defaulting to NAN).
//The ->end member is NULL
//If includeNA is not 0 then ->start is also NULL, since it's implied
//Note that bwDestroyOverlappingIntervals() will work in either case
bwOverlappingIntervals_t *bwGetValues(bigWigFile_t *fp, const char *chrom, uint32_t start, uint32_t end, int includeNA) {
    uint32_t i, j, n;
    bwOverlappingIntervals_t *output = NULL;
    bwOverlappingIntervals_t *intermediate = bwGetOverlappingIntervals(fp, chrom, start, end);
    if(!intermediate) return NULL;

    output = calloc(1, sizeof(bwOverlappingIntervals_t));
    if(!output) goto error;
    if(includeNA) {
        output->l = end-start;
        output->value = malloc(output->l*sizeof(float));
        if(!output->value) goto error;
        for(i=0; i<output->l; i++) output->value[i] = NAN;
        for(i=0; i<intermediate->l; i++) {
            for(j=intermediate->start[i]; j<intermediate->end[i]; j++) {
                if(j < start || j >= end) continue;
                output->value[j-start] = intermediate->value[i];
            }
        }
    } else {
        n = 0;
        for(i=0; i<intermediate->l; i++) {
            if(intermediate->start[i] < start) intermediate->start[i] = start;
            if(intermediate->end[i] > end) intermediate->end[i] = end;
            n += intermediate->end[i]-intermediate->start[i];
        }
        output->l = n;
        output->start = malloc(sizeof(uint32_t)*n);
        if(!output->start) goto error;
        output->value = malloc(sizeof(float)*n);
        if(!output->value) goto error;
        n = 0; //this is now the index
        for(i=0; i<intermediate->l; i++) {
            for(j=intermediate->start[i]; j<intermediate->end[i]; j++) {
                if(j < start || j >= end) continue;
                output->start[n] = j;
                output->value[n++] = intermediate->value[i];
            }
        }
    }

    bwDestroyOverlappingIntervals(intermediate);
    return output;

error:
    if(intermediate) bwDestroyOverlappingIntervals(intermediate);
    if(output) bwDestroyOverlappingIntervals(output);
    return NULL;
}

void bwDestroyIndexNode(bwRTreeNode_t *node) {
    uint16_t i;

    if(!node) return;

    free(node->chrIdxStart);
    free(node->baseStart);
    free(node->chrIdxEnd);
    free(node->baseEnd);
    free(node->dataOffset);
    if(!node->isLeaf) {
        for(i=0; i<node->nChildren; i++) {
            bwDestroyIndexNode(node->x.child[i]);
        }
        free(node->x.child);
    } else {
        free(node->x.size);
    }
    free(node);
}

void bwDestroyIndex(bwRTree_t *idx) {
    bwDestroyIndexNode(idx->root);
    free(idx);
}

//Returns a pointer to the requested index (@offset, unless it's 0, in which case the index for the values is returned
//Returns NULL on error
bwRTree_t *bwReadIndex(bigWigFile_t *fp, uint64_t offset) {
    bwRTree_t *idx = readRTreeIdx(fp, offset);
    if(!idx) return NULL;

    //Read in the root node
    idx->root = bwGetRTreeNode(fp, idx->rootOffset);

    if(!idx->root) {
        bwDestroyIndex(idx);
        return NULL;
    }
    return idx;
}
