#include <limits.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bigWig.h"
#include "bwCommon.h"

/// @cond SKIP
struct val_t {
    uint32_t tid;
    uint32_t start;
    uint32_t nBases;
    float min, max, sum, sumsq;
    double scalar;
    struct val_t *next;
};
/// @endcond

//Create a chromList_t and attach it to a bigWigFile_t *. Returns NULL on error
//Note that chroms and lengths are duplicated, so you MUST free the input
chromList_t *bwCreateChromList(const char* const* chroms, const uint32_t *lengths, int64_t n) {
    int64_t i = 0;
    chromList_t *cl = calloc(1, sizeof(chromList_t));
    if(!cl) return NULL;

    cl->nKeys = n;
    cl->chrom = malloc(sizeof(char*)*n);
    cl->len = malloc(sizeof(uint32_t)*n);
    if(!cl->chrom) goto error;
    if(!cl->len) goto error;

    for(i=0; i<n; i++) {
        cl->len[i] = lengths[i];
        cl->chrom[i] = bwStrdup(chroms[i]);
        if(!cl->chrom[i]) goto error;
    }

    return cl;

error:
    if(i) {
        int64_t j;
        for(j=0; j<i; j++) free(cl->chrom[j]);
    }
    if(cl) {
        if(cl->chrom) free(cl->chrom);
        if(cl->len) free(cl->len);
        free(cl);
    }
    return NULL;
}

//If maxZooms == 0, then 0 is used (i.e., there are no zoom levels). If maxZooms < 0 or > 65535 then 10 is used.
//TODO allow changing bufSize and blockSize
int bwCreateHdr(bigWigFile_t *fp, int32_t maxZooms) {
    if(!fp->isWrite) return 1;
    bigWigHdr_t *hdr = calloc(1, sizeof(bigWigHdr_t));
    if(!hdr) return 2;

    hdr->version = 4;
    if(maxZooms < 0 || maxZooms > 65535) {
        hdr->nLevels = 10;
    } else {
        hdr->nLevels = maxZooms;
    }

    hdr->bufSize = 32768; //When the file is finalized this is reset if fp->writeBuffer->compressPsz is 0!
    hdr->minVal = DBL_MAX;
    hdr->maxVal = DBL_MIN;
    fp->hdr = hdr;
    fp->writeBuffer->blockSize = 64;

    //Allocate the writeBuffer buffers
    fp->writeBuffer->compressPsz = compressBound(hdr->bufSize);
    fp->writeBuffer->compressP = malloc(fp->writeBuffer->compressPsz);
    if(!fp->writeBuffer->compressP) return 3;
    fp->writeBuffer->p = calloc(1,hdr->bufSize);
    if(!fp->writeBuffer->p) return 4;

    return 0;
}

//return 0 on success
static int writeAtPos(void *ptr, size_t sz, size_t nmemb, size_t pos, FILE *fp) {
    size_t curpos = ftell(fp);
    if(fseek(fp, pos, SEEK_SET)) return 1;
    if(fwrite(ptr, sz, nmemb, fp) != nmemb) return 2;
    if(fseek(fp, curpos, SEEK_SET)) return 3;
    return 0;
}

//We lose keySize bytes on error
static int writeChromList(FILE *fp, chromList_t *cl) {
    uint16_t k;
    uint32_t j, magic = CIRTREE_MAGIC;
    uint32_t nperblock = (cl->nKeys > 0x7FFF) ? 0x7FFF : cl->nKeys; //Items per leaf/non-leaf, there are no unsigned ints in java :(
    uint32_t nblocks, keySize = 0, valSize = 8; //In theory valSize could be optimized, in practice that'd be annoying
    uint64_t i, nonLeafEnd, leafSize, nextLeaf;
    uint8_t eight;
    int64_t i64;
    char *chrom;
    size_t l;

    if(cl->nKeys > 1073676289) {
        fprintf(stderr, "[writeChromList] Error: Currently only 1,073,676,289 contigs are supported. If you really need more then please post a request on github.\n");
        return 1;
    }
    nblocks = cl->nKeys/nperblock;
    nblocks += ((cl->nKeys % nperblock) > 0)?1:0;

    for(i64=0; i64<cl->nKeys; i64++) {
        l = strlen(cl->chrom[i64]);
        if(l>keySize) keySize = l;
    }
    l--; //We don't null terminate strings, because schiess mich tot
    chrom = calloc(keySize, sizeof(char));

    //Write the root node of a largely pointless tree
    if(fwrite(&magic, sizeof(uint32_t), 1, fp) != 1) return 1;
    if(fwrite(&nperblock, sizeof(uint32_t), 1, fp) != 1) return 2;
    if(fwrite(&keySize, sizeof(uint32_t), 1, fp) != 1) return 3;
    if(fwrite(&valSize, sizeof(uint32_t), 1, fp) != 1) return 4;
    if(fwrite(&(cl->nKeys), sizeof(uint64_t), 1, fp) != 1) return 5;

    //Padding?
    i=0;
    if(fwrite(&i, sizeof(uint64_t), 1, fp) != 1) return 6;

    //Do we need a non-leaf node?
    if(nblocks > 1) {
        eight = 0;
        if(fwrite(&eight, sizeof(uint8_t), 1, fp) != 1) return 7;
        if(fwrite(&eight, sizeof(uint8_t), 1, fp) != 1) return 8; //padding
        if(fwrite(&nblocks, sizeof(uint16_t), 1, fp) != 1) return 8;
        nonLeafEnd = ftell(fp) + nperblock * (keySize + 8);
        leafSize = nperblock * (keySize + 8) + 4;
        for(i=0; i<nblocks; i++) { //Why yes, this is pointless
            chrom = strncpy(chrom, cl->chrom[i * nperblock], keySize);
            nextLeaf = nonLeafEnd + i * leafSize;
            if(fwrite(chrom, keySize, 1, fp) != 1) return 9;
            if(fwrite(&nextLeaf, sizeof(uint64_t), 1, fp) != 1) return 10;
        }
        for(i=0; i<keySize; i++) chrom[i] = '\0';
        nextLeaf = 0;
        for(i=nblocks; i<nperblock; i++) {
            if(fwrite(chrom, keySize, 1, fp) != 1) return 9;
            if(fwrite(&nextLeaf, sizeof(uint64_t), 1, fp) != 1) return 10;
        }
    }

    //Write the leaves
    nextLeaf = 0;
    for(i=0, j=0; i<nblocks; i++) {
        eight = 1;
        if(fwrite(&eight, sizeof(uint8_t), 1, fp) != 1) return 11;
        eight = 0;
        if(fwrite(&eight, sizeof(uint8_t), 1, fp) != 1) return 12;
        if(cl->nKeys - j < nperblock) {
            k = cl->nKeys - j;
            if(fwrite(&k, sizeof(uint16_t), 1, fp) != 1) return 13;
        } else {
            if(fwrite(&nperblock, sizeof(uint16_t), 1, fp) != 1) return 13;
        }
        for(k=0; k<nperblock; k++) {
            if(j>=cl->nKeys) {
                if(chrom[0]) {
                    for(l=0; l<keySize; l++) chrom[l] = '\0';
                }
                if(fwrite(chrom, keySize, 1, fp) != 1) return 15;
                if(fwrite(&nextLeaf, sizeof(uint64_t), 1, fp) != 1) return 16;
            } else {
                chrom = strncpy(chrom, cl->chrom[j], keySize);
                if(fwrite(chrom, keySize, 1, fp) != 1) return 15;
                if(fwrite(&j, sizeof(uint32_t), 1, fp) != 1) return 16;
                if(fwrite(&(cl->len[j++]), sizeof(uint32_t), 1, fp) != 1) return 17;
            }
        }
    }

    free(chrom);
    return 0;
}

