#include "bigWig.h"
#include "bwCommon.h"
#include <errno.h>
#include <stdlib.h>
#include <zlib.h>
#include <math.h>
#include <string.h>

//Returns -1 if there are no applicable levels, otherwise an integer indicating the most appropriate level.
//Like Kent's library, this divides the desired bin size by 2 to minimize the effect of blocks overlapping multiple bins
static int32_t determineZoomLevel(const bigWigFile_t *fp, int basesPerBin) {
    int32_t out = -1;
    int64_t diff;
    uint32_t bestDiff = -1;
    uint16_t i;

    basesPerBin/=2;
    for(i=0; i<fp->hdr->nLevels; i++) {
        diff = basesPerBin - (int64_t) fp->hdr->zoomHdrs->level[i];
        if(diff >= 0 && diff < bestDiff) {
            bestDiff = diff;
            out = i;
        }
    }
    return out;
}

/// @cond SKIP
struct val_t {
    uint32_t nBases;
    float min, max, sum, sumsq;
    double scalar;
};

struct vals_t {
    uint32_t n;
    struct val_t **vals;
};
/// @endcond

void destroyVals_t(struct vals_t *v) {
    uint32_t i;
    if(!v) return;
    for(i=0; i<v->n; i++) free(v->vals[i]);
    if(v->vals) free(v->vals);
    free(v);
}

//Determine the base-pair overlap between an interval and a block
double getScalar(uint32_t i_start, uint32_t i_end, uint32_t b_start, uint32_t b_end) {
    double rv = 0.0;
    if(b_start <= i_start) {
        if(b_end > i_start) rv = ((double)(b_end - i_start))/(b_end-b_start);
    } else if(b_start < i_end) {
        if(b_end < i_end) rv = ((double)(b_end - b_start))/(b_end-b_start);
        else rv = ((double)(i_end - b_start))/(b_end-b_start);
    }

    return rv;
}

//Returns NULL on error
static struct vals_t *getVals(bigWigFile_t *fp, bwOverlapBlock_t *o, int i, uint32_t tid, uint32_t start, uint32_t end) {
    void *buf = NULL, *compBuf = NULL;
    uLongf sz = fp->hdr->bufSize;
    int compressed = 0, rv;
    uint32_t *p, vtid, vstart, vend;
    struct vals_t *vals = NULL;
    struct val_t *v = NULL;

    if(sz) {
        compressed = 1;
        buf = malloc(sz); 
    }
    sz = 0; //This is now the size of the compressed buffer

    if(bwSetPos(fp, o->offset[i])) goto error;

    vals = calloc(1,sizeof(struct vals_t));
    if(!vals) goto error;

    v = malloc(sizeof(struct val_t));
    if(!v) goto error;

    if(sz < o->size[i]) compBuf = malloc(o->size[i]);
    if(!compBuf) goto error;

    if(bwRead(compBuf, o->size[i], 1, fp) != 1) goto error;
    if(compressed) {
        sz = fp->hdr->bufSize;
        rv = uncompress(buf, &sz, compBuf, o->size[i]);
        if(rv != Z_OK) goto error;
    } else {
        buf = compBuf;
        sz = o->size[i];
    }

    p = buf;
    while(((uLongf) ((void*)p-buf)) < sz) {
        vtid = p[0];
        vstart = p[1];
        vend = p[2];
        v->nBases = p[3];
        v->min = ((float*) p)[4];
        v->max = ((float*) p)[5];
        v->sum = ((float*) p)[6];
        v->sumsq = ((float*) p)[7];
        v->scalar = getScalar(start, end, vstart, vend);

        if(tid == vtid) {
            if((start <= vstart && end > vstart) || (start < vend && start >= vstart)) {
                vals->vals = realloc(vals->vals, sizeof(struct val_t*)*(vals->n+1));
                if(!vals->vals) goto error;
                vals->vals[vals->n++] = v;
                v = malloc(sizeof(struct val_t));
                if(!v) goto error;
            }
            if(vstart > end) break;
        } else if(vtid > tid) {
            break;
        }
        p+=8;
    }

    free(v);
    free(buf);
    if(compressed) free(compBuf);
    return vals;

error:
    if(buf) free(buf);
    if(compBuf && compressed) free(compBuf);
    if(v) free(v);
    destroyVals_t(vals);
    return NULL;
}

