#include "bigWig.h"
#include "bwCommon.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

static uint64_t readChromBlock(bigWigFile_t *bw, chromList_t *cl, uint32_t keySize);

//Return the position in the file
long bwTell(bigWigFile_t *fp) {
    if(fp->URL->type == BWG_FILE) return ftell(fp->URL->x.fp);
    return (long) (fp->URL->filePos + fp->URL->bufPos);
}

//Seek to a given position, always from the beginning of the file
//Return 0 on success and -1 on error
//To do, use the return code of urlSeek() in a more useful way.
int bwSetPos(bigWigFile_t *fp, size_t pos) {
    CURLcode rv = urlSeek(fp->URL, pos);
    if(rv == CURLE_OK) return 0;
    return -1;
}

//returns the number of full members read (nmemb on success, something less on error)
size_t bwRead(void *data, size_t sz, size_t nmemb, bigWigFile_t *fp) {
    size_t i, rv;
    for(i=0; i<nmemb; i++) {
        rv = urlRead(fp->URL, data+i*sz, sz);
        if(rv != sz) return i;
    }
    return nmemb;
}

//Initializes curl and sets global variables
//Returns 0 on success and 1 on error
//This should be called only once and bwCleanup() must be called when finished.
int bwInit(size_t defaultBufSize) {
    //set the buffer size, number of iterations, sleep time between iterations, etc.
    GLOBAL_DEFAULTBUFFERSIZE = defaultBufSize;

    //call curl_global_init()
#ifndef NOCURL
    CURLcode rv;
    rv = curl_global_init(CURL_GLOBAL_ALL);
    if(rv != CURLE_OK) return 1;
#endif
    return 0;
}

//This should be called before quiting, to release memory acquired by curl
void bwCleanup() {
#ifndef NOCURL
    curl_global_cleanup();
#endif
}

static bwZoomHdr_t *bwReadZoomHdrs(bigWigFile_t *bw) {
    if(bw->isWrite) return NULL;
    uint16_t i;
    bwZoomHdr_t *zhdr = malloc(sizeof(bwZoomHdr_t));
    if(!zhdr) return NULL;
    uint32_t *level = malloc(bw->hdr->nLevels * sizeof(uint64_t));
    if(!level) {
        free(zhdr);
        return NULL;
    }
    uint32_t padding = 0;
    uint64_t *dataOffset = malloc(sizeof(uint64_t) * bw->hdr->nLevels);
    if(!dataOffset) {
        free(zhdr);
        free(level);
        return NULL;
    }
    uint64_t *indexOffset = malloc(sizeof(uint64_t) * bw->hdr->nLevels);
    if(!indexOffset) {
        free(zhdr);
        free(level);
        free(dataOffset);
        return NULL;
    }

    for(i=0; i<bw->hdr->nLevels; i++) {
        if(bwRead((void*) &(level[i]), sizeof(uint32_t), 1, bw) != 1) goto error;
        if(bwRead((void*) &padding, sizeof(uint32_t), 1, bw) != 1) goto error;
        if(bwRead((void*) &(dataOffset[i]), sizeof(uint64_t), 1, bw) != 1) goto error;
        if(bwRead((void*) &(indexOffset[i]), sizeof(uint64_t), 1, bw) != 1) goto error;
    }

    zhdr->level = level;
    zhdr->dataOffset = dataOffset;
    zhdr->indexOffset = indexOffset;
    zhdr->idx = calloc(bw->hdr->nLevels, sizeof(bwRTree_t*));
    if(!zhdr->idx) goto error;

    return zhdr;

error:
    for(i=0; i<bw->hdr->nLevels; i++) {
        if(zhdr->idx[i]) bwDestroyIndex(zhdr->idx[i]);
    }
    free(zhdr);
    free(level);
    free(dataOffset);
    free(indexOffset);
    return NULL;
}

static void bwHdrDestroy(bigWigHdr_t *hdr) {
    int i;
    if(hdr->zoomHdrs) {
        free(hdr->zoomHdrs->level);
        free(hdr->zoomHdrs->dataOffset);
        free(hdr->zoomHdrs->indexOffset);
        for(i=0; i<hdr->nLevels; i++) {
            if(hdr->zoomHdrs->idx[i]) bwDestroyIndex(hdr->zoomHdrs->idx[i]);
        }
        free(hdr->zoomHdrs->idx);
        free(hdr->zoomHdrs);
    }
    free(hdr);
}

