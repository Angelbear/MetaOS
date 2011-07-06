#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>  
#include <regex.h>
#include <dirent.h>

#include "CA.h"
#include "common.h"

struct MemoryStruct {
    char *memory;
    size_t size;
}; 
struct MemoryStruct chunk; 

static void *myrealloc(void *ptr, size_t size)
{
    /* There might be a realloc() out there that doesn't like reallocing
     *      NULL pointers, so we take care of it here */
    if(ptr)
        return realloc(ptr, size);
    else
        return malloc(size);
} 

static size_t WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)data; 
    mem->memory = (char *)myrealloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory) {
        memcpy(&(mem->memory[mem->size]), ptr, realsize);
        mem->size += realsize;
        mem->memory[mem->size] = 0;
    }
    return realsize;
}

int getFileInBuffer(char* URL, char * buffer)
{
    CURL *curl_handle;
    struct MemoryStruct chunk;
    chunk.memory=NULL; /* we expect realloc(NULL, size) to work */
    chunk.size = 0;    /* no data at this point */
    curl_global_init(CURL_GLOBAL_ALL);
    /* init the curl session */
    curl_handle = curl_easy_init();
    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, URL);
    /* send all data to this function  */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    /* we pass our 'chunk' struct to the callback function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    /* some servers don't like requests that are made without a user-agent
       field, so we provide one */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    /* get it! */
    curl_easy_perform(curl_handle);
    /* cleanup curl stuff */
    curl_easy_cleanup(curl_handle);
    /*
     * Now, our chunk.memory points to a memory block that is chunk.size
     * bytes big and contains the remote file.
     *
     * Do something nice with it!
     *
     * You should be aware of the fact that at this point we might have an
     * allocated data block, and nothing has yet deallocated that data. So when
     * you're done with it, you should free() it as a nice application.
     */
    memcpy(buffer, chunk.memory, chunk.size);
    buffer[chunk.size] = 0;

    if(chunk.memory)
        free(chunk.memory);
    /* we're done with libcurl, so clean it up */
    curl_global_cleanup();
    return chunk.size;
}

static char* CHECK_REGISTER_REGX = "retCode=([0|1])&retDes=(.*)&UID=([0-9]*)&encPriKey=(.*)";
static char* APPLY_RANDOM_REGEX = "retCode=([0|1])&retDes=(.*)&randomNumber=([0-9]*)";
static char* APPLY_CPK_PRI_REGEX  = "retCode=([0|1])&retDes=(.*)&encPriKey=(.*)";
static char* CPK_AUTHENTICATION_REGEX = "retCode=([0|1])&retDes=(.*)&verifyResult=(.*)";

static char* substr(const char*str,unsigned start, unsigned end)
{
    unsigned n = end - start;
    static char stbuf[256];
    strncpy(stbuf, str + start, n);
    stbuf[n] = 0;
    return stbuf;
}

int checkForRegister(char* buf,char* priKey, char* UID) {
    regex_t reg;
    regmatch_t pm[10]; 
    const size_t nmatch = 10;
    int ret = regcomp(&reg, CHECK_REGISTER_REGX, REG_EXTENDED);
    int result = CA_SUCCESS;
    
    if(ret != 0) {
        result = UNKNOWN_ERROR;      
        goto END;
    }
    ret = regexec(&reg, buf, nmatch, pm, 0);
    if(ret == REG_NOMATCH) {
        result = CA_FORMAT_ERROR; 
        goto END;
    }else if (ret != 0) {
        result = CA_FORMAT_ERROR;
        goto END;
    }
    if(pm[1].rm_so != -1) {
        if(!strcmp(substr(buf, pm[1].rm_so, pm[1].rm_eo),"1")) {
            result = CA_FALSE;
            goto END;
        }
        if(pm[4].rm_so != -1) {
            strncpy(priKey, buf+pm[4].rm_so, pm[4].rm_eo-pm[4].rm_so);
        }
        if(pm[3].rm_so != -1) {
            strncpy(UID, buf+pm[3].rm_so, pm[3].rm_eo-pm[3].rm_so);
        }
    }else {
        result = CA_FORMAT_ERROR;
    }
END:
    regfree(&reg);
    return result;
}

int applyForRandom(char* buf,char* random) {
    regex_t reg;
    regmatch_t pm[10];
    const size_t nmatch = 10;
    int ret = regcomp(&reg, APPLY_RANDOM_REGEX, REG_EXTENDED);
    int result = CA_SUCCESS;
    if(ret != 0) {
        result =  UNKNOWN_ERROR;
        goto END;
    }
    ret = regexec(&reg, buf, nmatch, pm, 0);
    if(ret == REG_NOMATCH) {
        result = CA_FORMAT_ERROR;
        goto END;
    }else if (ret != 0) {
        result = CA_FORMAT_ERROR;
        goto END;
    }
    if(pm[1].rm_so != -1) {
        if(!strcmp(substr(buf, pm[1].rm_so, pm[1].rm_eo),"0")) {
            result = CA_FALSE;
            goto END;
        }
        if(pm[3].rm_so != -1) {
            strncpy(random, buf+pm[3].rm_so, pm[3].rm_eo-pm[3].rm_so);
        }
    }else {
        result = CA_FORMAT_ERROR;
    }
END:
    regfree(&reg);
    return  result;
}