//returns 0 on success
//Still need to fill in indexOffset
int bwWriteHdr(bigWigFile_t *bw) {
    uint32_t magic = BIGWIG_MAGIC;
    uint16_t two = 4;
    FILE *fp;
    const uint8_t pbuff[58] = {0}; // 58 bytes of nothing
    const void *p = (const void *)&pbuff;
    if(!bw->isWrite) return 1;

    //The header itself, largely just reserving space...
    fp = bw->URL->x.fp;
    if(!fp) return 2;
    if(fseek(fp, 0, SEEK_SET)) return 3;
    if(fwrite(&magic, sizeof(uint32_t), 1, fp) != 1) return 4;
    if(fwrite(&two, sizeof(uint16_t), 1, fp) != 1) return 5;
    if(fwrite(p, sizeof(uint8_t), 58, fp) != 58) return 6;

    //Empty zoom headers
    if(bw->hdr->nLevels) {
        for(two=0; two<bw->hdr->nLevels; two++) {
            if(fwrite(p, sizeof(uint8_t), 24, fp) != 24) return 9;
        }
    }

    //Update summaryOffset and write an empty summary block
    bw->hdr->summaryOffset = ftell(fp);
    if(fwrite(p, sizeof(uint8_t), 40, fp) != 40) return 10;
    if(writeAtPos(&(bw->hdr->summaryOffset), sizeof(uint64_t), 1, 0x2c, fp)) return 11;

    //Write the chromosome list as a stupid freaking tree (because let's TREE ALL THE THINGS!!!)
    bw->hdr->ctOffset = ftell(fp);
    if(writeChromList(fp, bw->cl)) return 7;
    if(writeAtPos(&(bw->hdr->ctOffset), sizeof(uint64_t), 1, 0x8, fp)) return 8;

    //Update the dataOffset
    bw->hdr->dataOffset = ftell(fp);
    if(writeAtPos(&bw->hdr->dataOffset, sizeof(uint64_t), 1, 0x10, fp)) return 12;

    //Save space for the number of blocks
    if(fwrite(p, sizeof(uint8_t), 8, fp) != 8) return 13;

    return 0;
}

static int insertIndexNode(bigWigFile_t *fp, bwRTreeNode_t *leaf) {
    bwLL *l = malloc(sizeof(bwLL));
    if(!l) return 1;
    l->node = leaf;
    l->next = NULL;

    if(!fp->writeBuffer->firstIndexNode) {
        fp->writeBuffer->firstIndexNode = l;
    } else {
        fp->writeBuffer->currentIndexNode->next = l;
    }
    fp->writeBuffer->currentIndexNode = l;
    return 0;
}

//0 on success
static int appendIndexNodeEntry(bigWigFile_t *fp, uint32_t tid0, uint32_t tid1, uint32_t start, uint32_t end, uint64_t offset, uint64_t size) {
    bwLL *n = fp->writeBuffer->currentIndexNode;
    if(!n) return 1;
    if(n->node->nChildren >= fp->writeBuffer->blockSize) return 2;

    n->node->chrIdxStart[n->node->nChildren] = tid0;
    n->node->baseStart[n->node->nChildren] = start;
    n->node->chrIdxEnd[n->node->nChildren] = tid1;
    n->node->baseEnd[n->node->nChildren] = end;
    n->node->dataOffset[n->node->nChildren] = offset;
    n->node->x.size[n->node->nChildren] = size;
    n->node->nChildren++;
    return 0;
}

//Returns 0 on success
static int addIndexEntry(bigWigFile_t *fp, uint32_t tid0, uint32_t tid1, uint32_t start, uint32_t end, uint64_t offset, uint64_t size) {
    bwRTreeNode_t *node;

    if(appendIndexNodeEntry(fp, tid0, tid1, start, end, offset, size)) {
        //The last index node is full, we need to add a new one
        node = calloc(1, sizeof(bwRTreeNode_t));
        if(!node) return 1;

        //Allocate and set the fields
        node->isLeaf = 1;
        node->nChildren = 1;
        node->chrIdxStart = malloc(sizeof(uint32_t)*fp->writeBuffer->blockSize);
        if(!node->chrIdxStart) goto error;
        node->baseStart = malloc(sizeof(uint32_t)*fp->writeBuffer->blockSize);
        if(!node->baseStart) goto error;
        node->chrIdxEnd = malloc(sizeof(uint32_t)*fp->writeBuffer->blockSize);
        if(!node->chrIdxEnd) goto error;
        node->baseEnd = malloc(sizeof(uint32_t)*fp->writeBuffer->blockSize);
        if(!node->baseEnd) goto error;
        node->dataOffset = malloc(sizeof(uint64_t)*fp->writeBuffer->blockSize);
        if(!node->dataOffset) goto error;
        node->x.size = malloc(sizeof(uint64_t)*fp->writeBuffer->blockSize);
        if(!node->x.size) goto error;

        node->chrIdxStart[0] = tid0;
        node->baseStart[0] = start;
        node->chrIdxEnd[0] = tid1;
        node->baseEnd[0] = end;
        node->dataOffset[0] = offset;
        node->x.size[0] = size;

        if(insertIndexNode(fp, node)) goto error;
    }

    return 0;

error:
    if(node->chrIdxStart) free(node->chrIdxStart);
    if(node->baseStart) free(node->baseStart);
    if(node->chrIdxEnd) free(node->chrIdxEnd);
    if(node->baseEnd) free(node->baseEnd);
    if(node->dataOffset) free(node->dataOffset);
    if(node->x.size) free(node->x.size);
    return 2;
}

/*
 * TODO:
 *     The buffer size and compression sz need to be determined elsewhere (and p and compressP filled in!)
 */
static int flushBuffer(bigWigFile_t *fp) {
    bwWriteBuffer_t *wb = fp->writeBuffer;
    uLongf sz = wb->compressPsz;
    uint16_t nItems;
    if(!fp->writeBuffer->l) return 0;
    if(!wb->ltype) return 0;

    //Fill in the header
    if(!memcpy(wb->p, &(wb->tid), sizeof(uint32_t))) return 1;
    if(!memcpy(wb->p+4, &(wb->start), sizeof(uint32_t))) return 2;
    if(!memcpy(wb->p+8, &(wb->end), sizeof(uint32_t))) return 3;
    if(!memcpy(wb->p+12, &(wb->step), sizeof(uint32_t))) return 4;
    if(!memcpy(wb->p+16, &(wb->span), sizeof(uint32_t))) return 5;
    if(!memcpy(wb->p+20, &(wb->ltype), sizeof(uint8_t))) return 6;
    //1 byte padding
    //Determine the number of items
    switch(wb->ltype) {
    case 1:
        nItems = (wb->l-24)/12;
        break;
    case 2:
        nItems = (wb->l-24)/8;
        break;
    case 3:
        nItems = (wb->l-24)/4;
        break;
    default:
        return 7;
    }
    if(!memcpy(wb->p+22, &nItems, sizeof(uint16_t))) return 8;

    if(sz) {
        //compress
        if(compress(wb->compressP, &sz, wb->p, wb->l) != Z_OK) return 9;

        //write the data to disk
        if(fwrite(wb->compressP, sizeof(uint8_t), sz, fp->URL->x.fp) != sz) return 10;
    } else {
        sz = wb->l;
        if(fwrite(wb->p, sizeof(uint8_t), wb->l, fp->URL->x.fp) != wb->l) return 10;
    }

    //Add an entry into the index
    if(addIndexEntry(fp, wb->tid, wb->tid, wb->start, wb->end, bwTell(fp)-sz, sz)) return 11;

    wb->nBlocks++;
    wb->l = 24;
    return 0;
}

