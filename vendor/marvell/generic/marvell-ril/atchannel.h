/* //device/system/reference-ril/atchannel.h
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
/******************************************************************************
 * *(C) Copyright 2008 Marvell International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/


#ifndef ATCHANNEL_H
#define ATCHANNEL_H 1

#ifdef __cplusplus
extern "C" {
#endif

//#define MYLOG LOGD
#define MYLOG LOGV

#define NUM_ELEMS(x) (sizeof(x) / sizeof(x[0]))

typedef enum {
	RECV_AT_CMD,
	RECV_SMS_PDU,
	SEND_AT_CMD,
	SEND_SMS_PDU
} ATLogType;

typedef enum {
	SERVICE_CC,
	SERVICE_DEV,
	SERVICE_MM,
	SERVICE_MSG,
	SERVICE_PS,
	SERVICE_SIM,
	SERVICE_SS,
	SERVICE_UNSOL,
	SERVICE_TOTAL
} ServiceType;

#ifdef DKB_CP
#define CHANNEL_UNSOLICITED   SERVICE_UNSOL
#define CHANNEL_DATA   SERVICE_PS
#define SUPPORTED_CHANNEL_NUMBER  SERVICE_TOTAL
#define SERVICE_NULL  SERVICE_TOTAL
#define COMMAND_CHANNEL_NUMBER  SERVICE_TOTAL
#elif defined BROWNSTONE_CP
typedef enum {
	CHANNEL_CMD,
	CHANNEL_DAT,
	CHANNEL_TOTAL
} ChannelType;
#define CHANNEL_UNSOLICITED   CHANNEL_CMD
#define SUPPORTED_CHANNEL_NUMBER  CHANNEL_TOTAL
#define CHANNEL_DATA  CHANNEL_CMD
#define SERVICE_NULL  SERVICE_TOTAL
#define COMMAND_CHANNEL_NUMBER (CHANNEL_TOTAL-1)
#endif

void  AT_DUMP(ATLogType logType, const char*  buff, int channel);

#define AT_ERROR_GENERIC -1
#define AT_ERROR_COMMAND_PENDING -2
#define AT_ERROR_CHANNEL_CLOSED -3
#define AT_ERROR_TIMEOUT -4
/* AT commands may not be issued from
   reader thread (or unsolicited response
   callback */
#define AT_ERROR_INVALID_THREAD -5
/* eg an at_send_command_singleline that
   did not get back an intermediate
   response */
#define AT_ERROR_INVALID_RESPONSE -6

typedef enum {
	NO_RESULT,      /* no intermediate response expected */
	NUMERIC,        /* a single intermediate response starting with a 0-9 */
	SINGLELINE,     /* a single intermediate response starting with a prefix */
	MULTILINE /* multiple line intermediate response
		    starting with a prefix */
} ATCommandType;

/** a singly-lined list of intermediate responses */
typedef struct ATLine {
	struct ATLine *p_next;
	char *line;
} ATLine;

/** Free this with at_response_free() */
typedef struct {
	int success;          /* true if final response indicates
				    success (eg "OK") */
	char *finalResponse;            /* eg OK, ERROR */
	ATLine  *p_intermediates;       /* any intermediate responses */
} ATResponse;

/**
 * a user-provided unsolicited response handler function
 * this will be called from the reader thread, so do not block
 * "s" is the line, and "sms_pdu" is either NULL or the PDU response
 * for multi-line TS 27.005 SMS PDU responses (eg +CMT:)
 */
typedef void (*ATUnsolHandler)(const char *s, const char *sms_pdu);

typedef void (*ATOnConfirmHandler)(int channelID, ATResponse* response, int cookie);

int at_channel_init(ATUnsolHandler h1, ATOnConfirmHandler h2);
int at_channel_open(int channelID, int fd);
void at_channel_close(int channelID);

/* This callback is invoked on the command thread.
   You should reset or handshake here to avoid getting out of sync */
void at_set_on_timeout(void (*onTimeout)(void));
/* This callback is invoked on the reader thread (like ATUnsolHandler)
   when the input stream closes before you call at_close
   (not when you call at_close())
   You should still call at_close()
   It may also be invoked immediately from the current thread if the read
   channel is already closed */
void at_set_on_reader_closed(void (*onClose)(void));


int at_send_command(int channelID, const char *command, ATCommandType type,
		    const char *responsePrefix, const char *smspdu,
		    int needResponse, int cookie);

int at_send_command_sync(int channelID, const char *command, ATCommandType type,
			 const char *responsePrefix, const char *smspdu,
			 ATResponse **pp_outResponse);

void at_response_free(ATResponse *p_response);

int at_switch_data_mode(int channelID, const char *cmd, ATResponse **pp_outResponse);

typedef enum {
	CME_ERROR_NON_CME = -1,
	CME_SUCCESS = 0,
	CME_SIM_NOT_INSERTED  = 10,
	CME_SIM_UNKNOWN_ERROR = 100,
} AT_CME_Error;

AT_CME_Error at_get_cme_error(const ATResponse *p_response);

#ifdef __cplusplus
}
#endif

#endif /*ATCHANNEL_H*/
