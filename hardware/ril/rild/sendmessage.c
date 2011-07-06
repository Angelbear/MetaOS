/* //device/system/toolbox/resetradio.c
 **
 ** Copyright 2006, The Android Open Source Project
 **
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **     http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <cutils/sockets.h>

#define SOCKET_NAME_RECOVERY	"recovery"

enum options {
	EVENT_BEGIN_ANIMATION,
	EVENT_SET_PROGRESS,
	EVENT_PRINT_MESSAGE,
	EVENT_RESET_PROGRESS,
	EVENT_REBOOT,
	EVENT_INSTALL,
};

static void print_usage() {
	perror("Usage: sendmessage [option] [extra_socket_args]\n\
			0 - BEGIN ANIMATION, \n\
			1 - SET PROGRESS [delta progress, 0 ~ 100], [time, sec] \n\
			2 - PRINT MESSAGE [message],\n\
			3 - RESET PROGRESS, \n\
			4 - REBOOT\n\
			5 - INSTALL [update file name]\n");

}

static int error_check(int argc, char * argv[]) {
	if (argc < 2) {
		return -1;
	}
	const int option = atoi(argv[1]);
	switch(option) {
		case EVENT_BEGIN_ANIMATION:
		case EVENT_RESET_PROGRESS:
		case EVENT_REBOOT:
			if(argc == 2) return 0;
			break;
		case EVENT_PRINT_MESSAGE:
		case EVENT_INSTALL:
			if(argc == 3) return 0;
			break;
		case EVENT_SET_PROGRESS:
			if(argc == 4) return 0;
			break;
		default:
			break;
	}
	return -1;
}

int main(int argc, char *argv[])
{
	int fd;
	int num_socket_args = 0;
	int i  = 0;
	if(error_check(argc, argv)) {
		print_usage();
		exit(-1);
	}

	fd = socket_local_client(SOCKET_NAME_RECOVERY,
			ANDROID_SOCKET_NAMESPACE_ABSTRACT,
			SOCK_STREAM);
	if (fd < 0) {
		perror ("opening recovery socket error");
		exit(-1);
	}

	const int option = atoi(argv[1]);

	char cmd[256];
	
	int len = sizeof(option);
	memcpy(cmd,(void*)&option,sizeof(option));

	if(option == EVENT_SET_PROGRESS) {
		int fraction = atoi(argv[2]);
		int time = atoi(argv[3]);
		memcpy(cmd + sizeof(option), (void*)&fraction,sizeof(fraction));
		memcpy(cmd +sizeof(option) + sizeof(fraction), (void*)&time,sizeof(time));
		len += sizeof(fraction) + sizeof(time);
	} else if (option == EVENT_PRINT_MESSAGE || option == EVENT_INSTALL) {
		int message_len = strlen(argv[2]);
		memcpy(cmd + sizeof(option) , (void*)&message_len,sizeof(message_len));
		len += sizeof(message_len);
		memcpy(cmd + sizeof(option) + sizeof(message_len), (void*) argv[2], message_len);
		len += message_len;
	}
	

	int ret = send(fd, cmd, sizeof(char) * len, 0);
	if (ret != len * sizeof(char)) {
		perror ("Socket write Error: When sending command");
		close(fd);
		exit(-1);
	}

	close(fd);
	return 0;
}
