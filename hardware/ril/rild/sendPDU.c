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
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <cutils/sockets.h>

#define SOCKET_NAME_RIL_DEBUG	"rild-debug"	/* from ril.cpp */
/**
 * RIL_REQUEST_SEND_SMS
 *
 * Send an SMS message
 *
 * "data" is const char **
 * ((const char **)data)[0] is SMSC address in GSM BCD format prefixed
 *      by a length byte (as expected by TS 27.005) or NULL for default SMSC
 * ((const char **)data)[1] is SMS in PDU format as an ASCII hex string
 *      less the SMSC address
 *      TP-Layer-Length is be "strlen(((const char **)data)[1])/2"
 *
 * "response" is a const RIL_SMS_Response *
 *
 * Based on the return error, caller decides to resend if sending sms
 * fails. SMS_SEND_FAIL_RETRY means retry (i.e. error cause is 332)
 * and GENERIC_FAILURE means no retry (i.e. error cause is 500)
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE
 *  SMS_SEND_FAIL_RETRY
 *  FDN_CHECK_FAILURE
 *  GENERIC_FAILURE
 *
 * FIXME how do we specify TP-Message-Reference if we need to resend?
 */

/*Format of SMS-SUBMIT-PDU
 *SCA		PDUType	MR	DA		PID	DCS	VP		UDL	UD
 *1-12		1		1	2-12	1	1	0,1,7	1	0-140
 */

static void print_usage() {
	perror("Usage: sendPDU [PDUType] [MR] [DA] [PID] [DCS] [VP] [UDL] [UD]");
}

static int error_check(int argc, char * argv[]) {
	if( argc == 9 ) {
		return 0;
	}
	return -1;
}

static int args_to_buf(char* buf,char* argv[]){
	return snprintf(buf,1024,"%s%s%s%s%s%s%s%s",argv[1]/*PDUType*/,argv[2]/*MR*/,
			argv[3]/*DA*/,argv[4]/*PID*/,argv[5]/*DCS*/,
			argv[6]/*VP*/,argv[7]/*UDL*/,argv[8]/*UD*/);
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

	fd = socket_local_client(SOCKET_NAME_RIL_DEBUG,
			ANDROID_SOCKET_NAMESPACE_RESERVED,
			SOCK_STREAM);
	if (fd < 0) {
		perror ("opening radio debug socket");
		exit(-1);
	}

	num_socket_args = 2;
	int ret = send(fd, (const void *)&num_socket_args, sizeof(int), 0);
	if(ret != sizeof(int)) {
		perror ("Socket write error when sending num args");
		close(fd);
		exit(-1);
	}

	char* option = "11";
	int len = 2;
	ret = send(fd, &len, sizeof(int), 0);
	if (ret != sizeof(int)) {
		perror("Socket write Error: when sending arg1 length");
		close(fd);
		exit(-1);
	}

	ret = send(fd,option, sizeof(char)*len,0);
	if (ret != len * sizeof(char)) {
		perror("Socket write Error: when sending arg1");
		close(fd);
		exit(-1);
	} 

	char buf[1024];
	len = args_to_buf(buf,argv);

	ret = send(fd, &len, sizeof(int), 0);
	if (ret != sizeof(int)) {
		perror("Socket write Error: when sending arg2 length");
		close(fd);
		exit(-1);
	}

	ret = send(fd,buf, sizeof(char)*len,0);
	printf("len is %d and buf is %s\n",len, buf);
	if (ret != len * sizeof(char)) {
		perror("Socket write Error: when sending arg2");
		close(fd);
		exit(-1);
	}

	close(fd);
	return 0;
}
