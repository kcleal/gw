#ifndef NOCURL
#include <curl/curl.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bigWigIO.h"
#include <inttypes.h>
#include <errno.h>

size_t GLOBAL_DEFAULTBUFFERSIZE;

#ifndef NOCURL
uint64_t getContentLength(const URL_t *URL) {
    double size;
    if(curl_easy_getinfo(URL->x.curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &size) != CURLE_OK) {
        return 0;
    }
    if(size== -1.0) return 0;
    return (uint64_t) size;
}

//Fill the buffer, note that URL may be left in an unusable state on error!
CURLcode urlFetchData(URL_t *URL, unsigned long bufSize) {
    CURLcode rv;
    char range[1024];

    if(URL->filePos != (size_t) -1) URL->filePos += URL->bufLen;
    else URL->filePos = 0;

    URL->bufPos = URL->bufLen = 0; //Otherwise, we can't copy anything into the buffer!
    sprintf(range,"%lu-%lu", URL->filePos, URL->filePos+bufSize-1);
    rv = curl_easy_setopt(URL->x.curl, CURLOPT_RANGE, range);
    if(rv != CURLE_OK) {
        fprintf(stderr, "[urlFetchData] Couldn't set the range (%s)\n", range);
        return rv;
    }

    rv = curl_easy_perform(URL->x.curl);
    errno = 0; //Sometimes curl_easy_perform leaves a random errno remnant
    return rv;
}

//Read data into a buffer, ideally from a buffer already in memory
//The loop is likely no longer needed.
size_t url_fread(void *obuf, size_t obufSize, URL_t *URL) {
    size_t remaining = obufSize, fetchSize;
    void *p = obuf;
    CURLcode rv;

    while(remaining) {
        if(!URL->bufLen) {
            rv = urlFetchData(URL, URL->bufSize);
            if(rv != CURLE_OK) {
                fprintf(stderr, "[url_fread] urlFetchData (A) returned %s\n", curl_easy_strerror(rv));
                return 0;
            }  
        } else if(URL->bufLen < URL->bufPos + remaining) { //Copy the remaining buffer and reload the buffer as needed
            p = memcpy(p, URL->memBuf+URL->bufPos, URL->bufLen - URL->bufPos);
            if(!p) return 0;
            p += URL->bufLen - URL->bufPos;
            remaining -= URL->bufLen - URL->bufPos;
            if(remaining) {
                if(!URL->isCompressed) {
                    fetchSize = URL->bufSize;
                } else {
                    fetchSize = (remaining<URL->bufSize)?remaining:URL->bufSize;
                }
                rv = urlFetchData(URL, fetchSize);
                if(rv != CURLE_OK) {
                    fprintf(stderr, "[url_fread] urlFetchData (B) returned %s\n", curl_easy_strerror(rv));
                    return 0;
                }
            }
        } else {
            p = memcpy(p, URL->memBuf+URL->bufPos, remaining);
            if(!p) return 0;
            URL->bufPos += remaining;
            remaining = 0;
        }
    }
    return obufSize;
}
#endif

//Returns the number of bytes requested or a smaller number on error
//Note that in the case of remote files, the actual amount read may be less than the return value!
size_t urlRead(URL_t *URL, void *buf, size_t bufSize) {
#ifndef NOCURL
    if(URL->type==0) {
        return fread(buf, bufSize, 1, URL->x.fp)*bufSize;
    } else {
        return url_fread(buf, bufSize, URL);
    }
#else
    return fread(buf, bufSize, 1, URL->x.fp)*bufSize;
#endif
}

size_t bwFillBuffer(const void *inBuf, size_t l, size_t nmemb, void *pURL) {
    URL_t *URL = (URL_t*) pURL;
    void *p = URL->memBuf;
    size_t copied = l*nmemb;
    if(!p) return 0;

    p += URL->bufLen;
    if(l*nmemb > URL->bufSize - URL->bufPos) { //We received more than we can store!
        copied = URL->bufSize - URL->bufLen;
    }
    memcpy(p, inBuf, copied);
    URL->bufLen += copied;

    if(!URL->memBuf) return 0; //signal error
    return copied;
}

//Seek to an arbitrary location, returning a CURLcode
//Note that a local file returns CURLE_OK on success or CURLE_FAILED_INIT on any error;
CURLcode urlSeek(URL_t *URL, size_t pos) {
#ifndef NOCURL
    char range[1024];
    CURLcode rv;

    if(URL->type == BWG_FILE) {
#endif
        if(fseek(URL->x.fp, pos, SEEK_SET) == 0) {
            errno = 0;
            return CURLE_OK;
        } else {
            return CURLE_FAILED_INIT; //This is arbitrary
        }
#ifndef NOCURL
    } else {
        //If the location is covered by the buffer then don't seek!
        if(pos < URL->filePos || pos >= URL->filePos+URL->bufLen) {
            URL->filePos = pos;
            URL->bufLen = 0; //Otherwise, filePos will get incremented on the next read!
            URL->bufPos = 0;
            //Maybe this works for FTP?
            sprintf(range,"%lu-%lu", pos, pos+URL->bufSize-1);
            rv = curl_easy_setopt(URL->x.curl, CURLOPT_RANGE, range);
            if(rv != CURLE_OK) {
                fprintf(stderr, "[urlSeek] Couldn't set the range (%s)\n", range);
                return rv;
            }
            rv = curl_easy_perform(URL->x.curl);
            if(rv != CURLE_OK) {
                fprintf(stderr, "[urlSeek] curl_easy_perform received an error!\n");
            }
            errno = 0;  //Don't propogate remnant resolved libCurl errors
            return rv;
        } else {
            URL->bufPos = pos-URL->filePos;
            return CURLE_OK;
        }
    }
#endif
}

