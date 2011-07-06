#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "CA.h"
#include "utils.h"
#include "cloudstorage.h"

#define DEBUG

typedef enum HTTP_METHOD {
    HTTP_GET = 0,
    HTTP_PUT,
    HTTP_DEL,
} HTTP_METHOD; 

#define MAX_HTTP_RESPONSE_SIZE  4096

struct HTTPReponseMemoryStruct {
    char memory[MAX_HTTP_RESPONSE_SIZE];
    size_t size;
};

int getFile(const char* dstFile, const char* url, const char* customHTTPHeader);
int putFile(const char* srcFile, const char* url, const char* customHTTPHeader);
int rest(int httpMethod, const char* url, const char* customhttpheader);

const char* CLOUD_STORAGE_URL = "http://210.75.5.232:8085/";
const char* WALLPAPER_FILENAME = "wallpaper.jpg";
const char* MYPREFERENCES_URL_TEP = "http://210.75.5.232:8085/%s/myPreferecens/";
const char* WALLPAPER_URL_TEP = "http://210.75.5.232:8085/%s/myPreferecens/wallpaper.jpg";

CURL *curl_handle;
char curl_error_buffer[CURL_ERROR_SIZE];
long curl_http_response_code;
struct HTTPReponseMemoryStruct curl_memory_chunk;


int test(char* uid, char* signature) {
    initCurl();
    CURLcode res;
    res = uploadLocalFile("./wallpaper.jpg", uid, signature, "wallpaper.jpg");
    if (CURLE_OK == res) {
        printf("\nupload wallpaper file success: %ld\n", curl_http_response_code);
        if (curl_memory_chunk.size) {
            printf("http response: %s\n", curl_memory_chunk.memory);
        }
    }
    else {
        printf("\nupload wall paper file failure, rescode %d\n", res);
    }
    res = downloadCloudFile("./wallpaper-new.jpg", uid, signature, "wallpaper.jpg");
    if (CURLE_OK == res) {
        printf("\nretrieve wallpaper file success: %ld\n", curl_http_response_code);
    }
    else {
        printf("\nretrieve wall paper file failure, rescode %d\n", res);
    }
    res = deleteCloudFile(uid, signature, "wallpaper.jpg");
    if (CURLE_OK == res) {
        printf("\ndelete wallpaper file success: %ld\n", curl_http_response_code);
        if (curl_memory_chunk.size) {
            printf("http response: %s\n", curl_memory_chunk.memory);
        }
    }
    else {
        printf("\ndelete wall paper file failure, rescode %d\n", res);
    }
    cleanupCurl();
    return 0;
}

char *getFileNameFromPath (const char *pathname)
{
    char *fname = NULL;
    if (pathname)
    {
        fname = strrchr (pathname, '/');
        if (fname) {
            fname = fname + 1;
        }
        else {
            fname = (char*)pathname;
        }
    }
    return fname;
}        

// download file from cloud storage
int downloadCloudFile(const char* dstFile, const char* uid, const char* token, const char* fileName) {
    char url[256], auth[256];
    snprintf(url,sizeof(url), MYPREFERENCES_URL_TEP, uid);
    //strcpy(url, MYPREFERENCES_URL);
    strcat(url, fileName);
    snprintf(auth, sizeof(auth), "Authorization:CPK %s:%s", uid, token);
#ifdef DEBUG        
    printf("url: %s\n", url);
    printf("authorization: %s\n", auth);
#endif       
    CURLcode res = getFile(dstFile, url, auth);
    if (CURLE_OK != res && 200 != curl_http_response_code) {
#ifdef DEBUG
        printf("curl error msg: %s\n", curl_error_buffer);
        printf("http response code: %ld\n", curl_http_response_code);
#endif        
        return -1;            
    }
    return 0;
}