int applyForPriKey(char* buf, char* priKey,char* uid) {
    regex_t reg;
    regmatch_t pm[10];
    const size_t nmatch = 10;
    int ret = regcomp(&reg, APPLY_CPK_PRI_REGEX, REG_EXTENDED);
    int result = CA_SUCCESS;
    if(ret != 0) {
        result = UNKNOWN_ERROR;
        goto END;
    }
    ret = regexec(&reg, buf, nmatch, pm, 0);
    if(ret == REG_NOMATCH) {
        result = CA_FORMAT_ERROR;
        goto END;
    }else if (ret != 0) {
        result = CA_FORMAT_ERROR;
        goto END;
    }
    if(pm[1].rm_so != -1) {
        if(!strcmp(substr(buf, pm[1].rm_so, pm[1].rm_eo),"1")) {
            result = CA_FALSE;
            goto END;
        }
        if(pm[3].rm_so != -1) {
            strncpy(priKey, buf+pm[3].rm_so, pm[3].rm_eo-pm[3].rm_so);
        }
        if(pm[4].rm_so != -1) {
            strncpy(uid, buf+pm[4].rm_so, pm[4].rm_eo-pm[4].rm_so);
        }
    }else {
        result = CA_FORMAT_ERROR;
    }
END:
    regfree(&reg);
    return result;
}

int cpkAuthentication(char* buf) {
    regex_t reg;
    regmatch_t pm[10];
    const size_t nmatch = 10;
    int ret = regcomp(&reg, CPK_AUTHENTICATION_REGEX, REG_EXTENDED);
    int result = CA_TRUE;
    if(ret != 0) {
        result = UNKNOWN_ERROR;
        goto END;
    }
    ret = regexec(&reg, buf, nmatch, pm, 0);
    if(ret == REG_NOMATCH) {
        result = CA_FORMAT_ERROR;
        goto END;
    }else if (ret != 0) {
        result = CA_FORMAT_ERROR;
        goto END;
    }
    if(pm[1].rm_so != -1) {
        if(!strcmp(substr(buf, pm[1].rm_so, pm[1].rm_eo),"1")) {
            result = CA_FALSE;
            goto END;
        }
        if(!strcmp(substr(buf, pm[3].rm_so, pm[3].rm_eo),"success")) {
            result =  CA_FALSE;
            goto END;
        }
    }
END:
    regfree(&reg);
    return result;
}

int check_register(char* imsi, char* pri, char* uid) {
    char url[128],buf[1024];
    snprintf(url,sizeof(url),CHECK_REGISTER_URL_TEP,imsi);
    getFileInBuffer(url,buf);
    if(checkForRegister(buf,pri,uid) == CA_SUCCESS) {
        return 1;
    }
    return 0;
}

int apply_random(char* uid, char* random) {
    char url[128], buf[1024];
    snprintf(url,sizeof(url),APPLY_RANDOM_URL_TEP, uid);
    getFileInBuffer(url, buf);
    if(applyForRandom(buf, random) == CA_SUCCESS) {
        return 1;
    }
    return 0;
}

int apply_cpk_pri(char* imsi,char* pri,char* uid) {
    char url[128], buf[1024];
    snprintf(url,sizeof(url), APPLY_CPK_PRI_URL_TEP, imsi);
    getFileInBuffer(url, buf);
    if(applyForPriKey(buf, pri, uid) == CA_SUCCESS) {
        return 1;
    }
    return 0;
}

int cpk_authentication(char* uid, char* signature) {
    char url[1024], buf[1024];
    snprintf(url,sizeof(url), CPK_AUTHENTICATION_URL_TEP, uid, signature);
    getFileInBuffer(url, buf);
    return cpkAuthentication(buf);
}

char *path_cat (const char *str1, char *str2) {
    size_t str1_len = strlen(str1);
    size_t str2_len = strlen(str2);
    char *result;
    result = malloc((str1_len+str2_len+1)*sizeof *result);
    strcpy (result,str1);
    int i,j;
    for(i=str1_len, j=0; ((i<(str1_len+str2_len)) && (j<str2_len));i++, j++) {
        result[i]=str2[j];
    }
    result[str1_len+str2_len]='\0';
    return result;
}

static char* IMSI_FILE_REGEX_TEP="%s_([0-9]+)_cpk.pem";
int is_priKey_exist(char* imsi, char* path, char* uid) {
    struct dirent *dp;
    char pattern[256];
    snprintf(pattern, sizeof(pattern), IMSI_FILE_REGEX_TEP, imsi);
    regex_t reg;
    regmatch_t pm[10];
    const size_t nmatch = 10;
    int ret = regcomp(&reg, pattern, REG_EXTENDED);
    if(ret != 0) {
        return UNKNOWN_ERROR;
    }

    int result = 0;
    // enter existing path to directory below
#ifndef __LINUX__
    const char *dir_path="/sdcard/";
#else
    const char* dir_path="./";
#endif
    DIR *dir = opendir(dir_path);
    while ((dp=readdir(dir)) != NULL) {
        char *tmp;
        tmp = path_cat(dir_path, dp->d_name);
        ret = regexec(&reg, tmp, nmatch, pm, 0);
        //free(tmp);
        //tmp = NULL;

        if(ret == 0){
            result = 1;
            strncpy(path, dp->d_name,strlen(dp->d_name));
            strncpy(uid, tmp+pm[1].rm_so, pm[1].rm_eo - pm[1].rm_so);
            break;
        }
        free(tmp);
        tmp = NULL;

    }
    regfree(&reg);
    closedir(dir);
    return result;
}

int save_priKey(char* priKey, char* filename) {
    FILE *fp;
    if((fp=fopen(filename, "wb"))==NULL) {
        return 0;
    }
    if(fprintf(fp,priKey) != strlen(priKey)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    
    return 1;
}