static void updateStats(bigWigFile_t *fp, uint32_t span, float val) {
    if(val < fp->hdr->minVal) fp->hdr->minVal = val;
    else if(val > fp->hdr->maxVal) fp->hdr->maxVal = val;
    fp->hdr->nBasesCovered += span;
    fp->hdr->sumData += span*val;
    fp->hdr->sumSquared += span*pow(val,2);

    fp->writeBuffer->nEntries++;
    fp->writeBuffer->runningWidthSum += span;
}

//12 bytes per entry
int bwAddIntervals(bigWigFile_t *fp, const char* const* chrom, const uint32_t *start, const uint32_t *end, const float *values, uint32_t n) {
    uint32_t tid = 0, i;
    const char *lastChrom = NULL;
    bwWriteBuffer_t *wb = fp->writeBuffer;
    if(!n) return 0; //Not an error per se
    if(!fp->isWrite) return 1;
    if(!wb) return 2;

    //Flush if needed
    if(wb->ltype != 1) if(flushBuffer(fp)) return 3;
    if(wb->l+36 > fp->hdr->bufSize) if(flushBuffer(fp)) return 4;
    lastChrom = chrom[0];
    tid = bwGetTid(fp, chrom[0]);
    if(tid == (uint32_t) -1) return 5;
    if(tid != wb->tid) {
        if(flushBuffer(fp)) return 6;
        wb->tid = tid;
        wb->start = start[0];
        wb->end = end[0];
    }

    //Ensure that everything is set correctly
    wb->ltype = 1;
    if(wb->l <= 24) {
        wb->start = start[0];
        wb->span = 0;
        wb->step = 0;
    }
    if(!memcpy(wb->p+wb->l, start, sizeof(uint32_t))) return 7;
    if(!memcpy(wb->p+wb->l+4, end, sizeof(uint32_t))) return 8;
    if(!memcpy(wb->p+wb->l+8, values, sizeof(float))) return 9;
    updateStats(fp, end[0]-start[0], values[0]);
    wb->l += 12;

    for(i=1; i<n; i++) {
        if(strcmp(chrom[i],lastChrom) != 0) {
            wb->end = end[i-1];
            flushBuffer(fp);
            lastChrom = chrom[i];
            tid = bwGetTid(fp, chrom[i]);
            if(tid == (uint32_t) -1) return 10;
            wb->tid = tid;
            wb->start = start[i];
        }
        if(wb->l+12 > fp->hdr->bufSize) { //12 bytes/entry
            wb->end = end[i-1];
            flushBuffer(fp);
            wb->start = start[i];
        }
        if(!memcpy(wb->p+wb->l, &(start[i]), sizeof(uint32_t))) return 11;
        if(!memcpy(wb->p+wb->l+4, &(end[i]), sizeof(uint32_t))) return 12;
        if(!memcpy(wb->p+wb->l+8, &(values[i]), sizeof(float))) return 13;
        updateStats(fp, end[i]-start[i], values[i]);
        wb->l += 12;
    }
    wb->end = end[i-1];

    return 0;
}

int bwAppendIntervals(bigWigFile_t *fp, const uint32_t *start, const uint32_t *end, const float *values, uint32_t n) {
    uint32_t i;
    bwWriteBuffer_t *wb = fp->writeBuffer;
    if(!n) return 0;
    if(!fp->isWrite) return 1;
    if(!wb) return 2;
    if(wb->ltype != 1) return 3;

    for(i=0; i<n; i++) {
        if(wb->l+12 > fp->hdr->bufSize) {
            if(i>0) { //otherwise it's already set
                wb->end = end[i-1];
            }
            flushBuffer(fp);
            wb->start = start[i];
        }
        if(!memcpy(wb->p+wb->l, &(start[i]), sizeof(uint32_t))) return 4;
        if(!memcpy(wb->p+wb->l+4, &(end[i]), sizeof(uint32_t))) return 5;
        if(!memcpy(wb->p+wb->l+8, &(values[i]), sizeof(float))) return 6;
        updateStats(fp, end[i]-start[i], values[i]);
        wb->l += 12;
    }
    wb->end = end[i-1];

    return 0;
}

//8 bytes per entry
int bwAddIntervalSpans(bigWigFile_t *fp, const char *chrom, const uint32_t *start, uint32_t span, const float *values, uint32_t n) {
    uint32_t i, tid;
    bwWriteBuffer_t *wb = fp->writeBuffer;
    if(!n) return 0;
    if(!fp->isWrite) return 1;
    if(!wb) return 2;
    if(wb->ltype != 2) if(flushBuffer(fp)) return 3;
    if(flushBuffer(fp)) return 4;

    tid = bwGetTid(fp, chrom);
    if(tid == (uint32_t) -1) return 5;
    wb->tid = tid;
    wb->start = start[0];
    wb->step = 0;
    wb->span = span;
    wb->ltype = 2;

    for(i=0; i<n; i++) {
        if(wb->l + 8 >= fp->hdr->bufSize) { //8 bytes/entry
            if(i) wb->end = start[i-1]+span;
            flushBuffer(fp);
            wb->start = start[i];
        }
        if(!memcpy(wb->p+wb->l, &(start[i]), sizeof(uint32_t))) return 5;
        if(!memcpy(wb->p+wb->l+4, &(values[i]), sizeof(float))) return 6;
        updateStats(fp, span, values[i]);
        wb->l += 8;
    }
    wb->end = start[n-1] + span;

    return 0;
}

int bwAppendIntervalSpans(bigWigFile_t *fp, const uint32_t *start, const float *values, uint32_t n) {
    uint32_t i;
    bwWriteBuffer_t *wb = fp->writeBuffer;
    if(!n) return 0;
    if(!fp->isWrite) return 1;
    if(!wb) return 2;
    if(wb->ltype != 2) return 3;

    for(i=0; i<n; i++) {
        if(wb->l + 8 >= fp->hdr->bufSize) {
            if(i) wb->end = start[i-1]+wb->span;
            flushBuffer(fp);
            wb->start = start[i];
        }
        if(!memcpy(wb->p+wb->l, &(start[i]), sizeof(uint32_t))) return 4;
        if(!memcpy(wb->p+wb->l+4, &(values[i]), sizeof(float))) return 5;
        updateStats(fp, wb->span, values[i]);
        wb->l += 8;
    }
    wb->end = start[n-1] + wb->span;

    return 0;
}