// upload file into cloud storage
int uploadLocalFile(const char* srcFile, const char* uid, const char* token, const char* fileName) {
    char url[256], auth[256];
    snprintf(url, sizeof(url), MYPREFERENCES_URL_TEP, uid);
    //strcpy(url, MYPREFERENCES_URL);
    strcat(url, fileName);
    snprintf(auth, sizeof(auth), "Authorization:CPK %s:%s", uid, token);
#ifdef DEBUG        
    printf("url: %s\n", url);
    printf("authorization: %s\n", auth);
#endif        
    CURLcode res = putFile(srcFile, url, auth);
    if (CURLE_OK != res && 200 != curl_http_response_code) {
#ifdef DEBUG
        printf("curl error msg: %s\n", curl_error_buffer);
        printf("http response code: %ld\n", curl_http_response_code);
        if (curl_memory_chunk.size) {
            printf("http response: %s\n", curl_memory_chunk.memory);
        }
#endif        
        return -1;            
    }
    return 0;
}

// delete file in cloud storage
int deleteCloudFile(const char* uid, const char* token, const char* fileName) {
    char url[256], auth[256];
    snprintf(url, sizeof(url), MYPREFERENCES_URL_TEP, uid);
    //strcpy(url, MYPREFERENCES_URL);
    strcat(url, fileName);
    snprintf(auth, sizeof(auth), "Authorization:CPK %s:%s", uid, token);
#ifdef DEBUG        
    printf("url: %s\n", url);
    printf("authorization: %s\n", auth);
#endif        
    CURLcode res = rest(HTTP_DEL, url, auth);
    if (CURLE_OK != res && 200 != curl_http_response_code) {
#ifdef DEBUG
        printf("curl error msg: %s\n", curl_error_buffer);
        printf("http response code: %ld\n", curl_http_response_code);
        if (curl_memory_chunk.size) {
            printf("http response: %s\n", curl_memory_chunk.memory);
        }
#endif        
        return -1;            
    }
    return 0;
}

int initCurl() {
    /* In windows, this will init the winsock stuff */
    curl_global_init(CURL_GLOBAL_ALL);

    /* get a curl handle */
    curl_handle = curl_easy_init();
    if (NULL == curl_handle) {
#ifdef DEBUG    
        printf("curl easy init failure\n");
#endif     
        return -1;    
    }
    return 0;
}

void cleanupCurl() {
    /* always cleanup curl stuff */
    curl_easy_cleanup(curl_handle);

    curl_global_cleanup(); 
}

static size_t writeMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
    size_t realsize = size * nmemb;
    struct HTTPReponseMemoryStruct *mem = (struct HTTPReponseMemoryStruct *)data;

    if (mem->size + realsize + 1 > sizeof(mem->memory)) {
        /* out of memory! */
        printf("not enough memory, buffer is full\n");
        exit(EXIT_FAILURE);
    }           
    memcpy(&(mem->memory[mem->size]), ptr, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

int rest(int httpMethod, const char* url, const char* customhttpheader) {
    CURLcode res;

    curl_memory_chunk.size = 0;

    curl_easy_reset(curl_handle); 

    struct curl_slist *list = NULL;
    list = curl_slist_append(list, customhttpheader);

    switch (httpMethod) {
        case HTTP_GET: 
            /* HTTP GET please */
            curl_easy_setopt(curl_handle, CURLOPT_HTTPGET, 1L);
            break;

        case HTTP_PUT:
            /* HTTP PUT please */
            curl_easy_setopt(curl_handle, CURLOPT_PUT, 1L);
            break;

        case HTTP_DEL:
            /* HTTP DELETE please */
            curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "DELETE"); 
            break;

        default:
            curl_easy_setopt(curl_handle, CURLOPT_HTTPGET, 1L);
            break;
    }

    /* specify target URL, and note that this URL should include a file
       name, not only a directory */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* http header with our own custom authorization: */
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, list);

    /* store human readable error messages */
    curl_easy_setopt(curl_handle, CURLOPT_ERRORBUFFER, curl_error_buffer);

    /* send all data to this function  */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);

    /* we pass our 'chunk' struct to the callback function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&curl_memory_chunk);

    /* Now run off and do what you've been told! */
    res = curl_easy_perform(curl_handle);

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE , &curl_http_response_code); 

    curl_slist_free_all(list);

    return res;
}

