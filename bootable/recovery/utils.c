#include <stdio.h>
#include <stdlib.h>
#include"utils.h"
#include <regex.h>
#include <dirent.h>
#ifndef __LINUX__
#include"cutils/properties.h"
#endif
#define NON_NUM '0' 

char Char2Num(char ch){ 
    if(ch>='0' && ch<='9')return (char)(ch-'0'); 
    if(ch>='a' && ch<='f')return (char)(ch-'a'+10); 
    if(ch>='A' && ch<='F')return (char)(ch-'A'+10); 
    return NON_NUM; 
} 

int URLEncode(const char* str, const int strSize, char* result, const int resultSize) { 
    int i; 
    int j = 0; /* for result index */ 
    char ch; 

    if ((str == NULL) || (result == NULL) || (strSize <= 0) || (resultSize <= 0)) { 
        return 0; 
    } 

    for (i=0; (i<strSize) && (j<resultSize); i++) { 
        ch = str[i]; 
        if ((ch >= 'A') && (ch <= 'Z')) { 
            result[j++] = ch; 
        } else if ((ch >= 'a') && (ch <= 'z')) { 
            result[j++] = ch; 
        } else if ((ch >= '0') && (ch <= '9')) { 
            result[j++] = ch; 
        } else if(ch == ' '){ 
            result[j++] = '+'; 
        } else { 
            if (j + 3 < resultSize) { 
                sprintf(result+j, "%%%02X", (unsigned char)ch); 
                j += 3; 
            } else { 
                return 0; 
            } 
        } 
    } 

    result[j] = '\0'; 
    return j; 
} 

#define PROPERTY_RIL_IMSI "gsm.info.imsi"

int is_imsi_got(char* imsi) {
#ifndef __LINUX__
    property_get(PROPERTY_RIL_IMSI, imsi, "460001843103570");
    //   if(!strcmp(imsi,"null")) {
    //       return 0;
    //   }
#else
    strncpy(imsi,"460001843103570",16);
#endif
    return 1;
}

int is_internet_connected() {
#ifndef __LINUX__    
    char gateway[256];                           
    property_get("net.ccinet0.gw",gateway,"1");  
    if(strlen(gateway) ==1) {                    
        property_get("net.mlan0.gw",gateway,"1");
        if(strlen(gateway) ==1) {                
            return 0;                            
        }                                        
    }            
#endif    
    return 1;                                    
}                                                

extern char *path_cat (const char *str1, char *str2);


char name[256][256] = { NULL };
char scripts[256][256] = { NULL};
static char* UPDATE_SCRIPT_REGEX="(.*).sh";
int find_sdcard_update_script() {
    struct dirent *dp;
    regex_t reg;
    regmatch_t pm[10];
    const size_t nmatch = 10;
    int ret = regcomp(&reg, UPDATE_SCRIPT_REGEX, REG_EXTENDED);
    if(ret != 0) {
        return 0;
    }

    int result = 0;
#ifndef __LINUX__
    const char *dir_path="/sdcard/update/";
#else
    const char* dir_path="./test_dir/";
#endif
    DIR *dir = opendir(dir_path);
    while ((dp=readdir(dir)) != NULL) {
        char *tmp;
        tmp = path_cat(dir_path, dp->d_name);
        ret = regexec(&reg, dp->d_name, nmatch, pm, 0);

        if(ret == 0){
            strncpy(scripts[result] , tmp, strlen(tmp));
            strncpy(name[result] , dp->d_name+pm[1].rm_so, pm[1].rm_eo - pm[1].rm_so);
            result++;
        }
        free(tmp);
        tmp = NULL;

    }
    regfree(&reg);
    closedir(dir);
    return result;
}