//4 bytes per entry
int bwAddIntervalSpanSteps(bigWigFile_t *fp, const char *chrom, uint32_t start, uint32_t span, uint32_t step, const float *values, uint32_t n) {
    uint32_t i, tid;
    bwWriteBuffer_t *wb = fp->writeBuffer;
    if(!n) return 0;
    if(!fp->isWrite) return 1;
    if(!wb) return 2;
    if(wb->ltype != 3) flushBuffer(fp);
    if(flushBuffer(fp)) return 3;

    tid = bwGetTid(fp, chrom);
    if(tid == (uint32_t) -1) return 4;
    wb->tid = tid;
    wb->start = start;
    wb->step = step;
    wb->span = span;
    wb->ltype = 3;

    for(i=0; i<n; i++) {
        if(wb->l + 4 >= fp->hdr->bufSize) {
            wb->end = wb->start + ((wb->l-24)>>2) * step;
            flushBuffer(fp);
            wb->start = wb->end;
        }
        if(!memcpy(wb->p+wb->l, &(values[i]), sizeof(float))) return 5;
        updateStats(fp, wb->span, values[i]);
        wb->l += 4;
    }
    wb->end = wb->start + (wb->l>>2) * step;

    return 0;
}

int bwAppendIntervalSpanSteps(bigWigFile_t *fp, const float *values, uint32_t n) {
    uint32_t i;
    bwWriteBuffer_t *wb = fp->writeBuffer;
    if(!n) return 0;
    if(!fp->isWrite) return 1;
    if(!wb) return 2;
    if(wb->ltype != 3) return 3;

    for(i=0; i<n; i++) {
        if(wb->l + 4 >= fp->hdr->bufSize) {
            wb->end = wb->start + ((wb->l-24)>>2) * wb->step;
            flushBuffer(fp);
            wb->start = wb->end;
        }
        if(!memcpy(wb->p+wb->l, &(values[i]), sizeof(float))) return 4;
        updateStats(fp, wb->span, values[i]);
        wb->l += 4;
    }
    wb->end = wb->start + (wb->l>>2) * wb->step;

    return 0;
}

//0 on success
int writeSummary(bigWigFile_t *fp) {
    if(writeAtPos(&(fp->hdr->nBasesCovered), sizeof(uint64_t), 1, fp->hdr->summaryOffset, fp->URL->x.fp)) return 1;
    if(writeAtPos(&(fp->hdr->minVal), sizeof(double), 1, fp->hdr->summaryOffset+8, fp->URL->x.fp)) return 2;
    if(writeAtPos(&(fp->hdr->maxVal), sizeof(double), 1, fp->hdr->summaryOffset+16, fp->URL->x.fp)) return 3;
    if(writeAtPos(&(fp->hdr->sumData), sizeof(double), 1, fp->hdr->summaryOffset+24, fp->URL->x.fp)) return 4;
    if(writeAtPos(&(fp->hdr->sumSquared), sizeof(double), 1, fp->hdr->summaryOffset+32, fp->URL->x.fp)) return 5;
    return 0;
}

static bwRTreeNode_t *makeEmptyNode(uint32_t blockSize) {
    bwRTreeNode_t *n = calloc(1, sizeof(bwRTreeNode_t));
    if(!n) return NULL;

    n->chrIdxStart = malloc(blockSize*sizeof(uint32_t));
    if(!n->chrIdxStart) goto error;
    n->baseStart = malloc(blockSize*sizeof(uint32_t));
    if(!n->baseStart) goto error;
    n->chrIdxEnd = malloc(blockSize*sizeof(uint32_t));
    if(!n->chrIdxEnd) goto error;
    n->baseEnd = malloc(blockSize*sizeof(uint32_t));
    if(!n->baseEnd) goto error;
    n->dataOffset = calloc(blockSize,sizeof(uint64_t)); //This MUST be 0 for node writing!
    if(!n->dataOffset) goto error;
    n->x.child = malloc(blockSize*sizeof(uint64_t));
    if(!n->x.child) goto error;

    return n;

error:
    if(n->chrIdxStart) free(n->chrIdxStart);
    if(n->baseStart) free(n->baseStart);
    if(n->chrIdxEnd) free(n->chrIdxEnd);
    if(n->baseEnd) free(n->baseEnd);
    if(n->dataOffset) free(n->dataOffset);
    if(n->x.child) free(n->x.child);
    free(n);
    return NULL;
}

//Returns 0 on success. This doesn't attempt to clean up!
static bwRTreeNode_t *addLeaves(bwLL **ll, uint64_t *sz, uint64_t toProcess, uint32_t blockSize) {
    uint32_t i;
    uint64_t foo;
    bwRTreeNode_t *n = makeEmptyNode(blockSize);
    if(!n) return NULL;

    if(toProcess <= blockSize) {
        for(i=0; i<toProcess; i++) {
            n->chrIdxStart[i] = (*ll)->node->chrIdxStart[0];
            n->baseStart[i] = (*ll)->node->baseStart[0];
            n->chrIdxEnd[i] = (*ll)->node->chrIdxEnd[(*ll)->node->nChildren-1];
            n->baseEnd[i] = (*ll)->node->baseEnd[(*ll)->node->nChildren-1];
            n->x.child[i] = (*ll)->node;
            *sz += 4 + 32*(*ll)->node->nChildren;
            *ll = (*ll)->next;
            n->nChildren++;
        }
    } else {
        for(i=0; i<blockSize; i++) {
            foo = ceil(((double) toProcess)/((double) blockSize-i));
            if(!ll) break;
            n->x.child[i] = addLeaves(ll, sz, foo, blockSize);
            if(!n->x.child[i]) goto error;
            n->chrIdxStart[i] = n->x.child[i]->chrIdxStart[0];
            n->baseStart[i] = n->x.child[i]->baseStart[0];
            n->chrIdxEnd[i] = n->x.child[i]->chrIdxEnd[n->x.child[i]->nChildren-1];
            n->baseEnd[i] = n->x.child[i]->baseEnd[n->x.child[i]->nChildren-1];
            n->nChildren++;
            toProcess -= foo;
        }
    }

    *sz += 4 + 24*n->nChildren;
    return n;

error:
    bwDestroyIndexNode(n);
    return NULL;
}

//Returns 1 on error
int writeIndexTreeNode(FILE *fp, bwRTreeNode_t *n, uint8_t *wrote, int level) {
    uint8_t one = 0;
    uint32_t i, j, vector[6] = {0, 0, 0, 0, 0, 0}; //The last 8 bytes are left as 0

    if(n->isLeaf) return 0;

    for(i=0; i<n->nChildren; i++) {
        if(n->dataOffset[i]) { //traverse into child
            if(n->isLeaf) return 0; //Only write leaves once!
            if(writeIndexTreeNode(fp, n->x.child[i], wrote, level+1)) return 1;
        } else {
            n->dataOffset[i] = ftell(fp);
            if(fwrite(&(n->x.child[i]->isLeaf), sizeof(uint8_t), 1, fp) != 1) return 1;
            if(fwrite(&one, sizeof(uint8_t), 1, fp) != 1) return 1; //one byte of padding
            if(fwrite(&(n->x.child[i]->nChildren), sizeof(uint16_t), 1, fp) != 1) return 1;
            for(j=0; j<n->x.child[i]->nChildren; j++) {
                vector[0] = n->x.child[i]->chrIdxStart[j];
                vector[1] = n->x.child[i]->baseStart[j];
                vector[2] = n->x.child[i]->chrIdxEnd[j];
                vector[3] = n->x.child[i]->baseEnd[j];
                if(n->x.child[i]->isLeaf) {
                    //Include the offset and size
                    if(fwrite(vector, sizeof(uint32_t), 4, fp) != 4) return 1;
                    if(fwrite(&(n->x.child[i]->dataOffset[j]), sizeof(uint64_t), 1, fp) != 1) return 1;
                    if(fwrite(&(n->x.child[i]->x.size[j]), sizeof(uint64_t), 1, fp) != 1) return 1;
                } else {
                    if(fwrite(vector, sizeof(uint32_t), 6, fp) != 6) return 1;
                }
            }
            *wrote = 1;
        }
    }

    return 0;
}

