
#ifndef _CLOUD_STORAGE_H_
#define _CLOUD_STORAGE_H_

#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

int initCurl();
void cleanupCurl();

int downloadCloudFile(const char* dstFile, const char* uid, const char* token, const char* fileName);
int uploadLocalFile(const char* srcFile, const char* uid, const char* token, const char* fileName);
int deleteCloudFile(const char* uid, const char* token, const char* filename);

extern CURL *curl_handle;
extern char curl_error_buffer[CURL_ERROR_SIZE];
extern const char* CLOUD_STORAGE_URL;
extern const char* WALLPAPER_FILENAME;
extern const char* MYPREFERENCES_URL;
extern const char* WALLPAPER_URL;

#endif
 