URL_t *urlOpen(const char *fname, CURLcode (*callBack)(CURL*), const char *mode) {
    URL_t *URL = calloc(1, sizeof(URL_t));
    if(!URL) return NULL;
    char *url = NULL, *req = NULL;
#ifndef NOCURL
    CURLcode code;
    char range[1024];
#endif

    URL->fname = fname;

    if((!mode) || (strchr(mode, 'w') == 0)) {
        //Set the protocol
#ifndef NOCURL
        if(strncmp(fname, "http://", 7) == 0) URL->type = BWG_HTTP;
        else if(strncmp(fname, "https://", 8) == 0) URL->type = BWG_HTTPS;
        else if(strncmp(fname, "ftp://", 6) == 0) URL->type = BWG_FTP;
        else URL->type = BWG_FILE;
#else
        URL->type = BWG_FILE;
#endif

        //local file?
        if(URL->type == BWG_FILE) {
            URL->filePos = -1; //This signals that nothing has been read
            URL->x.fp = fopen(fname, "rb");
            if(!(URL->x.fp)) {
                free(URL);
                fprintf(stderr, "[urlOpen] Couldn't open %s for reading\n", fname);
                return NULL;
            }
#ifndef NOCURL
        } else {
            //Remote file, set up the memory buffer and get CURL ready
            URL->memBuf = malloc(GLOBAL_DEFAULTBUFFERSIZE);
            if(!(URL->memBuf)) {
                free(URL);
                fprintf(stderr, "[urlOpen] Couldn't allocate enough space for the file buffer!\n");
                return NULL;
            }
            URL->bufSize = GLOBAL_DEFAULTBUFFERSIZE;
            URL->x.curl = curl_easy_init();
            if(!(URL->x.curl)) {
                fprintf(stderr, "[urlOpen] curl_easy_init() failed!\n");
                goto error;
            }
            //Negotiate a reasonable HTTP authentication method
            if(curl_easy_setopt(URL->x.curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY) != CURLE_OK) {
                fprintf(stderr, "[urlOpen] Failed instructing curl to use any HTTP authentication it finds to be suitable!\n");
                goto error;
            }
            //Follow redirects
            if(curl_easy_setopt(URL->x.curl, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK) {
                fprintf(stderr, "[urlOpen] Failed instructing curl to follow redirects!\n");
                goto error;
            }
            //Set the URL
            if(curl_easy_setopt(URL->x.curl, CURLOPT_URL, fname) != CURLE_OK) {
                fprintf(stderr, "[urlOpen] Couldn't set CURLOPT_URL!\n");
                goto error;
            }
            //Set the range, which doesn't do anything for HTTP
            sprintf(range, "0-%lu", URL->bufSize-1);
            if(curl_easy_setopt(URL->x.curl, CURLOPT_RANGE, range) != CURLE_OK) {
                fprintf(stderr, "[urlOpen] Couldn't set CURLOPT_RANGE (%s)!\n", range);
                goto error;
            }
            //Set the callback info, which means we no longer need to directly deal with sockets and header!
            if(curl_easy_setopt(URL->x.curl, CURLOPT_WRITEFUNCTION, bwFillBuffer) != CURLE_OK) {
                fprintf(stderr, "[urlOpen] Couldn't set CURLOPT_WRITEFUNCTION!\n");
                goto error;
            }
            if(curl_easy_setopt(URL->x.curl, CURLOPT_WRITEDATA, (void*)URL) != CURLE_OK) {
                fprintf(stderr, "[urlOpen] Couldn't set CURLOPT_WRITEDATA!\n");
                goto error;
            }
            //Ignore certificate errors with https, libcurl just isn't reliable enough with conda
            if(curl_easy_setopt(URL->x.curl, CURLOPT_SSL_VERIFYPEER, 0) != CURLE_OK) {
                fprintf(stderr, "[urlOpen] Couldn't set CURLOPT_SSL_VERIFYPEER to 0!\n");
                goto error;
            }
            if(curl_easy_setopt(URL->x.curl, CURLOPT_SSL_VERIFYHOST, 0) != CURLE_OK) {
                fprintf(stderr, "[urlOpen] Couldn't set CURLOPT_SSL_VERIFYHOST to 0!\n");
                goto error;
            }
            if(callBack) {
                code = callBack(URL->x.curl);
                if(code != CURLE_OK) {
                    fprintf(stderr, "[urlOpen] The user-supplied call back function returned an error: %s\n", curl_easy_strerror(code));
                    goto error;
                }
            }
            code = curl_easy_perform(URL->x.curl);
            errno = 0; //Sometimes curl_easy_perform leaves a random errno remnant
            if(code != CURLE_OK) {
                fprintf(stderr, "[urlOpen] curl_easy_perform received an error: %s\n", curl_easy_strerror(code));
                goto error;
            }
#endif
        }
    } else {
        URL->type = BWG_FILE;
        URL->x.fp = fopen(fname, mode);
        if(!(URL->x.fp)) {
            free(URL);
            fprintf(stderr, "[urlOpen] Couldn't open %s for writing\n", fname);
            return NULL;
        }
    }
    if(url) free(url);
    if(req) free(req);
    return URL;

#ifndef NOCURL
error:
    if(url) free(url);
    if(req) free(req);
    free(URL->memBuf);
    curl_easy_cleanup(URL->x.curl);
    free(URL);
    return NULL;
#endif
}

//Performs the necessary free() operations and handles cleaning up curl
void urlClose(URL_t *URL) {
    if(URL->type == BWG_FILE) {
        fclose(URL->x.fp);
#ifndef NOCURL
    } else {
        free(URL->memBuf);
        curl_easy_cleanup(URL->x.curl);
#endif
    }
    free(URL);
}