//On error, errno is set to ENOMEM and NaN is returned (though NaN can be returned normally)
static double blockMean(bigWigFile_t *fp, bwOverlapBlock_t *blocks, uint32_t tid, uint32_t start, uint32_t end) {
    uint32_t i, j;
    double output = 0.0, coverage = 0.0;
    struct vals_t *v = NULL;

    if(!blocks->n) return strtod("NaN", NULL);

    //Iterate over the blocks
    for(i=0; i<blocks->n; i++) {
        v = getVals(fp, blocks, i, tid, start, end);
        if(!v) goto error;
        for(j=0; j<v->n; j++) {
            output += v->vals[j]->sum * v->vals[j]->scalar;
            coverage += v->vals[j]->nBases * v->vals[j]->scalar;
        }
        destroyVals_t(v);
    }


    if(!coverage) return strtod("NaN", NULL);

    return output/coverage;

error:
    if(v) free(v);
    errno = ENOMEM;
    return strtod("NaN", NULL);
}

static double intMean(bwOverlappingIntervals_t* ints, uint32_t start, uint32_t end) {
    double sum = 0.0;
    uint32_t nBases = 0, i, start_use, end_use;

    if(!ints->l) return strtod("NaN", NULL);

    for(i=0; i<ints->l; i++) {
        start_use = ints->start[i];
        end_use = ints->end[i];
        if(ints->start[i] < start) start_use = start;
        if(ints->end[i] > end) end_use = end;
        nBases += end_use-start_use;
        sum += (end_use-start_use)*((double) ints->value[i]);
    }

    return sum/nBases;
}

//Does UCSC compensate for partial block/range overlap?
static double blockDev(bigWigFile_t *fp, bwOverlapBlock_t *blocks, uint32_t tid, uint32_t start, uint32_t end) {
    uint32_t i, j;
    double mean = 0.0, ssq = 0.0, coverage = 0.0, diff;
    struct vals_t *v = NULL;

    if(!blocks->n) return strtod("NaN", NULL);

    //Iterate over the blocks
    for(i=0; i<blocks->n; i++) {
        v = getVals(fp, blocks, i, tid, start, end);
        if(!v) goto error;
        for(j=0; j<v->n; j++) {
            coverage += v->vals[j]->nBases * v->vals[j]->scalar;
            mean += v->vals[j]->sum * v->vals[j]->scalar;
            ssq += v->vals[j]->sumsq * v->vals[j]->scalar;
        }
        destroyVals_t(v);
        v = NULL;
    }

    if(coverage<=1.0) return strtod("NaN", NULL);
    diff = ssq-mean*mean/coverage;
    if(coverage > 1.0) diff /= coverage-1;
    if(fabs(diff) > 1e-8) { //Ignore floating point differences
        return sqrt(diff);
    } else {
        return 0.0;
    }

error:
    if(v) destroyVals_t(v);
    errno = ENOMEM;
    return strtod("NaN", NULL);
}

//This uses compensated summation to account for finite precision math
static double intDev(bwOverlappingIntervals_t* ints, uint32_t start, uint32_t end) {
    double v1 = 0.0, mean, rv;
    uint32_t nBases = 0, i, start_use, end_use;

    if(!ints->l) return strtod("NaN", NULL);
    mean = intMean(ints, start, end);

    for(i=0; i<ints->l; i++) {
        start_use = ints->start[i];
        end_use = ints->end[i];
        if(ints->start[i] < start) start_use = start;
        if(ints->end[i] > end) end_use = end;
        nBases += end_use-start_use;
        v1 += (end_use-start_use) * pow(ints->value[i]-mean, 2.0); //running sum of squared difference
    }

    if(nBases>=2) rv = sqrt(v1/(nBases-1));
    else if(nBases==1) rv = sqrt(v1);
    else rv = strtod("NaN", NULL);

    return rv;
}

static double blockMax(bigWigFile_t *fp, bwOverlapBlock_t *blocks, uint32_t tid, uint32_t start, uint32_t end) {
    uint32_t i, j, isNA = 1;
    double o = strtod("NaN", NULL);
    struct vals_t *v = NULL;

    if(!blocks->n) return o;

    //Iterate the blocks
    for(i=0; i<blocks->n; i++) {
        v = getVals(fp, blocks, i, tid, start, end);
        if(!v) goto error;
        for(j=0; j<v->n; j++) {
            if(isNA) {
                o = v->vals[j]->max;
                isNA = 0;
            } else if(v->vals[j]->max > o) {
                o = v->vals[j]->max;
            }
        }
        destroyVals_t(v);
    }

    return o;

error:
    destroyVals_t(v);
    errno = ENOMEM;
    return strtod("NaN", NULL);
}

