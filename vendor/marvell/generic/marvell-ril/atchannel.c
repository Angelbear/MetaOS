/* based on //device/system/reference-ril/atchannel.c
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


#include "atchannel.h"
#include "marvell-ril.h"
#include "at_tok.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "misc.h"
#include <utils/Log.h>

static pthread_t s_tid_reader;
static ATUnsolHandler s_unsolHandler;
static ATOnConfirmHandler s_onConfirmHandler;

struct channel_struct
{
	int ttyFd;
	//enum channel_state state; //maybe we do'nt need it
	//pthread_mutex_t commandMutex; //used to assure just one AT cmd is processing
	int sessionCookie;
	ATCommandType s_type;
	int isBlocked;
	char s_responsePrefix[MAX_PREFIX_LENGTH];
	char s_smsPDU[MAX_PDU_LENGTH];
	ATResponse *sp_response;
	int needResponse;
	char ATBuffer[MAX_AT_RESPONSE + 1];
	char *ATBufferCur;
};

static struct channel_struct channels[SUPPORTED_CHANNEL_NUMBER];
static pthread_mutex_t mutex[SUPPORTED_CHANNEL_NUMBER]; // = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond[SUPPORTED_CHANNEL_NUMBER]; //= PTHREAD_COND_INITIALIZER;

int sync_cmd;

static void (*s_onTimeout)(void) = NULL;
static void (*s_onReaderClosed)(void) = NULL;
static int s_readerClosed;

static void onReaderClosed();
static int writeCtrlZ(int channelID, const char *s);
static int writeline(int channelID, const char *s);
static void reverseIntermediates(ATResponse *p_response);

void  AT_DUMP(ATLogType logType, const char*  buff, int channel)
{
	if (logType == SEND_AT_CMD)
	{
		LOGD("===>>[Send AT cmd][%d] %s", channel, buff);
	}
	else if (logType == SEND_SMS_PDU)
	{
		LOGD("===>>[Send SMS PDU][%d] > %s^Z\n", channel, buff);
	}
	else if (logType == RECV_AT_CMD)
	{
		LOGD("<<====[Recv AT cmd][%d] %s", channel, buff);
	}
	else if (logType == RECV_SMS_PDU)
	{
		LOGD("<<====[Recv SMS PDU][%d] %s", channel, buff);
	}
}

static int is_all_channel_closed()
{
	int i;

	for (i = 0; i < COMMAND_CHANNEL_NUMBER; i++)
	{
		if (channels[i].ttyFd != -1) return 0;
	}
	return 1;
}

/** add an intermediate response to sp_response*/
static void addIntermediate(int channelID, const char *line)
{
	ATLine *p_new;

	p_new = (ATLine *)malloc(sizeof(ATLine));
	p_new->line = strdup(line);

	/* note: this adds to the head of the list, so the list
	   will be in reverse order of lines received. the order is flipped
	   again before passing on to the command issuer */
	p_new->p_next = channels[channelID].sp_response->p_intermediates;
	channels[channelID].sp_response->p_intermediates = p_new;
}

/**
 * returns 1 if line is a final response indicating error
 * See 27.007 annex B
 * WARNING: NO CARRIER are alyways unsolicited
 */
static const char * s_finalResponsesError[] = {
	"ERROR",
	"+CMS ERROR:",
	"+CME ERROR:",
	"NO ANSWER",
	"NO DIALTONE",
};
static int isFinalResponseError(const char *line)
{
	size_t i;

	for (i = 0; i < NUM_ELEMS(s_finalResponsesError); i++)
	{
		if (strStartsWith(line, s_finalResponsesError[i]))
		{
			return 1;
		}
	}

	return 0;
}

/**
 * returns 1 if line is a final response indicating success
 * See 27.007 annex B
 */
static const char * s_finalResponsesSuccess[] = {
	"OK",
	"CONNECT"   /* some stacks start up data on another channel */
};
static int isFinalResponseSuccess(const char *line)
{
	size_t i;

	for (i = 0; i < NUM_ELEMS(s_finalResponsesSuccess); i++)
	{
		if (strStartsWith(line, s_finalResponsesSuccess[i]))
		{
			return 1;
		}
	}

	return 0;
}

