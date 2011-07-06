#include "getopt.h"
#include "limits.h"            
#include "stdio.h"             
#include "stdlib.h"            
#include "string.h"            
#include "pthread.h" 
#include "bootloader.h"        
#include "sys/reboot.h"
#include "sys/socket.h"
#include "cutils/properties.h" 
#include "cutils/sockets.h"
#include "cutils/record_stream.h"
#include "utils/Log.h"
#include "install.h"           
#include "minui/minui.h"       
#include "minzip/DirUtil.h"    
#include "roots.h"             
#include "recovery_ui.h"       
#include "socket_server.h"
#include "netinet/in.h"
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <alloca.h>
#include <sys/un.h>

#define SOCKET_NAME_RECOVERY "recovery"
#define MAX_COMMAND_BYTES (8 * 1024)

static int s_fdListen = -1;
static int s_fdCommand = -1;
static pthread_t s_tid_dispatch;

enum options {
	BEGIN_ANIMATION,
	SET_PROGRESS,
	PRINT_MESSAGE,
	RESET_PROGRESS,
	REBOOT,
	INSTALL,
};

static int process_cmd(void *buffer, size_t buflen) {
	/*
	 * Command Format
	 * begin/end animation:int type
	 * set progress:int type, int progress [0 ~ 100]
	 * print message:int type, int messageLen, char* message (end with '\0')
	 */
	if(buflen < sizeof(int)) {
		//LOGE("Command too short errno:%d\n", errno);
	}

	int* int_buffer = (int*) buffer;
	switch(int_buffer[0]) {
		case BEGIN_ANIMATION:
			//LOGE("Command: begin animation\n");
			ui_set_background(BACKGROUND_ICON_INSTALLING);
			ui_show_progress(0.0f,VERIFICATION_PROGRESS_TIME);
			break;
		case SET_PROGRESS:
			//LOGE("Commmand: set progress\n");
			if(buflen < sizeof(int) * 3) {
				LOGE("Params too few errno:%d\n", errno);
				break;
			}
			int progress = int_buffer[1];
			if( progress < 0 || progress > 100) {
				LOGE("Progress value is not valid errno:%d\n",errno);
				break;
			}
			float fraction = progress / 100.0f;
			int time = int_buffer[2];
			ui_show_progress(fraction,time);
			break;
		case PRINT_MESSAGE:
			//LOGE("Command: print message\n");
			if(buflen < sizeof(int) * 2) {
				//LOGE("Params too few errno:%d\n",errno);
				break;
			}
			int messageLen = int_buffer[1];
			if(messageLen < 0 || messageLen > buflen - 2 * sizeof(int)) {
				//LOGE("Invalid message length or message too short errno:%d\n",errno);
				break;
			}
			char message[256];
			memset(message,0,sizeof(message));
			memcpy(message,(char*)&int_buffer[2],messageLen);
			ui_print(message);
			ui_print("\n");
			break;
		case RESET_PROGRESS:
			//LOGE("Command: end animation\n");
			ui_reset_progress();
			ui_show_indeterminate_progress();
			break;
		case REBOOT:
			//LOGE("Command: reboot\n");
			reboot(RB_AUTOBOOT);
			break;
		case INSTALL:
			if(buflen < sizeof(int) * 2) {
				break;
			}
			int fileNameLen = int_buffer[1];
			if(fileNameLen < 0 || fileNameLen > buflen - 2 * sizeof(int)) {
				break;
			}
			char fileName[256];
			memset(fileName,0,sizeof(fileName));
			memcpy(fileName,(char*)&int_buffer[2],fileNameLen);
			int status = install_package(fileName);
			if(status == INSTALL_SUCCESS) {
				ui_print("install package success\n");
			}else {
				ui_print("install package failed\n");
			}
			break;
		default:
			//LOGE("Invalid command errno:%d\n",errno);
			break;
	}
	return 0;
}

static void dump_command(char* buf, size_t len) {
	LOGE("command len is %d\n",len);
	int i = 0;
	while(i < len) {
		LOGE("0x%02x ",buf[i]);
		i++;
	}
}

static void event_loop(){
	int ret = socket_local_server (SOCKET_NAME_RECOVERY,
			ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);

	if (ret < 0) {
		LOGE("Unable to bind socket errno:%d\n", errno);
		exit (-1);
	}

	//LOGE("Create server socket successfully\n");

	s_fdListen = ret;

	ret =  listen(s_fdListen, 4); 

	if (ret < 0) {
		LOGE("Failed to listen on control socket '%d': %s\n",
		     s_fdListen, strerror(errno));
		return;
	}

	//LOGE("Listen to control socket successfully\n");

	//RecordStream* p_rs;

	for(;;){
		s_fdCommand = accept(s_fdListen, NULL, NULL);
		if (s_fdCommand < 0 ) {
			LOGE("Accept socket errno:%d\n", errno);
			return;
		}

		//LOGE("Accept socket %d successfully\n",s_fdCommand);

		//p_rs = record_stream_new(s_fdCommand, MAX_COMMAND_BYTES);

		char p_record[256];
		size_t recordlen;

		//recordLen = recv(s_fdCommand, p_record,sizeof(p_record),0);

		for(;;){
			//ret = record_stream_get_next(p_rs, &p_record, &recordlen);
			recordlen = recv(s_fdCommand, p_record,sizeof(p_record),0);
			if (recordlen <= 0) {
				//LOGE("None command received errno:%d\n", errno);
				break;
			} else { 
				//dump_command((char*)p_record, recordlen);
				process_cmd(p_record, recordlen);
			}
		}
		close(s_fdCommand);
		s_fdCommand = -1;
		//record_stream_free(p_rs);

	}
	return;
}

int init_server(){
	pthread_attr_t attr;
	pthread_attr_init (&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	int	ret = pthread_create(&s_tid_dispatch, &attr, event_loop, NULL);
	if(ret < 0) {
		return INSTALL_ERROR;
	}
	return INSTALL_SUCCESS;
	
	//event_loop(); // never goes to return
	//return INSTALL_SUCCESS;
}