static double intMax(bwOverlappingIntervals_t* ints) {
    uint32_t i;
    double o;

    if(ints->l < 1) return strtod("NaN", NULL);

    o = ints->value[0];
    for(i=1; i<ints->l; i++) {
        if(ints->value[i] > o) o = ints->value[i];
    }

    return o;
}

static double blockMin(bigWigFile_t *fp, bwOverlapBlock_t *blocks, uint32_t tid, uint32_t start, uint32_t end) {
    uint32_t i, j, isNA = 1;
    double o = strtod("NaN", NULL);
    struct vals_t *v = NULL;

    if(!blocks->n) return o;

    //Iterate the blocks
    for(i=0; i<blocks->n; i++) {
        v = getVals(fp, blocks, i, tid, start, end);
        if(!v) goto error;
        for(j=0; j<v->n; j++) {
            if(isNA) {
                o = v->vals[j]->min;
                isNA = 0;
            } else if(v->vals[j]->min < o) o = v->vals[j]->min;
        }
        destroyVals_t(v);
    }

    return o;

error:
    destroyVals_t(v);
    errno = ENOMEM;
    return strtod("NaN", NULL);
}

static double intMin(bwOverlappingIntervals_t* ints) {
    uint32_t i;
    double o;

    if(ints->l < 1) return strtod("NaN", NULL);

    o = ints->value[0];
    for(i=1; i<ints->l; i++) {
        if(ints->value[i] < o) o = ints->value[i];
    }

    return o;
}

//Does UCSC compensate for only partial block/interval overlap?
static double blockCoverage(bigWigFile_t *fp, bwOverlapBlock_t *blocks, uint32_t tid, uint32_t start, uint32_t end) {
    uint32_t i, j;
    double o = 0.0;
    struct vals_t *v = NULL;

    if(!blocks->n) return strtod("NaN", NULL);

    //Iterate over the blocks
    for(i=0; i<blocks->n; i++) {
        v = getVals(fp, blocks, i, tid, start, end);
        if(!v) goto error;
        for(j=0; j<v->n; j++) {
            o+= v->vals[j]->nBases * v->vals[j]->scalar;
        }
        destroyVals_t(v);
    }

    if(o == 0.0) return strtod("NaN", NULL);
    return o;

error:
    destroyVals_t(v);
    errno = ENOMEM;
    return strtod("NaN", NULL);
}

static double intCoverage(bwOverlappingIntervals_t* ints, uint32_t start, uint32_t end) {
    uint32_t i, start_use, end_use;
    double o = 0.0;

    if(!ints->l) return strtod("NaN", NULL);

    for(i=0; i<ints->l; i++) {
        start_use = ints->start[i];
        end_use = ints->end[i];
        if(start_use < start) start_use = start;
        if(end_use > end) end_use = end;
        o += end_use - start_use;
    }

    return o/(end-start);
}

static double blockSum(bigWigFile_t *fp, bwOverlapBlock_t *blocks, uint32_t tid, uint32_t start, uint32_t end) {
    uint32_t i, j, sizeUse;
    double o = 0.0;
    struct vals_t *v = NULL;

    if(!blocks->n) return strtod("NaN", NULL);

    //Iterate over the blocks
    for(i=0; i<blocks->n; i++) {
        v = getVals(fp, blocks, i, tid, start, end);
        if(!v) goto error;
        for(j=0; j<v->n; j++) {
            //Multiply the block average by min(bases covered, block overlap with interval)
            sizeUse = v->vals[j]->scalar;
            if(sizeUse > v->vals[j]->nBases) sizeUse = v->vals[j]->nBases;
            o+= (v->vals[j]->sum * sizeUse) / v->vals[j]->nBases;
        }
        destroyVals_t(v);
    }

    if(o == 0.0) return strtod("NaN", NULL);
    return o;

error:
    destroyVals_t(v);
    errno = ENOMEM;
    return strtod("NaN", NULL);
}