//returns 1 on success
int writeIndexOffsets(FILE *fp, bwRTreeNode_t *n, uint64_t offset) {
    uint32_t i;

    if(n->isLeaf) return 0;
    for(i=0; i<n->nChildren; i++) {
        if(writeIndexOffsets(fp, n->x.child[i], n->dataOffset[i])) return 1;
        if(writeAtPos(&(n->dataOffset[i]), sizeof(uint64_t), 1, offset+20+24*i, fp)) return 2;
    }
    return 0;
}

//Returns 0 on success
int writeIndexTree(bigWigFile_t *fp) {
    uint64_t offset;
    uint8_t wrote = 0;
    int rv;

    while((rv = writeIndexTreeNode(fp->URL->x.fp, fp->idx->root, &wrote, 0)) == 0) {
        if(!wrote) break;
        wrote = 0;
    }

    if(rv || wrote) return 1;

    //Save the file position
    offset = bwTell(fp);

    //Write the offsets
    if(writeIndexOffsets(fp->URL->x.fp, fp->idx->root, fp->idx->rootOffset)) return 2;

    //Move the file pointer back to the end
    bwSetPos(fp, offset);

    return 0;
}

//Returns 0 on success. The original state SHOULD be preserved on error
int writeIndex(bigWigFile_t *fp) {
    uint32_t four = IDX_MAGIC;
    uint64_t idxSize = 0, foo;
    uint8_t one = 0;
    uint32_t i, vector[6] = {0, 0, 0, 0, 0, 0}; //The last 8 bytes are left as 0
    bwLL *ll = fp->writeBuffer->firstIndexNode, *p;
    bwRTreeNode_t *root = NULL;

    if(!fp->writeBuffer->nBlocks) return 0;
    fp->idx = malloc(sizeof(bwRTree_t));
    if(!fp->idx) return 2;
    fp->idx->root = root;

    //Update the file header to indicate the proper index position
    foo = bwTell(fp);
    if(writeAtPos(&foo, sizeof(uint64_t), 1, 0x18, fp->URL->x.fp)) return 3;

    //Make the tree
    if(ll == fp->writeBuffer->currentIndexNode) {
        root = ll->node;
        idxSize = 4 + 24*root->nChildren;
    } else {
        root = addLeaves(&ll, &idxSize, ceil(((double)fp->writeBuffer->nBlocks)/fp->writeBuffer->blockSize), fp->writeBuffer->blockSize);
    }
    if(!root) return 4;
    fp->idx->root = root;
    
    ll = fp->writeBuffer->firstIndexNode;
    while(ll) {
        p = ll->next;
        free(ll);
        ll=p;
    }

    //write the header
    if(fwrite(&four, sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 5;
    if(fwrite(&(fp->writeBuffer->blockSize), sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 6;
    if(fwrite(&(fp->writeBuffer->nBlocks), sizeof(uint64_t), 1, fp->URL->x.fp) != 1) return 7;
    if(fwrite(&(root->chrIdxStart[0]), sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 8;
    if(fwrite(&(root->baseStart[0]), sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 9;
    if(fwrite(&(root->chrIdxEnd[root->nChildren-1]), sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 10;
    if(fwrite(&(root->baseEnd[root->nChildren-1]), sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 11;
    if(fwrite(&idxSize, sizeof(uint64_t), 1, fp->URL->x.fp) != 1) return 12;
    four = 1;
    if(fwrite(&four, sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 13;
    four = 0;
    if(fwrite(&four, sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 14; //padding
    fp->idx->rootOffset = bwTell(fp);

    //Write the root node, since writeIndexTree writes the children and fills in the offset
    if(fwrite(&(root->isLeaf), sizeof(uint8_t), 1, fp->URL->x.fp) != 1) return 16;
    if(fwrite(&one, sizeof(uint8_t), 1, fp->URL->x.fp) != 1) return 17; //one byte of padding
    if(fwrite(&(root->nChildren), sizeof(uint16_t), 1, fp->URL->x.fp) != 1) return 18;
    for(i=0; i<root->nChildren; i++) {
        vector[0] = root->chrIdxStart[i];
        vector[1] = root->baseStart[i];
        vector[2] = root->chrIdxEnd[i];
        vector[3] = root->baseEnd[i];
        if(root->isLeaf) {
            //Include the offset and size
            if(fwrite(vector, sizeof(uint32_t), 4, fp->URL->x.fp) != 4) return 19;
            if(fwrite(&(root->dataOffset[i]), sizeof(uint64_t), 1, fp->URL->x.fp) != 1) return 20;
            if(fwrite(&(root->x.size[i]), sizeof(uint64_t), 1, fp->URL->x.fp) != 1) return 21;
        } else {
            root->dataOffset[i] = 0; //FIXME: Something upstream is setting this to impossible values (e.g., 0x21?!?!?)
            if(fwrite(vector, sizeof(uint32_t), 6, fp->URL->x.fp) != 6) return 22;
        }
    }

    //Write each level
    if(writeIndexTree(fp)) return 23;

    return 0;
}

//The first zoom level has a resolution of 4x mean entry size
//This may or may not produce the requested number of zoom levels
int makeZoomLevels(bigWigFile_t *fp) {
    uint32_t meanBinSize, i;
    uint32_t multiplier = 4, zoom = 10, maxZoom = 0;
    uint16_t nLevels = 0;

    meanBinSize = ((double) fp->writeBuffer->runningWidthSum)/(fp->writeBuffer->nEntries);
    //In reality, one level is skipped
    meanBinSize *= 4;
    //N.B., we must ALWAYS check that the zoom doesn't overflow a uint32_t!
    if(((uint32_t)-1)>>2 < meanBinSize) return 0; //No zoom levels!
    if(meanBinSize*4 > zoom) zoom = multiplier*meanBinSize;

    fp->hdr->zoomHdrs = calloc(1, sizeof(bwZoomHdr_t));
    if(!fp->hdr->zoomHdrs) return 1;
    fp->hdr->zoomHdrs->level = malloc(fp->hdr->nLevels * sizeof(uint32_t));
    fp->hdr->zoomHdrs->dataOffset = calloc(fp->hdr->nLevels, sizeof(uint64_t));
    fp->hdr->zoomHdrs->indexOffset = calloc(fp->hdr->nLevels, sizeof(uint64_t));
    fp->hdr->zoomHdrs->idx = calloc(fp->hdr->nLevels, sizeof(bwRTree_t*));
    if(!fp->hdr->zoomHdrs->level) return 2;
    if(!fp->hdr->zoomHdrs->dataOffset) return 3;
    if(!fp->hdr->zoomHdrs->indexOffset) return 4;
    if(!fp->hdr->zoomHdrs->idx) return 5;

    //There's no point in having a zoom level larger than the largest chromosome
    //This will none the less allow at least one zoom level, which is generally needed for IGV et al.
    for(i=0; i<fp->cl->nKeys; i++) {
        if(fp->cl->len[i] > maxZoom) maxZoom = fp->cl->len[i];
    }
    if(zoom > maxZoom) zoom = maxZoom;

    for(i=0; i<fp->hdr->nLevels; i++) {
        if(zoom > maxZoom) break; //prevent absurdly large zoom levels
        fp->hdr->zoomHdrs->level[i] = zoom;
        nLevels++;
        if(((uint32_t)-1)/multiplier < zoom) break;
        zoom *= multiplier;
    }
    fp->hdr->nLevels = nLevels;

    fp->writeBuffer->firstZoomBuffer = calloc(nLevels,sizeof(bwZoomBuffer_t*));
    if(!fp->writeBuffer->firstZoomBuffer) goto error;
    fp->writeBuffer->lastZoomBuffer = calloc(nLevels,sizeof(bwZoomBuffer_t*));
    if(!fp->writeBuffer->lastZoomBuffer) goto error;
    fp->writeBuffer->nNodes = calloc(nLevels, sizeof(uint64_t));

    for(i=0; i<fp->hdr->nLevels; i++) {
        fp->writeBuffer->firstZoomBuffer[i] = calloc(1, sizeof(bwZoomBuffer_t));
        if(!fp->writeBuffer->firstZoomBuffer[i]) goto error;
        fp->writeBuffer->firstZoomBuffer[i]->p = calloc(fp->hdr->bufSize/32, 32);
        if(!fp->writeBuffer->firstZoomBuffer[i]->p) goto error;
        fp->writeBuffer->firstZoomBuffer[i]->m = fp->hdr->bufSize;
        ((uint32_t*)fp->writeBuffer->firstZoomBuffer[i]->p)[0] = 0;
        ((uint32_t*)fp->writeBuffer->firstZoomBuffer[i]->p)[1] = 0;
        ((uint32_t*)fp->writeBuffer->firstZoomBuffer[i]->p)[2] = fp->hdr->zoomHdrs->level[i];
        if(fp->hdr->zoomHdrs->level[i] > fp->cl->len[0]) ((uint32_t*)fp->writeBuffer->firstZoomBuffer[i]->p)[2] = fp->cl->len[0];
        fp->writeBuffer->lastZoomBuffer[i] =  fp->writeBuffer->firstZoomBuffer[i];
    }

    return 0;

error:
    if(fp->writeBuffer->firstZoomBuffer) {
        for(i=0; i<fp->hdr->nLevels; i++) {
            if(fp->writeBuffer->firstZoomBuffer[i]) {
                if(fp->writeBuffer->firstZoomBuffer[i]->p) free(fp->writeBuffer->firstZoomBuffer[i]->p);
                free(fp->writeBuffer->firstZoomBuffer[i]);
            }
        }
        free(fp->writeBuffer->firstZoomBuffer);
    }
    if(fp->writeBuffer->lastZoomBuffer) free(fp->writeBuffer->lastZoomBuffer);
    if(fp->writeBuffer->nNodes) free(fp->writeBuffer->lastZoomBuffer);
    return 6;
}

//Given an interval start, calculate the next one at a zoom level
void nextPos(bigWigFile_t *fp, uint32_t size, uint32_t *pos, uint32_t desiredTid) {
    uint32_t *tid = pos;
    uint32_t *start = pos+1;
    uint32_t *end = pos+2;
    *start += size;
    if(*start >= fp->cl->len[*tid]) {
        (*start) = 0;
        (*tid)++;
    }

    //prevent needless iteration when changing chromosomes
    if(*tid < desiredTid) {
        *tid = desiredTid;
        *start = 0;
    }

    (*end) = *start+size;
    if(*end > fp->cl->len[*tid]) (*end) = fp->cl->len[*tid];
}

//Return the number of bases two intervals overlap
uint32_t overlapsInterval(uint32_t tid0, uint32_t start0, uint32_t end0, uint32_t tid1, uint32_t start1, uint32_t end1) {
    if(tid0 != tid1) return 0;
    if(end0 <= start1) return 0;
    if(end1 <= start0) return 0;
    if(end0 <= end1) {
        if(start1 > start0) return end0-start1;
        return end0-start0;
    } else {
        if(start1 > start0) return end1-start1;
        return end1-start0;
    }
}

//Returns the number of bases of the interval written
uint32_t updateInterval(bigWigFile_t *fp, bwZoomBuffer_t *buffer, double *sum, double *sumsq, uint32_t size, uint32_t tid, uint32_t start, uint32_t end, float value) {
    uint32_t *p2 = (uint32_t*) buffer->p;
    float *fp2 = (float*) p2;
    uint32_t rv = 0, offset = 0;
    if(!buffer) return 0;
    if(buffer->l+32 >= buffer->m) return 0;

    //Make sure that we don't overflow a uint32_t by adding some huge value to start
    if(start + size < start) size = ((uint32_t) -1) - start;

    if(buffer->l) {
        offset = buffer->l/32;
    } else {
        p2[0] = tid;
        p2[1] = start;
        if(start+size < end) p2[2] = start+size;
        else p2[2] = end;
    }

    //Do we have any overlap with the previously added interval?
    if(offset) {
        rv = overlapsInterval(p2[8*(offset-1)], p2[8*(offset-1)+1], p2[8*(offset-1)+1] + size, tid, start, end);
        if(rv) {
            p2[8*(offset-1)+2] = start + rv;
            p2[8*(offset-1)+3] += rv;
            if(fp2[8*(offset-1)+4] > value) fp2[8*(offset-1)+4] = value;
            if(fp2[8*(offset-1)+5] < value) fp2[8*(offset-1)+5] = value;
            *sum += rv*value;
            *sumsq += rv*pow(value, 2.0);
            return rv;
        } else {
            fp2[8*(offset-1)+6] = *sum;
            fp2[8*(offset-1)+7] = *sumsq;
            *sum = 0.0;
            *sumsq = 0.0;
        }
    }

    //If we move to a new interval then skip iterating over a bunch of obviously non-overlapping intervals
    if(offset && p2[8*offset+2] == 0) {
        p2[8*offset] = tid;
        p2[8*offset+1] = start;
        if(start+size < end) p2[8*offset+2] = start+size;
        else p2[8*offset+2] = end;
        //nextPos(fp, size, p2+8*offset, tid); //We can actually skip uncovered intervals
    }

    //Add a new entry
    while(!(rv = overlapsInterval(p2[8*offset], p2[8*offset+1], p2[8*offset+1] + size, tid, start, end))) {
        p2[8*offset] = tid;
        p2[8*offset+1] = start;
        if(start+size < end) p2[8*offset+2] = start+size;
        else p2[8*offset+2] = end;
        //nextPos(fp, size, p2+8*offset, tid);
    }
    p2[8*offset+3] = rv;
    fp2[8*offset+4] = value; //min
    fp2[8*offset+5] = value; //max
    *sum += rv * value;
    *sumsq += rv * pow(value,2.0);
    buffer->l += 32;
    return rv;
}

//Returns 0 on success
int addIntervalValue(bigWigFile_t *fp, uint64_t *nEntries, double *sum, double *sumsq, bwZoomBuffer_t *buffer, uint32_t itemsPerSlot, uint32_t zoom, uint32_t tid, uint32_t start, uint32_t end, float value) {
    bwZoomBuffer_t *newBuffer = NULL;
    uint32_t rv;

    while(start < end) {
        rv = updateInterval(fp, buffer, sum, sumsq, zoom, tid, start, end, value);
        if(!rv) {
            //Allocate a new buffer
            newBuffer = calloc(1, sizeof(bwZoomBuffer_t));
            if(!newBuffer) return 1;
            newBuffer->p = calloc(itemsPerSlot, 32);
            if(!newBuffer->p) goto error;
            newBuffer->m = itemsPerSlot*32;
            memcpy(newBuffer->p, buffer->p+buffer->l-32, 4);
            memcpy(newBuffer->p+4, buffer->p+buffer->l-28, 4);
            ((uint32_t*) newBuffer->p)[2] = ((uint32_t*) newBuffer->p)[1] + zoom;
            *sum = *sumsq = 0.0;
            rv = updateInterval(fp, newBuffer, sum, sumsq, zoom, tid, start, end, value);
            if(!rv) goto error;
            buffer->next = newBuffer;
            buffer = buffer->next;
            *nEntries += 1;
        }
        start += rv;
    }

    return 0;

error:
    if(newBuffer) {
        if(newBuffer->m) free(newBuffer->p);
        free(newBuffer);
    }
    return 2;
}

//Get all of the intervals and add them to the appropriate zoomBuffer
int constructZoomLevels(bigWigFile_t *fp) {
    bwOverlapIterator_t *it = NULL;
    double *sum = NULL, *sumsq = NULL;
    uint32_t i, j, k;

    sum = calloc(fp->hdr->nLevels, sizeof(double));
    sumsq = calloc(fp->hdr->nLevels, sizeof(double));
    if(!sum || !sumsq) goto error;

    for(i=0; i<fp->cl->nKeys; i++) {
        it = bwOverlappingIntervalsIterator(fp, fp->cl->chrom[i], 0, fp->cl->len[i], 100000);
        if(!it) goto error;
	while(it->data != NULL){
	  for(j=0;j<it->intervals->l;j++){
		for(k=0;k<fp->hdr->nLevels;k++){
			if(addIntervalValue(fp, &(fp->writeBuffer->nNodes[k]), sum+k, sumsq+k, fp->writeBuffer->lastZoomBuffer[k], fp->hdr->bufSize/32, fp->hdr->zoomHdrs->level[k], i, it->intervals->start[j], it->intervals->end[j], it->intervals->value[j])) goto error;
			while(fp->writeBuffer->lastZoomBuffer[k]->next) fp->writeBuffer->lastZoomBuffer[k] = fp->writeBuffer->lastZoomBuffer[k]->next;
		}
	  }
	  it = bwIteratorNext(it);
	}
	bwIteratorDestroy(it);

    }

    //Make an index for each zoom level
    for(i=0; i<fp->hdr->nLevels; i++) {
        fp->hdr->zoomHdrs->idx[i] = calloc(1, sizeof(bwRTree_t));
        if(!fp->hdr->zoomHdrs->idx[i]) return 1;
        fp->hdr->zoomHdrs->idx[i]->blockSize = fp->writeBuffer->blockSize;
    }


    free(sum);
    free(sumsq);

    return 0;

error:
    if(it) bwIteratorDestroy(it);
    if(sum) free(sum);
    if(sumsq) free(sumsq);
    return 1;
}

int writeZoomLevels(bigWigFile_t *fp) {
    uint64_t offset1, offset2, idxSize = 0;
    uint32_t i, j, four = 0, last, vector[6] = {0, 0, 0, 0, 0, 0}; //The last 8 bytes are left as 0;
    uint8_t wrote, one = 0;
    uint16_t actualNLevels = 0;
    int rv;
    bwLL *ll, *p;
    bwRTreeNode_t *root;
    bwZoomBuffer_t *zb, *zb2;
    bwWriteBuffer_t *wb = fp->writeBuffer;
    uLongf sz;

    for(i=0; i<fp->hdr->nLevels; i++) {
        if(i) {
            //Is this a duplicate level?
            if(fp->writeBuffer->nNodes[i] == fp->writeBuffer->nNodes[i-1]) break;
        }
        actualNLevels++;

        //reserve a uint32_t for the number of blocks
        fp->hdr->zoomHdrs->dataOffset[i] = bwTell(fp);
        fp->writeBuffer->nBlocks = 0;
        fp->writeBuffer->l = 24;
        if(fwrite(&four, sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 1;
        zb = fp->writeBuffer->firstZoomBuffer[i];
        fp->writeBuffer->firstIndexNode = NULL;
        fp->writeBuffer->currentIndexNode = NULL;
        while(zb) {
            sz = fp->hdr->bufSize;
            if(compress(wb->compressP, &sz, zb->p, zb->l) != Z_OK) return 2;

            //write the data to disk
            if(fwrite(wb->compressP, sizeof(uint8_t), sz, fp->URL->x.fp) != sz) return 3;

            //Add an entry into the index
            last = (zb->l - 32)>>2;
            if(addIndexEntry(fp, ((uint32_t*)zb->p)[0], ((uint32_t*)zb->p)[last], ((uint32_t*)zb->p)[1], ((uint32_t*)zb->p)[last+2], bwTell(fp)-sz, sz)) return 4;

            wb->nBlocks++;
            wb->l = 24;
            zb = zb->next;
        }
        if(writeAtPos(&(wb->nBlocks), sizeof(uint32_t), 1, fp->hdr->zoomHdrs->dataOffset[i], fp->URL->x.fp)) return 5;

        //Make the tree
        ll = fp->writeBuffer->firstIndexNode;
        if(ll == fp->writeBuffer->currentIndexNode) {
            root = ll->node;
            idxSize = 4 + 24*root->nChildren;
        } else {
            root = addLeaves(&ll, &idxSize, ceil(((double)fp->writeBuffer->nBlocks)/fp->writeBuffer->blockSize), fp->writeBuffer->blockSize);
        }
        if(!root) return 4;
        fp->hdr->zoomHdrs->idx[i]->root = root;

        ll = fp->writeBuffer->firstIndexNode;
        while(ll) {
            p = ll->next;
            free(ll);
            ll=p; 
        }


        //write the index
        wrote = 0;
        fp->hdr->zoomHdrs->indexOffset[i] = bwTell(fp);
        four = IDX_MAGIC;
        if(fwrite(&four, sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 1;
        root = fp->hdr->zoomHdrs->idx[i]->root;
        if(fwrite(&(fp->writeBuffer->blockSize), sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 6;
        if(fwrite(&(fp->writeBuffer->nBlocks), sizeof(uint64_t), 1, fp->URL->x.fp) != 1) return 7;
        if(fwrite(&(root->chrIdxStart[0]), sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 8;
        if(fwrite(&(root->baseStart[0]), sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 9;
        if(fwrite(&(root->chrIdxEnd[root->nChildren-1]), sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 10;
        if(fwrite(&(root->baseEnd[root->nChildren-1]), sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 11;
        if(fwrite(&idxSize, sizeof(uint64_t), 1, fp->URL->x.fp) != 1) return 12;
        four = fp->hdr->bufSize/32;
        if(fwrite(&four, sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 13;
        four = 0;
        if(fwrite(&four, sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 14; //padding
        fp->hdr->zoomHdrs->idx[i]->rootOffset = bwTell(fp);

        //Write the root node, since writeIndexTree writes the children and fills in the offset
        offset1 = bwTell(fp);
        if(fwrite(&(root->isLeaf), sizeof(uint8_t), 1, fp->URL->x.fp) != 1) return 16;
        if(fwrite(&one, sizeof(uint8_t), 1, fp->URL->x.fp) != 1) return 17; //one byte of padding
        if(fwrite(&(root->nChildren), sizeof(uint16_t), 1, fp->URL->x.fp) != 1) return 18;
        for(j=0; j<root->nChildren; j++) {
            vector[0] = root->chrIdxStart[j];
            vector[1] = root->baseStart[j];
            vector[2] = root->chrIdxEnd[j];
            vector[3] = root->baseEnd[j];
            if(root->isLeaf) {
                //Include the offset and size
                if(fwrite(vector, sizeof(uint32_t), 4, fp->URL->x.fp) != 4) return 19;
                if(fwrite(&(root->dataOffset[j]), sizeof(uint64_t), 1, fp->URL->x.fp) != 1) return 20;
                if(fwrite(&(root->x.size[j]), sizeof(uint64_t), 1, fp->URL->x.fp) != 1) return 21;
            } else {
                if(fwrite(vector, sizeof(uint32_t), 6, fp->URL->x.fp) != 6) return 22;
            }
        }

        while((rv = writeIndexTreeNode(fp->URL->x.fp, fp->hdr->zoomHdrs->idx[i]->root, &wrote, 0)) == 0) {
            if(!wrote) break;
            wrote = 0;
        }

        if(rv || wrote) return 6;

        //Save the file position
        offset2 = bwTell(fp);

        //Write the offsets
        if(writeIndexOffsets(fp->URL->x.fp, root, offset1)) return 2;

        //Move the file pointer back to the end
        bwSetPos(fp, offset2);


        //Free the linked list
        zb = fp->writeBuffer->firstZoomBuffer[i];
        while(zb) {
            if(zb->p) free(zb->p);
            zb2 = zb->next;
            free(zb);
            zb = zb2;
        }
        fp->writeBuffer->firstZoomBuffer[i] = NULL;
    }

    //Free unused zoom levels
    for(i=actualNLevels; i<fp->hdr->nLevels; i++) {
        zb = fp->writeBuffer->firstZoomBuffer[i];
        while(zb) {
            if(zb->p) free(zb->p);
            zb2 = zb->next;
            free(zb);
            zb = zb2;
        }
        fp->writeBuffer->firstZoomBuffer[i] = NULL;
    }

    //Write the zoom headers to disk
    offset1 = bwTell(fp);
    if(bwSetPos(fp, 0x40)) return 7;
    four = 0;
    for(i=0; i<actualNLevels; i++) {
        if(fwrite(&(fp->hdr->zoomHdrs->level[i]), sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 8;
        if(fwrite(&four, sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 9;
        if(fwrite(&(fp->hdr->zoomHdrs->dataOffset[i]), sizeof(uint64_t), 1, fp->URL->x.fp) != 1) return 10;
        if(fwrite(&(fp->hdr->zoomHdrs->indexOffset[i]), sizeof(uint64_t), 1, fp->URL->x.fp) != 1) return 11;
    }

    //Write the number of levels if needed
    if(bwSetPos(fp, 0x6)) return 12;
    if(fwrite(&actualNLevels, sizeof(uint16_t), 1, fp->URL->x.fp) != 1) return 13;

    if(bwSetPos(fp, offset1)) return 14;

    return 0;
}

//0 on success
int bwFinalize(bigWigFile_t *fp) {
    uint32_t four;
    uint64_t offset;
    if(!fp->isWrite) return 0;

    //Flush the buffer
    if(flushBuffer(fp)) return 1; //Valgrind reports a problem here!

    //Update the data section with the number of blocks written
    if(fp->hdr) {
        if(writeAtPos(&(fp->writeBuffer->nBlocks), sizeof(uint64_t), 1, fp->hdr->dataOffset, fp->URL->x.fp)) return 2;
    } else {
        //The header wasn't written!
        return 1;
    }

    //write the bufferSize
    if(fp->hdr->bufSize) {
        if(writeAtPos(&(fp->hdr->bufSize), sizeof(uint32_t), 1, 0x34, fp->URL->x.fp)) return 2;
    }

    //write the summary information
    if(writeSummary(fp)) return 3;

    //Convert the linked-list to a tree and write to disk
    if(writeIndex(fp)) return 4;

    //Zoom level stuff here?
    if(fp->hdr->nLevels && fp->writeBuffer->nBlocks) {
        offset = bwTell(fp);
        if(makeZoomLevels(fp)) return 5;
        if(constructZoomLevels(fp)) return 6;
        bwSetPos(fp, offset);
        if(writeZoomLevels(fp)) return 7; //This write nLevels as well
    }

    //write magic at the end of the file
    four = BIGWIG_MAGIC;
    if(fwrite(&four, sizeof(uint32_t), 1, fp->URL->x.fp) != 1) return 9;

    return 0;
}

/*
data chunk:
uint64_t number of blocks (2 / 110851)
some blocks

an uncompressed data block (24 byte header)
uint32_t Tid	0-4
uint32_t start	4-8
uint32_t end	8-12
uint32_t step	12-16
uint32_t span	16-20
uint8_t type	20
uint8_t padding
uint16_t nItems	22
nItems of:
    type 1: //12 bytes
        uint32_t start
        uint32_t end
        float value
    type 2: //8 bytes
        uint32_t start
        float value
    type 3: //4 bytes
        float value

data block index header
uint32_t magic
uint32_t blockSize (256 in the example) maximum number of children
uint64_t number of blocks (2 / 110851)
uint32_t startTid
uint32_t startPos
uint32_t endTid
uint32_t endPos
uint64_t index size? (0x1E7 / 0x1AF0401F) index address?
uint32_t itemsPerBlock (1 / 1) 1024 for zoom headers 1024 for zoom headers
uint32_t padding

data block index node non-leaf (4 bytes + 24*nChildren)
uint8_t isLeaf
uint8_t padding
uint16_t nChildren (2, 256)
uint32_t startTid
uint32_t startPos
uint32_t endTid
uint32_t endPos
uint64_t dataOffset (0x1AF05853, 0x1AF07057)

data block index node leaf (4 bytes + 32*nChildren)
uint8_t isLeaf
uint8_t padding
uint16_t nChildren (2)
uint32_t startTid
uint32_t startPos
uint32_t endTid
uint32_t endPos
uint64_t dataOffset (0x198, 0x1CF)
uint64_t dataSize (55, 24)

zoom data block
uint32_t number of blocks (10519766)
some data blocks
*/