/*
 * This example shows a HTTP PUT operation. PUTs a file given as a command
 * line argument to the URL also given on the command line.
 *
 * This example also uses its own read callback.
 *
 * Here's an article on how to setup a PUT handler for Apache:
 * http://www.apacheweek.com/features/put
 */
static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *stream)
{
    size_t retcode;

    /* in real-world cases, this would probably get this data differently
       as this fread() stuff is exactly what the library already would do
       by default internally */
    retcode = fread(ptr, size, nmemb, stream);

    return retcode;
}

int putFile(const char* srcFile, const char* url, const char* customHTTPHeader) {

    CURLcode res;

    curl_memory_chunk.size = 0;

    /* get the file size of the local file */
    int hd = open(srcFile, O_RDONLY) ;
    if (0 == hd) {
#ifdef DEBUG    
        printf("open file( %s ) failure\n", srcFile);
#endif        
        return -1;
    }
    struct stat file_info;
    fstat(hd, &file_info);
#ifdef DEBUG    
    printf("szie of file( %s ) is : %d\n", srcFile, (int)file_info.st_size);
#endif    

    /* get a FILE * of the same file, could also be made with
       fdopen() from the previous descriptor, but hey this is just
       an example! */
    FILE * hd_src = fdopen(hd, "rb");
    if (hd_src == NULL) {
#ifdef DEBUG    
        printf("open file( %s ) handler failure\n", srcFile);
#endif     
        return -1;
    }

    curl_easy_reset(curl_handle); 

    struct curl_slist *list = NULL;    
    list = curl_slist_append(list, customHTTPHeader);

    /* we want to use our own read function */
    curl_easy_setopt(curl_handle, CURLOPT_READFUNCTION, read_callback);

    /* enable uploading */
    curl_easy_setopt(curl_handle, CURLOPT_UPLOAD, 1L);

    /* HTTP PUT please */
    curl_easy_setopt(curl_handle, CURLOPT_PUT, 1L);

    /* specify target URL, and note that this URL should include a file
       name, not only a directory */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* now specify which file to upload */
    curl_easy_setopt(curl_handle, CURLOPT_READDATA, hd_src);

    /* provide the size of the upload, we specicially typecast the value
       to curl_off_t since we must be sure to use the correct data size */
    curl_easy_setopt(curl_handle, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_info.st_size);

    /* redo request with our own custom Accept: */
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, list);

    /* store human readable error messages */
    curl_easy_setopt(curl_handle, CURLOPT_ERRORBUFFER, curl_error_buffer);

    /* send all data to this function  */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);

    /* we pass our 'chunk' struct to the callback function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&curl_memory_chunk);

    /* Now run off and do what you've been told! */
    res = curl_easy_perform(curl_handle);

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE , &curl_http_response_code); 

    curl_slist_free_all(list);

    /* close the local file */
    fclose(hd_src); 

    return res;
}

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
    int written = fwrite(ptr, size, nmemb, (FILE *)stream);
    return written;
}


int getFile(const char* dstFile, const char* url, const char* customHTTPHeader) {

    CURLcode res; 

    curl_memory_chunk.size = 0;

    /* open the files */
    FILE *hd_dst = fopen(dstFile,"wb");
    if (hd_dst == NULL) {
#ifdef DEBUG    
        printf("open file %s failure\n", dstFile);
#endif        
        return -1;
    }

    curl_easy_reset(curl_handle); 

    struct curl_slist *list = NULL;
    list = curl_slist_append(list, customHTTPHeader);

    /* set URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* no progress meter please */
    curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);

    /* send all data to this function  */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);

    /*
     * Notice here that if you want the actual data sent anywhere else but
     * stdout, you should consider using the CURLOPT_WRITEDATA option.  */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, hd_dst);

    /* redo request with our own custom Accept: */
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, list);

    /* store human readable error messages */
    curl_easy_setopt(curl_handle, CURLOPT_ERRORBUFFER, curl_error_buffer);

    /* get it! */
    res = curl_easy_perform(curl_handle);

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE , &curl_http_response_code); 

    curl_slist_free_all(list); 

    /* close the header file */
    fclose(hd_dst);

    return res;
}