/**
 * returns 1 if line is a final response, either  error or success
 * See 27.007 annex B
 * WARNING: NO CARRIER and others are sometimes unsolicited
 */
static int isFinalResponse(const char *line)
{
	return (isFinalResponseSuccess(line) || isFinalResponseError(line));
}


/**
 * returns 1 if line is the first line in (what will be) a two-line
 * SMS unsolicited response
 */
static const char * s_smsUnsoliciteds[] = {
	"+CMT:",
	"+CDS:",
	"+CBM:"
};
static int isSMSUnsolicited(const char *line)
{
	size_t i;

	for (i = 0; i < NUM_ELEMS(s_smsUnsoliciteds); i++)
	{
		if (strStartsWith(line, s_smsUnsoliciteds[i]))
		{
			return 1;
		}
	}

	return 0;
}

/**
 * returns 1 if line is call related unsolicited msg. These msg should be sent within the call session/context,
 * so they are not sent via unsol channel
 */
static const char * s_callUnsoliciteds[] = {
	"+CRING:",
	"RING",
	"NO CARRIER",
	"+CCWA",
	"+CSSI:",
	"+CSSU:",
	"+CUSD:",
	"+CGEV:",
	"CONNECT"	
};
static int isCallUnsolicited(const char *line)
{
	size_t i;

	for (i = 0; i < NUM_ELEMS(s_callUnsoliciteds); i++)
	{
		if (strStartsWith(line, s_callUnsoliciteds[i]))
		{
			return 1;
		}
	}

	return 0;
}

/** assumes s_commandmutex is held */
static void handleFinalResponse(int channelID, const char *line)
{
	MYLOG("%s entry", __FUNCTION__);
	channels[channelID].sp_response->finalResponse = strdup(line);

	//pthread_cond_signal(&s_commandcond);
	if (channels[channelID].needResponse == 0)
	{
		MYLOG("%s: no need response", __FUNCTION__);
	}
	else
	{
		/* line reader stores intermediate responses in reverse order */
		reverseIntermediates(channels[channelID].sp_response);
	}

	if (channels[channelID].isBlocked == 1)
	{
		//pthread_cond_signal should be protected by pthread_mutex_lock, otherwise, it will cause endless waiting
		pthread_mutex_lock(&mutex[channelID]);
		pthread_cond_signal(&cond[channelID]);
		pthread_mutex_unlock(&mutex[channelID]);
	}
	else
	{
		MYLOG("%s:start to call onConfirm", __FUNCTION__);
		s_onConfirmHandler(channelID, channels[channelID].sp_response, channels[channelID].sessionCookie);

		pthread_mutex_lock(&mutex[channelID]);
		at_response_free(channels[channelID].sp_response);
		channels[channelID].sp_response = NULL;
		pthread_cond_signal(&cond[channelID]);
		pthread_mutex_unlock(&mutex[channelID]);
	}
	MYLOG("%s exit", __FUNCTION__);
}

static void handleUnsolicited(const int channelID, const char *line)
{
	if (channelID != CHANNEL_UNSOLICITED)
	{
		if ( !isCallUnsolicited(line))
			return;
	}

	if (s_unsolHandler != NULL)
	{
		s_unsolHandler(line, NULL);
	}
}

