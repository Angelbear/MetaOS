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

#include "CA.h"
#include "utils.h"
#include "cloudstorage.h"


#ifndef __LINUX__
#define LOCAL_WALLPAPER_FILE "/data/data/com.android.settings/wallaper"
#else
#define LOCAL_WALLPAPER_FILE "./wallpaper.jpg"
#endif

#define CLOUD_WALLPAPER_FILE_NAME "wallpaper"


static char randoms[64];
static char signature[256];
static char ret[256];

void backup_action() {
    char imsi[32],filepath[64], command[256], uid[64], ret[256], signature[256];
    memset(imsi, 0, sizeof(imsi));
    memset(filepath, 0, sizeof(filepath));
    memset(command, 0, sizeof(command));
    memset(uid, 0, sizeof(uid));
    memset(ret, 0, sizeof(ret));
    memset(signature, 0, sizeof(signature));

    if(is_imsi_got(imsi)) {
        printf("get imsi %s\n",imsi);
        if(is_priKey_exist(imsi,filepath,uid)) {
            printf("private key file exist with uid %s\n",uid);
            memset(signature, 0, sizeof(signature));
            memset(ret, 0, sizeof(ret));
            if(strlen(randoms) == 0) {
                printf("random not exist\n");
APPLY:
                if(apply_random(uid,randoms)) {
                    printf("apply randoms sucess %s\n",randoms);
SIGNATURE:
                    snprintf(command, sizeof(command), "echo %s | head -c %d | ./cpk -sign -pass %s%s > .signature", randoms, strlen(randoms), imsi, "0123456789abcdefghijklmnopqrstuv");                                    
                    system(command);
                    FILE* stream = fopen(".signature","r");
                    int size = fread(ret, sizeof(char), sizeof(ret), stream);                               
                    printf("raw signature is %s\n",ret);
                    if(size <= 0) {                                                                      
                        goto SIGNATURE;
                    }
                    size = URLEncode(ret,size,signature,sizeof(signature));                             
                    signature[size - 3] = 0;
                    printf("encoded singature is %s\n",signature);
                    if(cpk_authentication(uid,ret) == CA_TRUE) {
                        printf("authentication sucess\n");
                        goto UPLOAD;                        
                    }else{
                        printf("authentication failed\n");
                        goto APPLY;
                    }
                }else {
                    printf("apply random failed\n");
                }
            }else {
                printf("random exist %s\n",randoms);
                goto SIGNATURE;
UPLOAD:
                printf("can update now!\n");
                CURLcode res = uploadLocalFile(LOCAL_WALLPAPER_FILE, uid, signature,CLOUD_WALLPAPER_FILE_NAME);
                if(CURLE_OK == res) {
                    printf("upload success!\n");
                }
            }
        } else {
            printf("prikey file not exist\n");
        }
    }else {
        system("/system/bin/radiooptions 11");
    }
}

 
int main(int argc, char** argv) {
    if(argc < 2) {
        printf("usage: backup_service [internal/sec]\n");
        return 1;
    }
    initCurl();
    int sleep_time = atoi(argv[1]);
    while(1){
        sleep(sleep_time);
        backup_action();
    }
    cleanupCurl();
    return 0;
}