static void bwHdrRead(bigWigFile_t *bw) {
    uint32_t magic;
    if(bw->isWrite) return;
    bw->hdr = calloc(1, sizeof(bigWigHdr_t));
    if(!bw->hdr) return;

    if(bwRead((void*) &magic, sizeof(uint32_t), 1, bw) != 1) goto error; //0x0
    if(magic != BIGWIG_MAGIC && magic != BIGBED_MAGIC) goto error;

    if(bwRead((void*) &(bw->hdr->version), sizeof(uint16_t), 1, bw) != 1) goto error; //0x4
    if(bwRead((void*) &(bw->hdr->nLevels), sizeof(uint16_t), 1, bw) != 1) goto error; //0x6
    if(bwRead((void*) &(bw->hdr->ctOffset), sizeof(uint64_t), 1, bw) != 1) goto error; //0x8
    if(bwRead((void*) &(bw->hdr->dataOffset), sizeof(uint64_t), 1, bw) != 1) goto error; //0x10
    if(bwRead((void*) &(bw->hdr->indexOffset), sizeof(uint64_t), 1, bw) != 1) goto error; //0x18
    if(bwRead((void*) &(bw->hdr->fieldCount), sizeof(uint16_t), 1, bw) != 1) goto error; //0x20
    if(bwRead((void*) &(bw->hdr->definedFieldCount), sizeof(uint16_t), 1, bw) != 1) goto error; //0x22
    if(bwRead((void*) &(bw->hdr->sqlOffset), sizeof(uint64_t), 1, bw) != 1) goto error; //0x24
    if(bwRead((void*) &(bw->hdr->summaryOffset), sizeof(uint64_t), 1, bw) != 1) goto error; //0x2c
    if(bwRead((void*) &(bw->hdr->bufSize), sizeof(uint32_t), 1, bw) != 1) goto error; //0x34
    if(bwRead((void*) &(bw->hdr->extensionOffset), sizeof(uint64_t), 1, bw) != 1) goto error; //0x38

    //zoom headers
    if(bw->hdr->nLevels) {
        if(!(bw->hdr->zoomHdrs = bwReadZoomHdrs(bw))) goto error;
    }

    //File summary information
    if(bw->hdr->summaryOffset) {
        if(urlSeek(bw->URL, bw->hdr->summaryOffset) != CURLE_OK) goto error;
        if(bwRead((void*) &(bw->hdr->nBasesCovered), sizeof(uint64_t), 1, bw) != 1) goto error;
        if(bwRead((void*) &(bw->hdr->minVal), sizeof(uint64_t), 1, bw) != 1) goto error;
        if(bwRead((void*) &(bw->hdr->maxVal), sizeof(uint64_t), 1, bw) != 1) goto error;
        if(bwRead((void*) &(bw->hdr->sumData), sizeof(uint64_t), 1, bw) != 1) goto error;
        if(bwRead((void*) &(bw->hdr->sumSquared), sizeof(uint64_t), 1, bw) != 1) goto error;
    }

    //In case of uncompressed remote files, let the IO functions know to request larger chunks
    bw->URL->isCompressed = (bw->hdr->bufSize > 0)?1:0;

    return;

error:
    bwHdrDestroy(bw->hdr);
    fprintf(stderr, "[bwHdrRead] There was an error while reading in the header!\n");
    bw->hdr = NULL;
}

static void destroyChromList(chromList_t *cl) {
    uint32_t i;
    if(!cl) return;
    if(cl->nKeys && cl->chrom) {
        for(i=0; i<cl->nKeys; i++) {
            if(cl->chrom[i]) free(cl->chrom[i]);
        }
    }
    if(cl->chrom) free(cl->chrom);
    if(cl->len) free(cl->len);
    free(cl);
}

static uint64_t readChromLeaf(bigWigFile_t *bw, chromList_t *cl, uint32_t valueSize) {
    uint16_t nVals, i;
    uint32_t idx;
    char *chrom = NULL;

    if(bwRead((void*) &nVals, sizeof(uint16_t), 1, bw) != 1) return -1;
    chrom = calloc(valueSize+1, sizeof(char));
    if(!chrom) return -1;

    for(i=0; i<nVals; i++) {
        if(bwRead((void*) chrom, sizeof(char), valueSize, bw) != valueSize) goto error;
        if(bwRead((void*) &idx, sizeof(uint32_t), 1, bw) != 1) goto error;
        if(bwRead((void*) &(cl->len[idx]), sizeof(uint32_t), 1, bw) != 1) goto error;
        cl->chrom[idx] = bwStrdup(chrom);
        if(!(cl->chrom[idx])) goto error;
    }

    free(chrom);
    return nVals;

error:
    free(chrom);
    return -1;
}

