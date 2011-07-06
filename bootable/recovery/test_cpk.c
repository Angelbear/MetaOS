/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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


static void enter_remote_update() {
    char imsi[256],filename[256],uid[64];
    memset(imsi, 0, sizeof(imsi));
    memset(filename, 0, sizeof(filename));
    memset(uid, 0, sizeof(uid));
    if(!is_imsi_got(imsi)) {
        printf("IMSI not got, please check your sim card\n");
        goto END;
    }

    // TODO:

    if(!is_priKey_exist(imsi,filename,uid)) {
        printf("priKey not exist\n");
        char pri[1024];
        memset(pri, 0, sizeof(pri));
        if(check_register(imsi,pri,uid)) {
            printf("check register sucess\n");
            char xrandom[64]; 
            snprintf(filename,sizeof(filename),"%s_%s_cpk.pem",imsi,uid);
            save_priKey(pri,filename);
GET_PRI:
            printf("uid:%s\n",uid);
            char command[256], ret[256], signature[256];
            memset(command, 0, sizeof(command));
            memset(ret, 0, sizeof(ret));
            memset(signature, 0, sizeof(signature));
            memset(xrandom,0,sizeof(xrandom));
            snprintf(command, sizeof(command), "./cpk -set-identity %s",uid);
            system(command);
            printf("set identity success\n");
            snprintf(command, sizeof(command), "./cpk -import-sign-key -in %s -pass %s%s", filename, imsi ,"0123456789abcdefghijklmnopqrstuv");
            system(command);
            printf("import sign key success\n");
            if(apply_random(uid,xrandom)) {
                printf("applied random number %s\n",xrandom);
                snprintf(command, sizeof(command), "echo %s | head -c %d | ./cpk -sign -pass %s%s >  .signature", xrandom,strlen(xrandom), imsi, "0123456789abcdefghijklmnopqrstuv");
                system(command);
                FILE* stream = fopen(".signature","r");
                int size = 0;
                if(stream != NULL) {
                    size = fread( ret, sizeof(char), sizeof(ret), stream);
                }
                fclose(stream);
                if(size <= 0) {
                    printf("read sign result failed\n");
                    goto END;
                }		
                ret[size] = 0;
                printf("signature raw string is %s\n",ret);
                size = URLEncode(ret,size,signature,sizeof(signature));
                signature[size - 3] = 0;
                printf("encoded signature is %s\n",signature);
                if(cpk_authentication(uid,signature) == CA_TRUE) {
                    // retrieve preferences
                    printf("authentication success\n");
                }else {
                    printf("authentication failed\n");
                }
            }else {
                printf("apply random failed\n");
            }
        }else {
            printf("auto reboot\n");
        }
    }else {
        goto GET_PRI;
    }

END:
    return;
}

int
main(int argc, char **argv) {
    printf("start test\n");
    system("./cpk -import-param -in public_params.der");
    //cpktool_import_params("/system/etc/cpk/public_params.der");
    enter_remote_update();
    return 0;
}