static void processLine(int channelID, const char *line)
{
	MYLOG("%s entry: process line: %s", __FUNCTION__, line);
	// NO CARRIER will be regarded as unsolicited message because all ATD will received OK after call-req-cnf
	if (channels[channelID].sp_response == NULL || strStartsWith(line, "NO CARRIER"))
	{
		/* no command pending */
		MYLOG("\t No command pending, it's unsolicited message");
		handleUnsolicited(channelID, line);
	}

	else if (isFinalResponseSuccess(line))
	{
		if(channelID != CHANNEL_DATA && strStartsWith(line, "CONNECT"))
		{
			//the CONNECT will be treated as unsolicited if it is not sent from data channel
			MYLOG("\t CONNECT on not data channel, it's unsolicited message");
			handleUnsolicited(channelID, line);
		}
		else
		{
			MYLOG("\t It's succesful final response");
			channels[channelID].sp_response->success = 1;
			handleFinalResponse(channelID, line);
		}
	}

	else if (isFinalResponseError(line))
	{
		MYLOG("\t It's failed final response end");
		channels[channelID].sp_response->success = 0;
		handleFinalResponse(channelID, line);
	}

	else if (channels[channelID].s_smsPDU[0] != '\0' && 0 == strcmp(line, "> "))
	{
		/* See eg. TS 27.005 4.3
		 * Commands like AT+CMGS have a "> " prompt
		 */
		writeCtrlZ(channelID, channels[channelID].s_smsPDU);
		channels[channelID].s_smsPDU[0] = '\0';
	}

	else
	{
		switch (channels[channelID].s_type)
		{
		case NO_RESULT:
		{
			MYLOG("\tintermediate NO RESULT, got unsolicited message");
			handleUnsolicited(channelID, line);
			break;
		}
		case NUMERIC:
		{
			MYLOG("\tintermediate NUMERIC:%s", line);
			if (channels[channelID].sp_response->p_intermediates == NULL && isdigit(line[0]))
			{
				MYLOG("\tintermediate NUMERIC add intermediate");
				addIntermediate(channelID, line);
			}
			else
			{
				/* either we already have an intermediate response or the line doesn't begin with a digit */
				MYLOG("\tintermediate NUMERIC unsolicitied");
				handleUnsolicited(channelID, line);
			}
			break;
		}
		case SINGLELINE:
		{
			MYLOG("\tintermediate SINGLELINE: line:%s, prefix:%s", line, channels[channelID].s_responsePrefix);
			if (channels[channelID].sp_response->p_intermediates == NULL && strStartsWith(line, channels[channelID].s_responsePrefix))
			{
				MYLOG("\tintermediate add");
				addIntermediate(channelID, line);
			}
			else
			{
				/* we already have an intermediate response */
				handleUnsolicited(channelID, line);
			}
			break;
		}
		case MULTILINE:
		{
			MYLOG("\tintermediate MULTILINE");
			if (strStartsWith(line, channels[channelID].s_responsePrefix))
			{
				addIntermediate(channelID, line);
			}
			else
			{
				handleUnsolicited(channelID, line);
			}
			break;
		}
		default: /* this should never be reached */
		{
			LOGE("Unsupported AT command type %d\n", channels[channelID].s_type);
			handleUnsolicited(channelID, line);
		}
		}
	}
	MYLOG("%s exit", __FUNCTION__);
	//pthread_mutex_unlock(&s_commandmutex);
}


/**
 * Returns a pointer to the end of the next line
 * special-cases the "> " SMS prompt
 *
 * returns NULL if there is no complete line
 */
static char * findNextEOL(char *cur)
{
	if (cur[0] == '>' && cur[1] == ' ' && cur[2] == '\0')
	{
		/* SMS prompt character...not \r terminated */
		return cur + 2;
	}

	// Find next newline
	while (*cur != '\0' && *cur != '\r' && *cur != '\n') cur++;

	return *cur == '\0' ? NULL : cur;
}


/**
 * Reads reply from the AT channel, save reply in channels[channelID].ATBuffer.
 * Assumes it has exclusive read access to the FD
 */
