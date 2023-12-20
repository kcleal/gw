#ifndef LIBBIGWIG_IO_H
#define LIBBIGWIG_IO_H

#ifndef NOCURL
#include <curl/curl.h>
#else
#include <stdio.h>
#ifndef CURLTYPE_DEFINED
#define CURLTYPE_DEFINED
typedef int CURLcode;
typedef void CURL;
#endif
#define CURLE_OK 0
#define CURLE_FAILED_INIT 1
#endif
/*! \file bigWigIO.h
 * These are (typically internal) IO functions, so there's generally no need for you to directly use them!
 */

/*!
 * The size of the buffer used for remote files.
 */
extern size_t GLOBAL_DEFAULTBUFFERSIZE;

/*!
 * The enumerated values that indicate the connection type used to access a file.
 */
enum bigWigFile_type_enum {
    BWG_FILE = 0,
    BWG_HTTP = 1,
    BWG_HTTPS = 2,
    BWG_FTP = 3
};

/*!
 * @brief This structure holds the file pointers and buffers needed for raw access to local and remote files.
 */
typedef struct {
    union {
#ifndef NOCURL
        CURL *curl; /**<The CURL * file pointer for remote files.*/
#endif
        FILE *fp; /**<The FILE * file pointer for local files.**/
    } x; /**<A union holding curl and fp.*/
    void *memBuf; /**<A void * pointing to memory of size bufSize.*/
    size_t filePos; /**<Current position inside the file.*/
    size_t bufPos; /**<Curent position inside the buffer.*/
    size_t bufSize; /**<The size of the buffer.*/
    size_t bufLen; /**<The actual size of the buffer used.*/
    enum bigWigFile_type_enum type; /**<The connection type*/
    int isCompressed; /**<1 if the file is compressed, otherwise 0*/
    const char *fname; /**<Only needed for remote connections. The original URL/filename requested, since we need to make multiple connections.*/
} URL_t;

/*!
 *  @brief Reads data into the given buffer.
 *
 *  This function will store bufSize data into buf for both local and remote files. For remote files an internal buffer is used to store a (typically larger) segment of the remote file.
 *
 *  @param URL A URL_t * pointing to a valid opened file or remote URL.
 *  @param buf The buffer in memory that you would like filled. It must be able to hold bufSize bytes!
 *  @param bufSize The number of bytes to transfer to buf.
 *
 *  @return Returns the number of bytes stored in buf, which should be bufSize on success and something else on error.
 *
 *  @warning Note that on error, URL for remote files is left in an unusable state. You can get around this by running urlSeek() to a position outside of the range held by the internal buffer.
 */
size_t urlRead(URL_t *URL, void *buf, size_t bufSize);

/*!
 *  @brief Seeks to a given position in a local or remote file.
 * 
 *  For local files, this will set the file position indicator for the file pointer to the desired position. For remote files, it sets the position to start downloading data for the next urlRead(). Note that for remote files that running urlSeek() with a pos within the current buffer will simply modify the internal offset.
 *
 *  @param URL A URL_t * pointing to a valid opened file or remote URL.
 *  @param pos The position to seek to.
 *
 *  @return CURLE_OK on success and a different CURLE_XXX on error. For local files, the error return value is always CURLE_FAILED_INIT
 */
CURLcode urlSeek(URL_t *URL, size_t pos);

/*!
 *  @brief Open a local or remote file
 *
 *  Opens a local or remote file. Currently, http, https, and ftp are the only supported protocols and the URL must then begin with "http://", "https://", or "ftp://" as appropriate.
 *
 *  For remote files, an internal buffer is used to hold file contents, to avoid downloading entire files before starting. The size of this buffer and various variable related to connection timeout are set with bwInit().
 *
 *  Note that you **must** run urlClose() on this when finished. However, you would typically just use bwOpen() rather than directly calling this function.
 *
 * @param fname The file name or URL to open.
 * @param callBack An optional user-supplied function. This is applied to remote connections so users can specify things like proxy and password information.
 * @param mode "r", "w" or NULL. If and only if the mode contains the character "w" will the file be opened for writing.
 *
 *  @return A URL_t * or NULL on error.
 */
URL_t *urlOpen(const char *fname, CURLcode (*callBack)(CURL*), const char* mode);

/*!
 *  @brief Close a local/remote file
 *
 *  This will perform the cleanup required on a URL_t*, releasing memory as needed.
 *
 *  @param URL A URL_t * pointing to a valid opened file or remote URL.
 *
 *  @warning URL will no longer point to a valid location in memory!
 */
void urlClose(URL_t *URL);

#endif // LIBBIGWIG_IO_H