static double intSum(bwOverlappingIntervals_t* ints, uint32_t start, uint32_t end) {
    uint32_t i, start_use, end_use;
    double o = 0.0;

    if(!ints->l) return strtod("NaN", NULL);

    for(i=0; i<ints->l; i++) {
        start_use = ints->start[i];
        end_use = ints->end[i];
        if(start_use < start) start_use = start;
        if(end_use > end) end_use = end;
        o += (end_use - start_use) * ints->value[i];
    }

    return o;
}

//Returns NULL on error, otherwise a double* that needs to be free()d
static double *bwStatsFromZoom(bigWigFile_t *fp, int32_t level, uint32_t tid, uint32_t start, uint32_t end, uint32_t nBins, enum bwStatsType type) {
    bwOverlapBlock_t *blocks = NULL;
    double *output = NULL;
    uint32_t pos = start, i, end2;

    if(!fp->hdr->zoomHdrs->idx[level]) {
        fp->hdr->zoomHdrs->idx[level] = bwReadIndex(fp, fp->hdr->zoomHdrs->indexOffset[level]);
        if(!fp->hdr->zoomHdrs->idx[level]) return NULL;
    }
    errno = 0; //Sometimes libCurls sets and then doesn't unset errno on errors

    output = malloc(sizeof(double)*nBins);
    if(!output) return NULL;

    for(i=0, pos=start; i<nBins; i++) {
        end2 = start + ((double)(end-start)*(i+1))/((int) nBins);
        blocks = walkRTreeNodes(fp, fp->hdr->zoomHdrs->idx[level]->root, tid, pos, end2);
        if(!blocks) goto error;

        switch(type) {
        case 0:
            //mean
            output[i] = blockMean(fp, blocks, tid, pos, end2);
            break;
        case 1:
            //stdev
            output[i] = blockDev(fp, blocks, tid, pos, end2);
            break;
        case 2:
            //max
            output[i] = blockMax(fp, blocks, tid, pos, end2);
            break;
        case 3:
            //min
            output[i] = blockMin(fp, blocks, tid, pos, end2);
            break;
        case 4:
            //cov
            output[i] = blockCoverage(fp, blocks, tid, pos, end2)/(end2-pos);
            break;
        case 5:
            //sum
            output[i] = blockSum(fp, blocks, tid, pos, end2);
            break;
        default:
            goto error;
            break;
        }
        if(errno) goto error;
        destroyBWOverlapBlock(blocks);
        pos = end2;
    }

    return output;

error:
    fprintf(stderr, "got an error in bwStatsFromZoom in the range %"PRIu32"-%"PRIu32": %s\n", pos, end2, strerror(errno));
    if(blocks) destroyBWOverlapBlock(blocks);
    if(output) free(output);
    return NULL;
}

double *bwStatsFromFull(bigWigFile_t *fp, const char *chrom, uint32_t start, uint32_t end, uint32_t nBins, enum bwStatsType type) {
    bwOverlappingIntervals_t *ints = NULL;
    double *output = malloc(sizeof(double)*nBins);
    uint32_t i, pos = start, end2;
    if(!output) return NULL;

    for(i=0; i<nBins; i++) {
        end2 = start + ((double)(end-start)*(i+1))/((int) nBins);
        ints = bwGetOverlappingIntervals(fp, chrom, pos, end2);

        if(!ints) {
            output[i] = strtod("NaN", NULL);
            continue;
        }

        switch(type) {
        default :
        case 0:
            output[i] = intMean(ints, pos, end2);
            break;
        case 1:
            output[i] = intDev(ints, pos, end2);
            break;
        case 2:
            output[i] = intMax(ints);
            break;
        case 3:
            output[i] = intMin(ints);
            break;
        case 4:
            output[i] = intCoverage(ints, pos, end2);
            break;
        case 5:
            output[i] = intSum(ints, pos, end2);
            break;
        }
        bwDestroyOverlappingIntervals(ints);
        pos = end2;
    }

    return output;
}

//Returns a list of floats of length nBins that must be free()d
//On error, NULL is returned
double *bwStats(bigWigFile_t *fp, const char *chrom, uint32_t start, uint32_t end, uint32_t nBins, enum bwStatsType type) {
    int32_t level = determineZoomLevel(fp, ((double)(end-start))/((int) nBins));
    uint32_t tid = bwGetTid(fp, chrom);
    if(tid == (uint32_t) -1) return NULL;

    if(level == -1) return bwStatsFromFull(fp, chrom, start, end, nBins, type);
    return bwStatsFromZoom(fp, level, tid, start, end, nBins, type);
}