static void readChannel(int channelID)
{
	MYLOG("%s entry,channelID:%d", __FUNCTION__, channelID);
	ssize_t count;

	char *p_read = NULL;
	char *p_eol = NULL;
	char *ret;
	/* this is a little odd. I use *s_ATBufferCur == 0 to
	 * mean "buffer consumed completely". If it points to a character, than
	 * the buffer continues until a \0
	 */
	if (*channels[channelID].ATBufferCur == '\0')
	{
		/* empty buffer */
		channels[channelID].ATBufferCur = channels[channelID].ATBuffer;
		*channels[channelID].ATBufferCur = '\0';
		p_read = channels[channelID].ATBuffer;
	}
	else
	{       /* *s_ATBufferCur != '\0' */
	  /* there's data in the buffer from the last getline*/
		MYLOG("%s ATBufferCur not empty:%s", __FUNCTION__, channels[channelID].ATBufferCur);
		size_t len;
		len = strlen(channels[channelID].ATBufferCur);
		p_read = channels[channelID].ATBuffer + len;
	}

	while (p_eol == NULL)
	{
		if (0 == MAX_AT_RESPONSE - (p_read - channels[channelID].ATBuffer))
		{
			LOGE("ERROR: Input line exceeded buffer\n");
			/* ditch buffer and start over again */
			channels[channelID].ATBufferCur = channels[channelID].ATBuffer;
			*channels[channelID].ATBufferCur = '\0';
			p_read = channels[channelID].ATBuffer;
		}

		do
		{
			count = read(channels[channelID].ttyFd, p_read, MAX_AT_RESPONSE - (p_read - channels[channelID].ATBuffer));
		} while (count < 0 && errno == EINTR);

		if (count > 0)
		{
			//s_readCount += count;
			p_read[count] = '\0';

			// skip over leading newlines
			while (*channels[channelID].ATBufferCur == '\r' || *channels[channelID].ATBufferCur == '\n')
			{
				channels[channelID].ATBufferCur++;
			}

			p_eol = findNextEOL(channels[channelID].ATBufferCur);
			p_read += count;
		}
		else if (count <= 0)
		{
			/* read error encountered or EOF reached */
			if (count == 0)
			{
				LOGE("atchannel: EOF reached");
			}
			else
			{
				LOGE("atchannel: read error %s", strerror(errno));
			}
			return;
		}
	}
	return;
}

/* get line from channel[channelID].ATBuffer
 * if not complete line in ATBuffer, return NULL.
 */
static const char *getline(int channelID)
{
	MYLOG("%s entry,channelID:%d", __FUNCTION__, channelID);
	ssize_t count;
	char *p_eol = NULL;
	char *ret;

	if (*channels[channelID].ATBufferCur == '\0') return NULL;

	// skip over leading newlines
	while (*channels[channelID].ATBufferCur == '\r' || *channels[channelID].ATBufferCur == '\n')
	{
		channels[channelID].ATBufferCur++;
	}
	p_eol = findNextEOL(channels[channelID].ATBufferCur);

	if (p_eol == NULL)
	{
		MYLOG("%s partial line meet:%s", __FUNCTION__, channels[channelID].ATBufferCur);
		/* a partial line. move it up and prepare to read more */
		size_t len;
		len = strlen(channels[channelID].ATBufferCur);

		memmove(channels[channelID].ATBuffer, channels[channelID].ATBufferCur, len + 1);
		channels[channelID].ATBufferCur = channels[channelID].ATBuffer;
		return NULL;
	}
	else
	{
		/* a full line in the buffer. Place a \0 over the \r and return */
		ret = channels[channelID].ATBufferCur;
		*p_eol = '\0';
		channels[channelID].ATBufferCur = p_eol + 1; /* this will always be <= p_read,    */
		/* and there will be a \0 at *p_read */
		while (*channels[channelID].ATBufferCur == '\r' || *channels[channelID].ATBufferCur == '\n')
		{
			channels[channelID].ATBufferCur++;
		}
		return ret;
	}
}

static void onReaderClosed()
{
	if (s_onReaderClosed != NULL && s_readerClosed == 0)
	{
		// pthread_mutex_lock(&s_commandmutex);
		// pthread_cond_signal(&s_commandcond);
		// pthread_mutex_unlock(&s_commandmutex);
		s_readerClosed = 1;
		s_onReaderClosed();
	}
}

static int getChannelID(int fd)
{
	int i;

	for (i = 0; i < COMMAND_CHANNEL_NUMBER; i++)
	{
		if (channels[i].ttyFd == fd) return i;
	}
	return -1;
}

static int channelReader(int channelID)
{
	const char * line;

	readChannel(channelID);
	while (1)
	{
		line = getline(channelID);
		if (line == NULL) return 0;
		AT_DUMP(RECV_AT_CMD, line, channelID);

		if (isSMSUnsolicited(line))
		{
			char *line1;
			const char *line2;
			MYLOG("found sms unsolicited on ChannelReader");

			// The scope of string returned by 'readline()' is valid only
			// till next call to 'readline()' hence making a copy of line
			// before calling readline again.
			line1 = strdup(line);
			line2 = getline(channelID);
			AT_DUMP(RECV_SMS_PDU, line2, channelID);

			while (line2 == NULL)
			{
				readChannel(channelID);
				line2 = getline(channelID);
				AT_DUMP(RECV_SMS_PDU, line2, channelID);
			}

			if ((s_unsolHandler != NULL)  && (channelID == CHANNEL_UNSOLICITED))
			{
				s_unsolHandler(line1, line2);
			}
			free(line1);
		}

		else
		{
			processLine(channelID, line);
		}
	}
	return 0;
}