static uint64_t readChromNonLeaf(bigWigFile_t *bw, chromList_t *cl, uint32_t keySize) {
    uint64_t offset , rv = 0, previous;
    uint16_t nVals, i;

    if(bwRead((void*) &nVals, sizeof(uint16_t), 1, bw) != 1) return -1;

    previous = bwTell(bw) + keySize;
    for(i=0; i<nVals; i++) {
        if(bwSetPos(bw, previous)) return -1;
        if(bwRead((void*) &offset, sizeof(uint64_t), 1, bw) != 1) return -1;
        if(bwSetPos(bw, offset)) return -1;
        rv += readChromBlock(bw, cl, keySize);
        previous += 8 + keySize;
    }

    return rv;
}

static uint64_t readChromBlock(bigWigFile_t *bw, chromList_t *cl, uint32_t keySize) {
    uint8_t isLeaf, padding;

    if(bwRead((void*) &isLeaf, sizeof(uint8_t), 1, bw) != 1) return -1;
    if(bwRead((void*) &padding, sizeof(uint8_t), 1, bw) != 1) return -1;

    if(isLeaf) {
        return readChromLeaf(bw, cl, keySize);
    } else { //I've never actually observed one of these, which is good since they're pointless
        return readChromNonLeaf(bw, cl, keySize);
    }
}

static chromList_t *bwReadChromList(bigWigFile_t *bw) {
    chromList_t *cl = NULL;
    uint32_t magic, keySize, valueSize, itemsPerBlock;
    uint64_t rv, itemCount;
    if(bw->isWrite) return NULL;
    if(bwSetPos(bw, bw->hdr->ctOffset)) return NULL;

    cl = calloc(1, sizeof(chromList_t));
    if(!cl) return NULL;

    if(bwRead((void*) &magic, sizeof(uint32_t), 1, bw) != 1) goto error;
    if(magic != CIRTREE_MAGIC) goto error;

    if(bwRead((void*) &itemsPerBlock, sizeof(uint32_t), 1, bw) != 1) goto error;
    if(bwRead((void*) &keySize, sizeof(uint32_t), 1, bw) != 1) goto error;
    if(bwRead((void*) &valueSize, sizeof(uint32_t), 1, bw) != 1) goto error;
    if(bwRead((void*) &itemCount, sizeof(uint64_t), 1, bw) != 1) goto error;

    cl->nKeys = itemCount;
    cl->chrom = calloc(itemCount, sizeof(char*));
    cl->len = calloc(itemCount, sizeof(uint32_t));
    if(!cl->chrom) goto error;
    if(!cl->len) goto error;

    if(bwRead((void*) &magic, sizeof(uint32_t), 1, bw) != 1) goto error;
    if(bwRead((void*) &magic, sizeof(uint32_t), 1, bw) != 1) goto error;

    //Read in the blocks
    rv = readChromBlock(bw, cl, keySize);
    if(rv == (uint64_t) -1) goto error;
    if(rv != itemCount) goto error;

    return cl;

error:
    destroyChromList(cl);
    return NULL;
}

//This is here mostly for convenience
static void bwDestroyWriteBuffer(bwWriteBuffer_t *wb) {
    if(wb->p) free(wb->p);
    if(wb->compressP) free(wb->compressP);
    if(wb->firstZoomBuffer) free(wb->firstZoomBuffer);
    if(wb->lastZoomBuffer) free(wb->lastZoomBuffer);
    if(wb->nNodes) free(wb->nNodes);
    free(wb);
}

void bwClose(bigWigFile_t *fp) {
    if(!fp) return;
    if(bwFinalize(fp)) {
        fprintf(stderr, "[bwClose] There was an error while finishing writing a bigWig file! The output is likely truncated.\n");
    }
    if(fp->URL) urlClose(fp->URL);
    if(fp->hdr) bwHdrDestroy(fp->hdr);
    if(fp->cl) destroyChromList(fp->cl);
    if(fp->idx) bwDestroyIndex(fp->idx);
    if(fp->writeBuffer) bwDestroyWriteBuffer(fp->writeBuffer);
    free(fp);
}

