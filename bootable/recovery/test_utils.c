#include "utils.h"

extern char name[256][256];
extern char scripts[256][256];
int main() {
    int num = find_sdcard_update_script(name, scripts);
    int i = 0;
    for(; i < num; i++) {
        printf("%s %s\n",name[i], scripts[i]);
    }
    return 0;
}