static void *readerLoop(void *arg)
{
	/* Create fdset to be listened to */
	int maxfd = 0, readSuccess;
	int channelID, i;
	fd_set fdset, readset;

	FD_ZERO(&fdset);
	for (i = 0; i < COMMAND_CHANNEL_NUMBER; i++)
	{
		if (channels[i].ttyFd > maxfd)
		{
			maxfd = channels[i].ttyFd;
		}

		if (channels[i].ttyFd != -1)
		{
			FD_SET(channels[i].ttyFd, &fdset);
		}
	}

	/* loop to read multi-channel */
	for (;; )
	{
		/* read multi-channel by select() */
		readset = fdset;
		do {
			i = select(maxfd + 1, &readset, NULL, NULL, NULL);
		} while (i < 0 && errno == EINTR);

		if (i < 0) break;

		/* check which fd is readable, then read it by readline(fd) */
		readSuccess = 1;
		for (i = 0; i < COMMAND_CHANNEL_NUMBER; i++)
		{
			if (channels[i].ttyFd != -1)
			{
				if (FD_ISSET(channels[i].ttyFd, &readset))
				{
					channelID = getChannelID(channels[i].ttyFd);
					int ret = channelReader(channelID);
					if (ret < 0)
					{
						readSuccess = 0;
						break;
					}
				}
			}
		}
		if (readSuccess == 0) break;
	}

	/* exit loop, and thread quit,
	 * callback s_onReaderClosed, which is set at_set_on_reader_closed, is called
	 */
	onReaderClosed();

	return NULL;
}

/**
 * Sends string s to the radio with a \r appended.
 * Returns AT_ERROR_* on error, 0 on success
 *
 * This function exists because as of writing, android libc does not
 * have buffered stdio.
 */
static int writeline (int channelID, const char *s)
{
	size_t cur = 0;
	size_t len = strlen(s);
	ssize_t written;
	int s_fd = channels[channelID].ttyFd;

	if (s_fd < 0 || s_readerClosed > 0)
	{
		return AT_ERROR_CHANNEL_CLOSED;
	}

	AT_DUMP( SEND_AT_CMD, s, channelID );

	/* the main string */
	while (cur < len)
	{
		do {
			written = write(s_fd, s + cur, len - cur);
		} while (written < 0 && errno == EINTR);

		if (written < 0)
		{
			return AT_ERROR_GENERIC;
		}

		cur += written;
	}

	/* the \r  */

	do {
		written = write(s_fd, "\r", 1);
	} while ((written < 0 && errno == EINTR) || (written == 0));

	if (written < 0) return AT_ERROR_GENERIC;

	return 0;
}

static int writeCtrlZ (int channelID, const char *s)
{
	size_t cur = 0;
	size_t len = strlen(s);
	ssize_t written;
	int s_fd = channels[channelID].ttyFd;

	if (s_fd < 0 || s_readerClosed > 0) return AT_ERROR_CHANNEL_CLOSED;

	AT_DUMP( SEND_AT_CMD, s, channelID );

	/* the main string */
	while (cur < len)
	{
		do {
			written = write(s_fd, s + cur, len - cur);
		} while (written < 0 && errno == EINTR);

		if (written < 0)
		{
			return AT_ERROR_GENERIC;
		}

		cur += written;
	}

	/* the ^Z  */
	do {
		written = write(s_fd, "\032", 1);
	} while ((written < 0 && errno == EINTR) || (written == 0));

	if (written < 0) return AT_ERROR_GENERIC;

	return 0;
}

/**
 * returns 0 on success, -1 on error
 */
int at_channel_init(ATUnsolHandler h1, ATOnConfirmHandler h2)
{
	int ret;
	pthread_t tid;
	pthread_attr_t attr;

	s_unsolHandler = h1;
	s_onConfirmHandler = h2;
	s_readerClosed = 0;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	ret = pthread_create(&s_tid_reader, &attr, readerLoop, &attr);

	if (ret < 0)
	{
		perror("pthread_create");
		return -1;
	}

	return 0;
}