int bwIsBigWig(const char *fname, CURLcode (*callBack) (CURL*)) {
    uint32_t magic = 0;
    URL_t *URL = NULL;

    URL = urlOpen(fname, *callBack, NULL);

    if(!URL) return 0;
    if(urlRead(URL, (void*) &magic, sizeof(uint32_t)) != sizeof(uint32_t)) magic = 0;
    urlClose(URL);
    if(magic == BIGWIG_MAGIC) return 1;
    return 0;
}

char *bbGetSQL(bigWigFile_t *fp) {
    char *o = NULL;
    uint64_t len;
    if(!fp->hdr->sqlOffset) return NULL;
    len = fp->hdr->summaryOffset - fp->hdr->sqlOffset; //This includes the NULL terminator
    o = malloc(sizeof(char) * len);
    if(!o) goto error;
    if(bwSetPos(fp, fp->hdr->sqlOffset)) goto error;
    if(bwRead((void*) o, len, 1, fp) != 1) goto error;
    return o;

error:
    if(o) free(o);
    printf("Got an error in bbGetSQL!\n");
    return NULL;
}

int bbIsBigBed(const char *fname, CURLcode (*callBack) (CURL*)) {
    uint32_t magic = 0;
    URL_t *URL = NULL;

    URL = urlOpen(fname, *callBack, NULL);

    if(!URL) return 0;
    if(urlRead(URL, (void*) &magic, sizeof(uint32_t)) != sizeof(uint32_t)) magic = 0;
    urlClose(URL);
    if(magic == BIGBED_MAGIC) return 1;
    return 0;
}

bigWigFile_t *bwOpen(const char *fname, CURLcode (*callBack) (CURL*), const char *mode) {
    bigWigFile_t *bwg = calloc(1, sizeof(bigWigFile_t));
    if(!bwg) {
        fprintf(stderr, "[bwOpen] Couldn't allocate space to create the output object!\n");
        return NULL;
    }
    if((!mode) || (strchr(mode, 'w') == NULL)) {
        bwg->isWrite = 0;
        bwg->URL = urlOpen(fname, *callBack, NULL);
        if(!bwg->URL) {
            fprintf(stderr, "[bwOpen] urlOpen is NULL!\n");
            goto error;
        }

        //Attempt to read in the fixed header
        bwHdrRead(bwg);
        if(!bwg->hdr) {
            fprintf(stderr, "[bwOpen] bwg->hdr is NULL!\n");
            goto error;
        }

        //Read in the chromosome list
        bwg->cl = bwReadChromList(bwg);
        if(!bwg->cl) {
            fprintf(stderr, "[bwOpen] bwg->cl is NULL (%s)!\n", fname);
            goto error;
        }

        //Read in the index
        if(bwg->hdr->indexOffset) {
            bwg->idx = bwReadIndex(bwg, 0);
            if(!bwg->idx) {
                fprintf(stderr, "[bwOpen] bwg->idx is NULL bwg->hdr->dataOffset 0x%"PRIx64"!\n", bwg->hdr->dataOffset);
                goto error;
            }
        }
    } else {
        bwg->isWrite = 1;
        bwg->URL = urlOpen(fname, NULL, "w+");
        if(!bwg->URL) goto error;
        bwg->writeBuffer = calloc(1,sizeof(bwWriteBuffer_t));
        if(!bwg->writeBuffer) goto error;
        bwg->writeBuffer->l = 24;
    }

    return bwg;

error:
    bwClose(bwg);
    return NULL;
}

bigWigFile_t *bbOpen(const char *fname, CURLcode (*callBack) (CURL*)) {
    bigWigFile_t *bb = calloc(1, sizeof(bigWigFile_t));
    if(!bb) {
        fprintf(stderr, "[bbOpen] Couldn't allocate space to create the output object!\n");
        return NULL;
    }

    //Set the type to 1 for bigBed
    bb->type = 1;

    bb->URL = urlOpen(fname, *callBack, NULL);
    if(!bb->URL) goto error;

    //Attempt to read in the fixed header
    bwHdrRead(bb);
    if(!bb->hdr) goto error;

    //Read in the chromosome list
    bb->cl = bwReadChromList(bb);
    if(!bb->cl) goto error;

    //Read in the index
    bb->idx = bwReadIndex(bb, 0);
    if(!bb->idx) goto error;

    return bb;

error:
    bwClose(bb);
    return NULL;
}


//Implementation taken from musl:
//https://git.musl-libc.org/cgit/musl/tree/src/string/strdup.c
//License: https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT
char* bwStrdup(const char *s) {
	size_t l = strlen(s);
	char *d = malloc(l+1);
	if (!d) return NULL;
	return memcpy(d, s, l+1);
}