/**
 * Starts AT handler on stream "fd'
 * returns 0 on success, -1 on error
 */
int at_channel_open(int channelID, int fd)
{
	int ret;

	channels[channelID].ttyFd = fd;
	channels[channelID].s_responsePrefix[0] = '\0';
	channels[channelID].s_smsPDU[0] = '\0';
	channels[channelID].sp_response = NULL;
	//channels[channelID].state = CHANNEL_IDLE;
	channels[channelID].ATBufferCur = channels[channelID].ATBuffer;

	if (!pthread_mutex_init(&mutex[channelID], NULL) || !pthread_cond_init(&cond[channelID], NULL))
	{
		return -1;
	}
	else
	{
		return 0;
	}
}
/* FIXME is it ok to call this from the reader and the command thread? */
void at_channel_close(int channelID)
{
	if (channels[channelID].ttyFd >= 0)
	{
		close(channels[channelID].ttyFd);
	}

	channels[channelID].ttyFd = -1;

	if (is_all_channel_closed() == 1)
	{
		s_readerClosed = 1;
	}
}

static ATResponse * at_response_new()
{
	return (ATResponse *)calloc(1, sizeof(ATResponse));
}

/* It is called manually by user only in syn case. For asyn case it is auto released in handleFinalResponse() */
void at_response_free(ATResponse *p_response)
{
	ATLine *p_line;

	if (p_response == NULL) return;

	p_line = p_response->p_intermediates;

	while (p_line != NULL)
	{
		ATLine *p_toFree;

		p_toFree = p_line;
		p_line = p_line->p_next;

		free(p_toFree->line);
		free(p_toFree);
	}

	free(p_response->finalResponse);
	free(p_response);
}

/**
 * The line reader places the intermediate responses in reverse order
 * here we flip them back
 */
static void reverseIntermediates(ATResponse *p_response)
{
	ATLine *pcur, *pnext;

	pcur = p_response->p_intermediates;
	p_response->p_intermediates = NULL;

	while (pcur != NULL)
	{
		pnext = pcur->p_next;
		pcur->p_next = p_response->p_intermediates;
		p_response->p_intermediates = pcur;
		pcur = pnext;
	}
}


int at_send_command_sync (int channelID, const char *command, ATCommandType type,
			  const char *responsePrefix, const char *smspdu, ATResponse **pp_outResponse)
{
	MYLOG("at_send_command_sync entry");
	int err = 0;

	pthread_mutex_lock(&mutex[channelID]);
	while (channels[channelID].sp_response != NULL)
	{
		/*Fixme: If AT CMD Server Crash and restart, here will never return */
		err = pthread_cond_wait(&cond[channelID], &mutex[channelID]);
	}

	if (pp_outResponse == NULL)
	{
		channels[channelID].needResponse = 0;
	}
	else
	{
		channels[channelID].needResponse = 1;
	}

	channels[channelID].s_type = type;
	channels[channelID].isBlocked = 1;
	if (responsePrefix != NULL)
	{
		strcpy(channels[channelID].s_responsePrefix, responsePrefix);
	}
	if (smspdu != NULL)
	{
		strcpy(channels[channelID].s_smsPDU, smspdu);
	}

	channels[channelID].sp_response = at_response_new();
	err = writeline(channelID, command);
	if (err < 0) goto error;

	err = pthread_cond_wait(&cond[channelID], &mutex[channelID]);
	if (err < 0) goto error;

	if (s_readerClosed > 0)
	{
		err = AT_ERROR_CHANNEL_CLOSED;
		goto error;
	}

	if (pp_outResponse != NULL)
	{
		*pp_outResponse = channels[channelID].sp_response;
	}
	else
		at_response_free(channels[channelID].sp_response);

	channels[channelID].sp_response = NULL;

	MYLOG("at_send_command_sync succ exit");
	pthread_mutex_unlock(&mutex[channelID]);
	return err;

 error:
	at_response_free(channels[channelID].sp_response);
	channels[channelID].sp_response = NULL;

	MYLOG("at_send_command_sync failure exit");
	pthread_mutex_unlock(&mutex[channelID]);
	return err;
}

int at_send_command (int channelID, const char *command, ATCommandType type,
		     const char *responsePrefix, const char *smspdu, int needResponse, int cookie)
{
	int err = 0;

	MYLOG("entry at_send_command");

	pthread_mutex_lock(&mutex[channelID]);

	/* There is still async AT cmd waiting for response, so pending here */
	while (channels[channelID].sp_response != NULL)
	{
		/*Fixme: If AT CMD Server Crash and restart, here will never return */
		err = pthread_cond_wait(&cond[channelID], &mutex[channelID]);
	}

	MYLOG("at_send_command (asyn) begin to exe");
	channels[channelID].needResponse = needResponse;
	channels[channelID].s_type = type;
	channels[channelID].sessionCookie = cookie;
	channels[channelID].isBlocked = 0;

	if (responsePrefix != NULL)
	{
		strcpy(channels[channelID].s_responsePrefix, responsePrefix);
	}
	if (smspdu != NULL)
	{
		strcpy(channels[channelID].s_smsPDU, smspdu);
	}

	channels[channelID].sp_response = at_response_new();
	if (channels[channelID].sp_response == NULL) {
		LOGE("*** Allocate buffer for sp_response failed. ***");
		pthread_mutex_unlock(&mutex[channelID]);
		return -1;
	}

	err = writeline(channelID, command);
	if (err < 0) goto error;


	MYLOG("exit at_send_command succ");
	pthread_mutex_unlock(&mutex[channelID]);
	return err;

 error:
	at_response_free(channels[channelID].sp_response);
	channels[channelID].sp_response = NULL;
	MYLOG("exit at_send_command fail");
	pthread_mutex_unlock(&mutex[channelID]);
	return err;
}

//Johnny: need to consider when to call it?
/** This callback is invoked on the command thread */
void at_set_on_timeout(void (*onTimeout)(void))
{
	s_onTimeout = onTimeout;
}

/**
 *  This callback is invoked on the reader thread (like ATUnsolHandler)
 *  when the input stream closes before you call at_close
 *  (not when you call at_close())
 *  You should still call at_close()
 */
void at_set_on_reader_closed(void (*onClose)(void))
{
	s_onReaderClosed = onClose;
}

/**
 * Returns error code from response
 * Assumes AT+CMEE=1 (numeric) mode
 */
AT_CME_Error at_get_cme_error(const ATResponse *p_response)
{
	int ret;
	int err;
	char *p_cur;

	if (p_response->success > 0) return CME_SUCCESS;

	if (p_response->finalResponse == NULL || !strStartsWith(p_response->finalResponse, "+CME ERROR:"))
	{
		return CME_ERROR_NON_CME;
	}

	p_cur = p_response->finalResponse;
	err = at_tok_start(&p_cur);
	if (err < 0)
	{
		return CME_ERROR_NON_CME;
	}

	err = at_tok_nextint(&p_cur, &ret);
	if (err < 0)
	{
		return CME_ERROR_NON_CME;
	}

	return (AT_CME_Error)ret;
}

#ifdef BROWNSTONE_CP
int at_switch_data_mode(int channelID, const char *cmd, ATResponse **pp_outResponse)
{
	int err = -1;
	const char *line;
	int fd = -1;

	fd = open(descriptions[channelID].ttyName, O_RDWR);
	if (fd >= 0)
	{
		descriptions[channelID].fd = fd;
		LOGI("AT channel [%d] open successfully, ttyName:%s", channelID, descriptions[channelID].ttyName );
	}

	at_channel_open(descriptions[channelID].channelID, fd);

	err = writeline(channelID, cmd);
	if (err < 0)
	{
		return -1;
	}

	channels[channelID].sp_response = at_response_new();
	*pp_outResponse = channels[channelID].sp_response;
	while(1)
	{
		readChannel(channelID);
		while(1)
		{
			line = getline(channelID);
			if (line == NULL)
			{
				break;
			}
			else if(isFinalResponseSuccess(line))
			{
				channels[channelID].sp_response->success = 1;
				return 0;
			}
			else if(isFinalResponseError(line))
			{
				channels[channelID].sp_response->success = 0;
				return 0;
			}
		}
	}
}
#endif

