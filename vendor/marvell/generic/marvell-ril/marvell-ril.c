/******************************************************************************
 * *(C) Copyright 2008 Marvell International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/

#include "marvell-ril.h"
#include <cutils/properties.h>

/* Global varialble used in ril layer */
RIL_RadioState sState = RADIO_STATE_UNAVAILABLE;
int sFirstGet = 1;  //default sFirstGet = TRUE

pthread_mutex_t s_state_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t s_state_cond = PTHREAD_COND_INITIALIZER;

int s_closed = 0;  /* trigger change to this with s_state_cond */
static int s_newrilconnection = 1; /*used to identify whether it is new RIL connection between RILD and App telephony*/
extern int AfterCPReset;

const struct timeval TIMEVAL_0 = { 0, 0 };

/* I/F implemented in ril.cpp */
extern const char * requestToString(int request);

/* I/F implemented in ril-xxx.c */
int onRequest_cc(int request, void *data, size_t datalen, RIL_Token token, char *cmdString, ATCommandType *pType, char *prefix, requestCallback *pCallback);
int onRequest_dev(int request, void *data, size_t datalen, RIL_Token token, char *cmdString, ATCommandType *pType, char *prefix, requestCallback *pCallback);
int onRequest_mm(int request, void *data, size_t datalen, RIL_Token token, char *cmdString, ATCommandType *pType, char *prefix, requestCallback *pCallback);
int onRequest_msg(int request, void *data, size_t datalen, RIL_Token token, char *cmdString, char *smsPdu, ATCommandType *pType, char *prefix, requestCallback *pCallback);
int onRequest_ps(int request, void *data, size_t datalen, RIL_Token token, char *cmdString, ATCommandType *pType, char *prefix, requestCallback *pCallback);
int onRequest_sim(int request, void *data, size_t datalen, RIL_Token token, char *cmdString, ATCommandType *pType, char *prefix, requestCallback *pCallback);
int onRequest_ss(int request, void *data, size_t datalen, RIL_Token token, char *cmdString, ATCommandType *pType, char *prefix, requestCallback *pCallback);
void onUnsolicited_cc(const char *s);
void onUnsolicited_dev(const char *s);
void onUnsolicited_mm(const char *s);
void onUnsolicited_msg(const char *s, const char *smsPdu);
void onUnsolicited_ps(const char *s);
void onUnsolicited_sim(const char *s);
void onUnsolicited_ss(const char *s);
int onConfirm_cc(ATResponse* response, struct requestSession* session);
int onConfirm_dev(ATResponse* response, struct requestSession* session);
int onConfirm_mm(ATResponse* response, struct requestSession* session);
int onConfirm_msg(ATResponse* response, struct requestSession* session);
int onConfirm_ps(ATResponse* response, struct requestSession* session);
int onConfirm_sim(ATResponse* response, struct requestSession* session);
int onConfirm_ss(ATResponse* response, struct requestSession* session);

void reportSignalStrength(void *param);
void setNetworkStateReportOption(int flag);
int getSimStatus(void);
void updateRadioState(void);
void InitPbkAndStk(void* param);

/* static I/F which will be called by Android upper layer */
static void onRequest(int request, void *data, size_t datalen, RIL_Token token);
static RIL_RadioState onCurrentState();
static int onSupports(int requestCode);
static void onCancel(RIL_Token token);
static void onConfirm(int channelID, ATResponse* response, int cookie);
static const char *onGetVersion();

/* Internal function declaration */
static void processNextRequest(void* param);
static void onUnsolicited(const char *s, const char *smsPdu);

/*** Static Variables ***/
static const RIL_RadioFunctions s_callbacks = {
	RIL_VERSION,
	onRequest,
	onCurrentState,
	onSupports,
	onCancel,
	onGetVersion
};

#define DATABITS       CS8
#define BAUD          B115200
#define STOPBITS        0
#define PARITYON        0
#define PARITY            0

#ifdef DKB_CP
struct channel_description descriptions[SUPPORTED_CHANNEL_NUMBER] = {
	{ SERVICE_CC,	 -1,   "/dev/citty0", NULL, CHANNEL_IDLE, PTHREAD_MUTEX_INITIALIZER },
	{ SERVICE_DEV,	 -1,   "/dev/citty1", NULL, CHANNEL_IDLE, PTHREAD_MUTEX_INITIALIZER },
	{ SERVICE_MM,	 -1,   "/dev/citty2", NULL, CHANNEL_IDLE, PTHREAD_MUTEX_INITIALIZER },
	{ SERVICE_MSG,	 -1,   "/dev/citty3", NULL, CHANNEL_IDLE, PTHREAD_MUTEX_INITIALIZER },
	{ SERVICE_PS,	 -1,   "/dev/citty4", NULL, CHANNEL_IDLE, PTHREAD_MUTEX_INITIALIZER },
	{ SERVICE_SIM,	 -1,   "/dev/citty5", NULL, CHANNEL_IDLE, PTHREAD_MUTEX_INITIALIZER },
	{ SERVICE_SS,	 -1,   "/dev/citty6", NULL, CHANNEL_IDLE, PTHREAD_MUTEX_INITIALIZER },
	{ SERVICE_UNSOL, -1,   "/dev/citty7", NULL, CHANNEL_IDLE, PTHREAD_MUTEX_INITIALIZER },
};
#elif defined BROWNSTONE_CP
struct channel_description descriptions[SUPPORTED_CHANNEL_NUMBER] = {
	{ CHANNEL_CMD,	 -1,   "/dev/ttyACM2", NULL, CHANNEL_IDLE, PTHREAD_MUTEX_INITIALIZER },
	{ CHANNEL_DAT,	 -1,   "/dev/ttyACM0", NULL, CHANNEL_IDLE, PTHREAD_MUTEX_INITIALIZER },
};

int service_channel_map[SERVICE_TOTAL] = {
	CHANNEL_CMD,
	CHANNEL_CMD,
	CHANNEL_CMD,
	CHANNEL_CMD,
	CHANNEL_CMD,
	CHANNEL_CMD,
	CHANNEL_CMD,
	CHANNEL_CMD
};
#endif

/*************************************************************
 *  Lib called by ril-xxx.c
 **************************************************************/
int callback_DefaultResponse(ATResponse* response, struct requestSession* session)
{
	if (response->success == 0)
	{
		RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	}
	else
	{
		RIL_onRequestComplete(session->token, RIL_E_SUCCESS, NULL, 0);
	}
	return 1;
}

int callback_DefaultSuccess(ATResponse* response, struct requestSession* session)
{
	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, NULL, 0);
	return 1;
}

int callback_ReturnOneInt(ATResponse* response, struct requestSession* session)
{
	int result, err;
	char *line;

	if (response->success == 0) goto error;

	if (!response->p_intermediates) goto error;

	line = response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &result);
	if (err < 0) goto error;

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, &result, sizeof(result));
	return 1;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

int callback_ReturnOneString(ATResponse* response, struct requestSession* session)
{
	int err;
	char *line, *result;

	if (response->success == 0) goto error;

	line = response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextstr(&line, &result);
	if (err < 0) goto error;

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, result, strlen(result));
	return 1;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}


int getTableIndex(RIL_AT_Map table[],  int request, int num)
{
	int index;

	for ( index = 0; index < num; index++)
	{
		if (table[index].request == request)
			return index;
	}
	return -1;
}

RIL_RadioState getRadioState()
{
	return sState;
}

void setRadioState(RIL_RadioState newState)
{
	RIL_RadioState oldState;

	pthread_mutex_lock(&s_state_mutex);

	oldState = sState;

	if (s_closed > 0)
	{
		/* If we're closed, the only reasonable state is RADIO_STATE_UNAVAILABLE
		 * This is here because things on the main thread may attempt to change the radio state
		 * after the closed event happened in another thread
		 */
		newState = RADIO_STATE_UNAVAILABLE;
		pthread_cond_broadcast(&s_state_cond);
	}

	sState = newState;

	pthread_mutex_unlock(&s_state_mutex);

	LOGI("setRadioState: sState=%d,oldState=%d", sState, oldState);

	/* Do these outside of the mutex */
	/* [Jerry] Send unsol msg to upper layer only when RILD is connected.
	 *  When RILD is connected, onNewCommandConnect() in RIL.cpp will be called, which calls RIL_onUnsolicitedResponse()
	 *  then onCurrentState() will be called, and sFirstGet will be changed
	 */
	if (!sFirstGet)
	{
		if (sState != oldState)
		{
			s_newrilconnection = 0;
			RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED, NULL, 0);
			s_newrilconnection = 1;
		}
	}
}


/*************************************************************
 * Power on process related
 **************************************************************/

/* Update latest correct state to upper layer, called by onCurrentState() */
static void reportLatestRadioState(void *param)
{
	if (sState != RADIO_STATE_OFF)
	{
		s_newrilconnection = 0;
		RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED, NULL, 0);
		s_newrilconnection = 1;
	}
}

/**
 * Synchronous call from the RIL to us to return current radio state.
 * RADIO_STATE_UNAVAILABLE should be the initial state.
 */
static RIL_RadioState onCurrentState()
{
	RIL_RadioState state;
	const struct timeval TIMEVAL_DELAY_1s = { 1, 0 };

	/*
	 * We set init state as RADIO_OFF. If we set real state other that RADIO_OFF, upper layer will send AT+CFUN=0 to reset radio.
	 * Sometimes the Android applicatoin will crash and Andriod application will restart and the ril connection between rild and
	 * android application will be reestablished. onNewCommandConnect() -> RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED, NULL ,0)
	 * ->onCurrentState() will be called.
	 * So we use one trick flag s_newrilconnection to identify it
	 */

	if(s_newrilconnection) sFirstGet = 1;

	if (sFirstGet)
	{
		state = RADIO_STATE_OFF;
#ifdef BROWNSTONE_CP
		char  value[PROPERTY_VALUE_MAX];
		property_get("have.internal.modem", value, "0");
		if (atoi(value) == 0) state = RADIO_STATE_UNAVAILABLE;
#endif
		sFirstGet = 0;
		LOGI("%s: Return init RADIO_STATE_OFF and call reportLatestRadioState()", __FUNCTION__);

		/* The func will be called when rild is connected, refer to RIL_onUnsolicitedResponse() in RIL.cpp */
		if (sState != RADIO_STATE_OFF)
		{
			/* If RILD has been connected and RADIO_OFF has been sent to upper layer for first access,
			      we need to update latest correct state to upper layer now. To avoid potential blocking of messages
			      between RILJ/RILC, latest radio state is reported sometime later */
			RIL_requestTimedCallback(reportLatestRadioState, NULL, &TIMEVAL_DELAY_1s);

			/* Enable reporting network state to upper layer because RIL has finished initialization and rild is connected */
			setNetworkStateReportOption(1);
		}
	}
	else
	{
		state = sState;
	}

	LOGI("%s: Fetched current state=%d", __FUNCTION__, state);
	return state;


}
/**
 * Call from RIL to us to find out whether a specific request code
 * is supported by this implementation.
 *
 * Return 1 for "supported" and 0 for "unsupported"
 */

static int onSupports (int requestCode)
{
	//@@@ todo
	return 1;
}

static void onCancel (RIL_Token token)
{
	//@@@todo
	LOGI("onCancel is called, but not implemented yet\n");
}

static const char * onGetVersion(void)
{
	return "Marvell Ril 1.0";
}

/**
 * Initialize everything that can be configured while we're still in
 * AT+CFUN=0
 */
 void initializeCallback(void *param)
{
	ATResponse *p_response = NULL;
	int err;
	at_send_command_sync(CHANNEL_ID(DEV), "AT", NO_RESULT, NULL, NULL, NULL);
	if(AfterCPReset)
	{
		at_send_command_sync(CHANNEL_ID(DEV), "AT+CFUN=1", NO_RESULT, NULL, NULL, NULL);
		AfterCPReset = 0;
	}
	//at_handshake();
	/* note: we don't check errors here. Everything important will
	   be handled in onATTimeout and onATReaderClosed */

	/*  atchannel is tolerant of echo but it must */
	/*  have verbose result codes */
	// at_send_command(0,"ATE0Q0V1", 0);

	/*  No auto-answer */
	at_send_command_sync(CHANNEL_ID(CC), "ATS0=0", NO_RESULT, NULL, NULL, NULL);

	/*  Extended errors */
	//at_send_command_sync(SERVICE_DEV, "AT+CMEE=1", NO_RESULT, NULL, NULL, NULL);

	/*  Network registration events */
	err = at_send_command_sync(CHANNEL_ID(MM), "AT+CREG=2", NO_RESULT, NULL, NULL, &p_response);

	/* some handsets -- in tethered mode -- don't support CREG=2 */
	if (err < 0 || p_response->success == 0)
	{
		at_send_command_sync(CHANNEL_ID(MM), "AT+CREG=1", NO_RESULT, NULL, NULL, NULL);
	}
	at_response_free(p_response);
	p_response = NULL;

	/*  GPRS registration events */
	err = at_send_command_sync(CHANNEL_ID(MM), "AT+CGREG=2", NO_RESULT, NULL, NULL, &p_response);
	if (err < 0 || p_response->success == 0)
	{
		at_send_command_sync(CHANNEL_ID(MM), "AT+CGREG=1", NO_RESULT, NULL, NULL, NULL);
	}
	at_response_free(p_response);

	/*  Call Waiting notifications */
	at_send_command_sync(CHANNEL_ID(SS), "AT+CCWA=1", NO_RESULT, NULL, NULL, NULL);

	/*  Alternating voice/data off */
	//at_send_command_sync(SERVICE_CC, "AT+CMOD=0", NO_RESULT, NULL, NULL, NULL);

	/*  Not muted */
	//at_send_command_sync(SERVICE_DEV, "AT+CMUT=0", NO_RESULT, NULL, NULL, NULL);

	/*  +CSSU unsolicited supp service notifications */
	at_send_command_sync(CHANNEL_ID(SS), "AT+CSSN=1,1", NO_RESULT, NULL, NULL, NULL);

	/*  no connected line identification */
	at_send_command_sync(CHANNEL_ID(SS), "AT+COLP=0", NO_RESULT, NULL, NULL, NULL);

	/*  HEX character set */
	//at_send_command_sync(SERVICE_DEV, "AT+CSCS=\"HEX\"", NO_RESULT, NULL, NULL, NULL);

	/*  USSD unsolicited */
	at_send_command_sync(CHANNEL_ID(SS), "AT+CUSD=1", NO_RESULT, NULL, NULL, NULL);

	/*  Enable +CGEV GPRS event notifications, but don't buffer */
	//at_send_command_sync(SERVICE_PS, "AT+CGEREP=1,0", NO_RESULT, NULL, NULL, NULL);

	/*  SMS PDU mode */
	at_send_command_sync(CHANNEL_ID(MSG), "AT+CMGF=0", NO_RESULT, NULL, NULL, NULL);

	/*  Disable +CCCM ind msg */
	at_send_command_sync(CHANNEL_ID(CC), "AT+CAOC=1", NO_RESULT, NULL, NULL, NULL);

	/*  Enable network mode indication msg */
	at_send_command_sync(CHANNEL_ID(MM), "AT+CIND=1", NO_RESULT, NULL, NULL, NULL);

	/*  MT msg saved in ME, enable the setting if SIM card already initialized */
	at_send_command_sync(CHANNEL_ID(MSG), "AT+CNMI=1,2,2,1,1", NO_RESULT, NULL, NULL, NULL);

	/*  Enable CSD mode  */
	at_send_command_sync(CHANNEL_ID(MM), "AT+CBST=134,1,0", NO_RESULT, NULL, NULL, NULL);

	/* If radio is on, get correct SIM state */
	if (isRadioOn() > 0)
	{
		updateRadioState();
	}

}

/*************************************************************
 * Mechanism to send async AT cmd
 **************************************************************/
static void enqueueSession(int channelID, struct requestSession* session)
{
	struct requestSession* lastSession;

	pthread_mutex_lock(&descriptions[channelID].mutex);
	lastSession = descriptions[channelID].lastSession;
	session->next = lastSession;
	descriptions[channelID].lastSession = session;
	pthread_mutex_unlock(&descriptions[channelID].mutex);
}

static void removeSession(int channelID, struct requestSession* session)
{
	pthread_mutex_lock(&descriptions[channelID].mutex);
	if (descriptions[channelID].lastSession == NULL)
	{
		LOGE("removeSession CRASH");
	}

	struct requestSession* tmp = descriptions[channelID].lastSession;
	if (tmp == NULL)
	{
		pthread_mutex_unlock(&descriptions[channelID].mutex);
		return;
	}
	if (tmp->sessionID == session->sessionID)
	{
		descriptions[channelID].lastSession = NULL;
		pthread_mutex_unlock(&descriptions[channelID].mutex);
		return;
	}
	while (tmp->next != NULL)
	{
		if (tmp->next->sessionID == session->sessionID)
		{
			tmp->next = tmp->next->next;
			break;
		}
		tmp = tmp->next;
	}
	pthread_mutex_unlock(&descriptions[channelID].mutex);
}

static struct requestSession* getSession(int channelID)
{
	pthread_mutex_lock(&descriptions[channelID].mutex);
	struct requestSession* tmp = descriptions[channelID].lastSession;
	if (tmp == NULL)
	{
		pthread_mutex_unlock(&descriptions[channelID].mutex);
		return NULL;
	}
	while (tmp->next != NULL)
	{
		tmp = tmp->next;
	}
	pthread_mutex_unlock(&descriptions[channelID].mutex);
	return tmp;
}

static struct requestSession* findSessionByCookie(int channelID, int cookie)
{
	pthread_mutex_lock(&descriptions[channelID].mutex);
	struct requestSession* tmp = descriptions[channelID].lastSession;
	if (tmp == NULL)
	{
		pthread_mutex_unlock(&descriptions[channelID].mutex);
		return NULL;
	}
	while (tmp != NULL)
	{
		if (tmp->sessionID == cookie)
		{
			pthread_mutex_unlock(&descriptions[channelID].mutex);
			return tmp;
		}
		tmp = tmp->next;
	}
	pthread_mutex_unlock(&descriptions[channelID].mutex);
	return NULL;
}
static int isSessionPending(int channelID)
{
	int ret;

	pthread_mutex_lock(&descriptions[channelID].mutex);
	struct requestSession* tmp = descriptions[channelID].lastSession;
	if (tmp == NULL)
	{
		ret = 0;
	}
	else
	{
		ret = 1;
	}
	pthread_mutex_unlock(&descriptions[channelID].mutex);
	return ret;
}

void continueProcessRequest(void* param)
{
	MYLOG("%s: entry", __FUNCTION__);
	struct channel_description* description = (struct channel_description*)param;
	struct requestSession * session = getSession(description->channelID);
	at_send_command(session->channelID, session->cmdString, session->type, session->responsePrefix, session->smsPdu, session->needResponse, session->sessionID);
	MYLOG("%s: exit", __FUNCTION__);
}
static void putChannel(int channelID)
{
	pthread_mutex_lock(&descriptions[channelID].mutex);
	if (descriptions[channelID].state != CHANNEL_BUSY)
	{
		goto done;
	}
	descriptions[channelID].state = CHANNEL_IDLE;
	RIL_requestTimedCallback(processNextRequest, &descriptions[channelID], &TIMEVAL_0);
 done:
	pthread_mutex_unlock(&descriptions[channelID].mutex);
}

/* Return channel status (idle or busy) */
static int getChannel(int channelID)
{
	int ret;

	pthread_mutex_lock(&descriptions[channelID].mutex);
	if (descriptions[channelID].state == CHANNEL_IDLE)
	{
		descriptions[channelID].state = CHANNEL_BUSY;
		ret = 1;
	}
	else
	{
		ret = 0;
	}
	pthread_mutex_unlock(&descriptions[channelID].mutex);
	return ret;

}

static void postCallback(int channelID, struct requestSession* session)
{
	removeSession(session->channelID, session);
	putChannel(session->channelID);
	free(session);
}

static void processNextRequest(void* param)
{
	MYLOG("%s entry", __FUNCTION__);
	struct channel_description* description = (struct channel_description*)param;
	if (isSessionPending(description->channelID) == 0)
		return;

	LOGD("%s session is pending", __FUNCTION__);
	if (getChannel(description->channelID) == 0)
		return;

	struct requestSession * session = getSession(description->channelID);
	if (session == NULL)
	{
		putChannel(description->channelID);
		return;
	}
	MYLOG("%s: queue is not empty", __FUNCTION__);
	at_send_command(session->channelID, session->cmdString, session->type, session->responsePrefix, session->smsPdu, session->needResponse, session->sessionID);
}

static void waitForClose()
{
	pthread_mutex_lock(&s_state_mutex);

	while (s_closed == 0)
	{
		pthread_cond_wait(&s_state_cond, &s_state_mutex);
	}

	pthread_mutex_unlock(&s_state_mutex);
}

/* Called on command or reader thread */
static void onATReaderClosed()
{
	at_channel_close(0);
	s_closed = 1;

	setRadioState(RADIO_STATE_UNAVAILABLE);
}

/* Called on command thread */
static void onATTimeout()
{
	at_channel_close(0);

	s_closed = 1;

	/* FIXME cause a radio reset here */

	setRadioState(RADIO_STATE_UNAVAILABLE);
}
static void usage(char *s)
{
#ifdef RIL_SHLIB
	fprintf(stderr, "reference-ril requires: -p <tcp port> or -d /dev/tty_device\n");
#else
	fprintf(stderr, "usage: %s [-p <tcp port>] [-d /dev/tty_device]\n", s);
	exit(-1);
#endif
}

static void *mainLoop(void *param)
{
	int fd, ret, i;

	LOGD("entering mainLoop()");
	at_set_on_reader_closed(onATReaderClosed);
	at_set_on_timeout(onATTimeout);

	for (;; )
	{
		fd = -1;
 again:
		for (i = 0; i < COMMAND_CHANNEL_NUMBER; i++)
		{
			//open TTY device, and attach it to channel
#if 1
			struct termios newtio; //place for old and new port settings for serial port
			fd = open(descriptions[i].ttyName, O_RDWR);
#ifdef DKB_CP
			fcntl(fd, F_SETFL, 0);
			newtio.c_cflag = BAUD | CRTSCTS | DATABITS | STOPBITS | PARITYON | PARITY | CLOCAL | CREAD;
			newtio.c_iflag = IGNPAR;
			newtio.c_oflag = 0;
			newtio.c_lflag =  ECHOE | ECHO | ICANON;        //ICANON;
			newtio.c_lflag =  0;                            //ICANON;

			newtio.c_cc[VMIN] = 1;
			newtio.c_cc[VTIME] = 0;
			newtio.c_cc[VERASE] = 0x8;
			newtio.c_cc[VEOL] = 0xD;

			newtio.c_cc[VINTR]    = 0;      /* Ctrl-c */
			newtio.c_cc[VQUIT]    = 0;      /* Ctrl-\ */
			newtio.c_cc[VERASE]   = 0;      /* del */
			newtio.c_cc[VKILL]    = 0;      /* @ */
			newtio.c_cc[VEOF]     = 4;      /* Ctrl-d */
			newtio.c_cc[VTIME]    = 0;      /* inter-character timer unused */
			newtio.c_cc[VMIN]     = 1;      /* blocking read until 1 character arrives */
			newtio.c_cc[VSWTC]    = 0;      /* '\0' */
			newtio.c_cc[VSTART]   = 0;      /* Ctrl-q */
			newtio.c_cc[VSTOP]    = 0;      /* Ctrl-s */
			newtio.c_cc[VSUSP]    = 0;      /* Ctrl-z */
			newtio.c_cc[VEOL]     = 0;      /* '\0' */
			newtio.c_cc[VREPRINT] = 0;      /* Ctrl-r */
			newtio.c_cc[VDISCARD] = 0;      /* Ctrl-u */
			newtio.c_cc[VWERASE]  = 0;      /* Ctrl-w */
			newtio.c_cc[VLNEXT]   = 0;      /* Ctrl-v */
			newtio.c_cc[VEOL2]    = 0;      /* '\0' */
			newtio.c_cc[VMIN] = 1;
			newtio.c_cc[VTIME] = 0;
			newtio.c_cc[VERASE] = 0x8;
			newtio.c_cc[VEOL] = 0xD;
#elif defined BROWNSTONE_CP
			tcgetattr(fd, &newtio);
			newtio.c_lflag = 0;
#endif
			tcflush(fd, TCIFLUSH);
			tcsetattr(fd, TCSANOW, &newtio);
#else
			fd = socket_local_client( s_device_path, ANDROID_SOCKET_NAMESPACE_FILESYSTEM, SOCK_STREAM );
#endif

			if (fd >= 0)
			{
#ifdef BROWNSTONE_CP
				property_set("have.internal.modem", "1");
#endif
				descriptions[i].fd = fd;
				LOGI("AT channel [%d] open successfully, ttyName:%s", i, descriptions[i].ttyName );
			}
			else
			{
#ifdef BROWNSTONE_CP
				property_set("have.internal.modem", "0");
#endif
				LOGE("AT channel [%d] open error, ttyName:%s, try again", i, descriptions[i].ttyName );
				sleep(1);
				goto again;
			}
			at_channel_open(descriptions[i].channelID, fd);
		}

		s_closed = 0;

		ret = at_channel_init(onUnsolicited, onConfirm);
		if (ret < 0)
		{
			LOGE("AT init error %d \n", ret);
			return 0;
		}
		/* Init radio state to RADIO OFF  */
		setRadioState(RADIO_STATE_OFF);
		RIL_requestTimedCallback(initializeCallback, NULL, &TIMEVAL_0);

		// Give initializeCallback a chance to dispatched, since
		// we don't presently have a cancellation mechanism
		sleep(1);

		waitForClose();
		LOGI("Re-opening after close");
	}
}


#ifdef RIL_SHLIB

pthread_t s_tid_mainloop;

const RIL_RadioFunctions *RIL_Init(const struct RIL_Env *env, int argc, char **argv)
{
	int ret;
	int fd = -1;
	int opt;
	pthread_attr_t attr;

#ifdef DKB_CP
	LOGI("Current CP: dkb version");
#elif defined BROWNSTONE_CP
	LOGI("Current CP: brownstone version");
#endif

	s_rilenv = env;

/* [Jerry] the arg "-d /dev/ttyS0" defined in /<nfsroot>/system/build.prop is not used now.
 *            dev name is defined in descriptions[i].ttyName, and opened in mainLoop()
 */
#if 0
	while ( -1 != (opt = getopt(argc, argv, "p:d:s:")))
	{
		switch (opt)
		{
		case 'p':
			s_port = atoi(optarg);
			if (s_port == 0)
			{
				usage(argv[0]);
				return NULL;
			}
			LOGI("Opening loopback port %d\n", s_port);
			break;

		case 'd':
			s_device_path = optarg;
			LOGI("Opening tty device %s\n", s_device_path);
			break;

		case 's':
			s_device_path   = optarg;
			s_device_socket = 1;
			LOGI("Opening socket %s\n", s_device_path);
			break;

		default:
			usage(argv[0]);
			return NULL;
		}
	}

	if (s_port < 0 && s_device_path == NULL)
	{
		usage(argv[0]);
		return NULL;
	}
#endif

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ret = pthread_create(&s_tid_mainloop, &attr, mainLoop, NULL);

	return &s_callbacks;
}
#else /* RIL_SHLIB */
int main (int argc, char **argv)
{
	int ret;
	int fd = -1;
	int opt;

	while ( -1 != (opt = getopt(argc, argv, "p:d:")))
	{
		switch (opt)
		{
		case 'p':
			s_port = atoi(optarg);
			if (s_port == 0)
			{
				usage(argv[0]);
			}
			LOGI("Opening loopback port %d\n", s_port);
			break;

		case 'd':
			s_device_path = optarg;
			LOGI("Opening tty device %s\n", s_device_path);
			break;

		case 's':
			s_device_path   = optarg;
			s_device_socket = 1;
			LOGI("Opening socket %s\n", s_device_path);
			break;

		default:
			usage(argv[0]);
		}
	}

	if (s_port < 0 && s_device_path == NULL)
	{
		usage(argv[0]);
	}

	RIL_register(&s_callbacks);

	mainLoop(NULL);

	return 0;
}

#endif /* RIL_SHLIB */

/*************************************************************
 * Entrance of service related code, wiil call func in each ril-xxx.c
 **************************************************************/
static ServiceType judgeServiceOnRequest(int request)
{
	ServiceType service = SERVICE_NULL;

	switch (request)
	{
	case RIL_REQUEST_GET_SIM_STATUS:
	case RIL_REQUEST_SIM_IO:
	case RIL_REQUEST_ENTER_SIM_PIN:
	case RIL_REQUEST_ENTER_SIM_PUK:
	case RIL_REQUEST_ENTER_SIM_PIN2:
	case RIL_REQUEST_ENTER_SIM_PUK2:
	case RIL_REQUEST_CHANGE_SIM_PIN:
	case RIL_REQUEST_CHANGE_SIM_PIN2:
	case RIL_REQUEST_ENTER_NETWORK_DEPERSONALIZATION:
	case RIL_REQUEST_GET_IMSI:
	case RIL_REQUEST_STK_GET_PROFILE:
	case RIL_REQUEST_STK_SET_PROFILE:
	case RIL_REQUEST_STK_SEND_ENVELOPE_COMMAND:
	case RIL_REQUEST_STK_SEND_TERMINAL_RESPONSE:
	case RIL_REQUEST_STK_HANDLE_CALL_SETUP_REQUESTED_FROM_SIM:
	case RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING:
	case RIL_REQUEST_SET_FACILITY_LOCK:
		service = SERVICE_SIM;
		break;

	case RIL_REQUEST_SIGNAL_STRENGTH:
	case RIL_REQUEST_REGISTRATION_STATE:
	case RIL_REQUEST_GPRS_REGISTRATION_STATE:
	case RIL_REQUEST_OPERATOR:
	case RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE:
	case RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC:
	case RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL:
	case RIL_REQUEST_QUERY_AVAILABLE_NETWORKS:
	case RIL_REQUEST_SCREEN_STATE:
	case RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE:
	case RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE:
	case RIL_REQUEST_GET_NEIGHBORING_CELL_IDS:
	case RIL_REQUEST_SET_LOCATION_UPDATES:
	case RIL_REQUEST_SELECT_BAND:
		service = SERVICE_MM;
		break;

	case RIL_REQUEST_GET_CURRENT_CALLS:
	case RIL_REQUEST_DIAL:
	case RIL_REQUEST_HANGUP:
	case RIL_REQUEST_DIAL_VT:
	case RIL_REQUEST_HANGUP_VT:
	case RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND:
	case RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND:
	case RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE:
	case RIL_REQUEST_CONFERENCE:
	case RIL_REQUEST_UDUB:
	case RIL_REQUEST_LAST_CALL_FAIL_CAUSE:
	case RIL_REQUEST_DTMF:
	case RIL_REQUEST_ANSWER:
	case RIL_REQUEST_DTMF_START:
	case RIL_REQUEST_DTMF_STOP:
	case RIL_REQUEST_SEPARATE_CONNECTION:
	case RIL_REQUEST_EXPLICIT_CALL_TRANSFER:
	case RIL_REQUEST_SET_MUTE:
	case RIL_REQUEST_GET_MUTE:
	case RIL_REQUEST_SET_ACM:
	case RIL_REQUEST_GET_ACM:
	case RIL_REQUEST_SET_AMM:
	case RIL_REQUEST_GET_AMM:
	case RIL_REQUEST_SET_CPUC:
	case RIL_REQUEST_GET_CPUC:
		service = SERVICE_CC;
		break;

	case RIL_REQUEST_RADIO_POWER:
	case RIL_REQUEST_GET_IMEI:
	case RIL_REQUEST_GET_IMEISV:
	case RIL_REQUEST_BASEBAND_VERSION:
	case RIL_REQUEST_RESET_RADIO:
	case RIL_REQUEST_OEM_HOOK_RAW:
	case RIL_REQUEST_OEM_HOOK_STRINGS:
	case RIL_REQUEST_SET_BAND_MODE:
	case RIL_REQUEST_QUERY_AVAILABLE_BAND_MODE:
	case RIL_REQUEST_DEVICE_IDENTITY:
		service = SERVICE_DEV;
		break;

	case RIL_REQUEST_SEND_SMS:
	case RIL_REQUEST_SEND_SMS_EXPECT_MORE:
	case RIL_REQUEST_SMS_ACKNOWLEDGE:
	case RIL_REQUEST_WRITE_SMS_TO_SIM:
	case RIL_REQUEST_DELETE_SMS_ON_SIM:
	case RIL_REQUEST_GET_SMSC_ADDRESS:
	case RIL_REQUEST_SET_SMSC_ADDRESS:
		service = SERVICE_MSG;
		break;

	case RIL_REQUEST_SETUP_DATA_CALL:
	case RIL_REQUEST_DEACTIVATE_DATA_CALL:
	case RIL_REQUEST_LAST_DATA_CALL_FAIL_CAUSE:
	case RIL_REQUEST_DATA_CALL_LIST:
	case RIL_REQUEST_FAST_DORMANCY:
		service = SERVICE_PS;
		break;

	case RIL_REQUEST_SEND_USSD:
	case RIL_REQUEST_CANCEL_USSD:
	case RIL_REQUEST_GET_CLIR:
	case RIL_REQUEST_SET_CLIR:
	case RIL_REQUEST_QUERY_CALL_FORWARD_STATUS:
	case RIL_REQUEST_SET_CALL_FORWARD:
	case RIL_REQUEST_QUERY_CALL_WAITING:
	case RIL_REQUEST_SET_CALL_WAITING:
	case RIL_REQUEST_QUERY_FACILITY_LOCK:
	case RIL_REQUEST_CHANGE_BARRING_PASSWORD:
	case RIL_REQUEST_QUERY_CLIP:
	case RIL_REQUEST_SET_SUPP_SVC_NOTIFICATION:
	case RIL_REQUEST_GET_CNAP:
		service = SERVICE_SS;
		break;

	default:
		LOGW("invalid request:%d\n", request);
	}

	return service;
}

static ServiceType judgeServiceOnUnsolicited(const char *s)
{
	int index, total, service;

	struct {
		char *prefix;
		ServiceType service;
	} matchTable[] = {
		{ "+CSQ:",	SERVICE_MM	       },
		{ "+CREG:",	SERVICE_MM	       },
		{ "+CGREG:",	SERVICE_MM	       },
		{ "+EEMGINFONC:", SERVICE_MM    },
		{ "+EEMUMTSINTER:", SERVICE_MM    },
		{ "+EEMUMTSINTRA:", SERVICE_MM    },
		{ "+EEMUMTSINTERRAT:", SERVICE_MM	  },
		{ "+EEMUMTSSVC:", SERVICE_MM	  },
		{ "+EEMGINFOBASIC", SERVICE_MM},
		{ "+EEMGINFOSVC", SERVICE_MM},
		{ "+EEMGINFOPS", SERVICE_MM},
		{ "+EEMGINBFTM", SERVICE_MM},
		{ "+NITZ:",	SERVICE_MM	       },
		{ "+MSRI:",	SERVICE_MM		},
		{ "+CRING:",	SERVICE_CC	       },
		{ "RING",	SERVICE_CC	       },
		{ "NO CARRIER", SERVICE_CC	       },
		{ "CONNECT", SERVICE_CC		   },
		{ "+CCWA",	SERVICE_CC	       },
		{ "+CCCM:",	SERVICE_CC	       },
		{ "+CLCC:",	SERVICE_CC	       },
		{ "+CGEV:",	SERVICE_PS	       },
		{ "+CMT:",	SERVICE_MSG	       },
		{ "+CMTI:",	SERVICE_MSG	       },
		{ "+CDS:",	SERVICE_MSG	       },
		{ "+MMSG:",	SERVICE_MSG	       },
		{ "+CBM:",      SERVICE_MSG            },
		{ "+CPIN:",	SERVICE_SIM	       },
		{ "+MPBK:",	SERVICE_SIM	       },
		{ "+MSTK:",	SERVICE_SIM	       },
		{ "+CSSI:",	SERVICE_SS	       },
		{ "+CSSU:",	SERVICE_SS	       },
		{ "+CUSD:",	SERVICE_SS	       },
	};

	total  = sizeof(matchTable) / sizeof(matchTable[0]);
	for (index = 0; index < total; index++)
	{
		if (strStartsWith(s, matchTable[index].prefix))
		{
			service = matchTable[index].service;
			return service;
		}
	}

	return SERVICE_NULL;
}

/*** Callback methods from the RIL library to us ***/

/**
 * Call from RIL to us to make a RIL_REQUEST
 *
 * Must be completed with a call to RIL_onRequestComplete()
 *
 * RIL_onRequestComplete() may be called from any thread, before or after
 * this function returns.
 *
 * Will always be called from the same thread, so returning here implies
 * that the radio is ready to process another command (whether or not
 * the previous command has completed).
 */

static void onRequest (int request, void *data, size_t datalen, RIL_Token token)
{
	char cmdString[MAX_AT_LENGTH];
	char smsPdu[MAX_PDU_LENGTH];
	char prefix[MAX_PREFIX_LENGTH];
	requestCallback callback;
	ATCommandType type;
	int continue_flag = 0;
	ServiceType service;

	LOGD("%s: %s, token:%p", __FUNCTION__, requestToString(request), token);

	if(AfterCPReset)
	{
		//if CP assert, should regard POWER ON as radio unavailable, POWER OFF as RADIO_OFF
		if(request ==  RIL_REQUEST_RADIO_POWER)
		{
			int onOff;
			assert(datalen >= sizeof(int *));
			onOff = ((int *)data)[0];
		
			if ((onOff == 0) && (sState != RADIO_STATE_OFF))
				setRadioState(RADIO_STATE_OFF);
			else if((onOff > 0) && (sState == RADIO_STATE_OFF))
				setRadioState(RADIO_STATE_UNAVAILABLE);

			RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);

			return;
		}
	}


	/* RIL_REQUEST_SCREEN_STATE is supported in any radio state */
	if (request == RIL_REQUEST_SCREEN_STATE)
	{
		onRequest_mm(request, data, datalen, token, cmdString, &type, prefix, &callback);
		return;
	}

	/* RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING is supported in any radio state */
	if (request == RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING)
	{
		onRequest_sim(request, data, datalen, token, cmdString, &type, prefix, &callback);
		return;
	}

	/* Handle RIL request when radio state is UNAVAILABLE */
	if (sState == RADIO_STATE_UNAVAILABLE)
	{
		if (request == RIL_REQUEST_GET_SIM_STATUS)
		{
			// SIM NOT READY
			RIL_AppStatus app_status = { RIL_APPTYPE_SIM, RIL_APPSTATE_DETECTED,
				RIL_PERSOSUBSTATE_UNKNOWN, NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN };

			RIL_CardStatus *p_card_status;

			/* Allocate and initialize base card status. */
			p_card_status = malloc(sizeof(RIL_CardStatus));
			p_card_status->card_state = RIL_CARDSTATE_PRESENT;
			p_card_status->universal_pin_state = RIL_PINSTATE_UNKNOWN;
			p_card_status->gsm_umts_subscription_app_index = 0;
			p_card_status->cdma_subscription_app_index = RIL_CARD_MAX_APPS;
			p_card_status->num_applications = 1;
			p_card_status->applications[0] = app_status;

			RIL_onRequestComplete(token, RIL_E_SUCCESS,
				(char*)p_card_status, sizeof(RIL_CardStatus));

			free(p_card_status);
			return;
		}

		else
		{
			RIL_onRequestComplete(token, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
			return;
		}
	}

	/* Handle RIL request when radio state is OFF */
	else if (sState == RADIO_STATE_OFF)
	{
		if (request == RIL_REQUEST_GET_SIM_STATUS)
		{
			// SIM NOT READY
			RIL_AppStatus app_status = { RIL_APPTYPE_SIM, RIL_APPSTATE_DETECTED,
				RIL_PERSOSUBSTATE_UNKNOWN, NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN };

			RIL_CardStatus *p_card_status;

			/* Allocate and initialize base card status. */
			p_card_status = malloc(sizeof(RIL_CardStatus));
			p_card_status->card_state = RIL_CARDSTATE_PRESENT;
			p_card_status->universal_pin_state = RIL_PINSTATE_UNKNOWN;
			p_card_status->gsm_umts_subscription_app_index = 0;
			p_card_status->cdma_subscription_app_index = RIL_CARD_MAX_APPS;
			p_card_status->num_applications = 1;
			p_card_status->applications[0] = app_status;

			RIL_onRequestComplete(token, RIL_E_SUCCESS,
				(char*)p_card_status, sizeof(RIL_CardStatus));

			free(p_card_status);
			return;
		}

		else if (request ==  RIL_REQUEST_RADIO_POWER)
		{
			onRequest_dev(request, data, datalen, token, cmdString, &type, prefix, &callback);
			return;
		}

		else
		{
			RIL_onRequestComplete(token, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
			return;
		}
	}

	/* Handle other cases */
	cmdString[0] = '\0';
	smsPdu[0] = '\0';
	prefix[0] = '\0';
	callback = NULL;

	service = judgeServiceOnRequest(request);
	switch (service)
	{
	case SERVICE_CC:
		continue_flag = onRequest_cc(request, data, datalen, token, cmdString, &type, prefix, &callback);
		break;
	case SERVICE_DEV:
		continue_flag = onRequest_dev(request, data, datalen, token, cmdString, &type, prefix, &callback);
		break;
	case SERVICE_MM:
		continue_flag = onRequest_mm(request, data, datalen, token, cmdString, &type, prefix, &callback);
		break;
	case SERVICE_MSG:
		continue_flag = onRequest_msg(request, data, datalen, token, cmdString, smsPdu, &type, prefix, &callback);
		break;
	case SERVICE_PS:
		continue_flag = onRequest_ps(request, data, datalen, token, cmdString, &type, prefix, &callback);
		break;
	case SERVICE_SIM:
		continue_flag = onRequest_sim(request, data, datalen, token, cmdString, &type, prefix, &callback);
		break;
	case SERVICE_SS:
		continue_flag = onRequest_ss(request, data, datalen, token, cmdString, &type, prefix, &callback);
		break;
	default:
		LOGW("%s: invalid service ID:%d\n", __FUNCTION__, service);
		RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
		return;
	}

	/* Send async AT cmd */
	if (continue_flag == 1)
	{
#ifdef DKB_CP
		int channelID = service; //one service, one channel
#elif defined BROWNSTONE_CP
		int channelID = service_channel_map[service];
#endif

		/* Create request for async AT cmd */
		struct requestSession* session = malloc(sizeof(struct requestSession));
		session->sessionID = (globalSessionID++) % MAX_SESSION_ID;
		session->request = request;
		session->token = token;
		session->channelID = channelID;
		session->type = type;
		session->callback = callback;
		strcpy(session->cmdString, cmdString);
		strcpy(session->smsPdu, smsPdu);
		strcpy(session->responsePrefix, prefix);

		session->next = NULL;
		session->needResponse = 1;      //we can ignore this variable

		MYLOG("%s: %s, finished to create request", __FUNCTION__, requestToString(request));

		/* Put request to queue */
		enqueueSession(channelID, session);
		if (getChannel(channelID) == 0)
		{
			LOGI("%s: channel is busy", __FUNCTION__);
			return;
		}

		/*Send last request if channel isn't busy */
		session = getSession(channelID);
		if (session == NULL)
		{
			putChannel(channelID);
			return;
		}
		request = session->request;
		if (at_send_command(session->channelID, session->cmdString, session->type, session->responsePrefix, session->smsPdu, session->needResponse, session->sessionID) < 0) {
			putChannel(channelID);
			RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
			return;
		}

		MYLOG("%s: send command:%s\n", __FUNCTION__, session->cmdString);
	}

	/* Local implementation */
	else if (continue_flag == 0)
	{
		LOGI("onRequest: %s, Local implementation, no AT cmd sent", requestToString(request));
	}

	/* Send sync AT cmd in ril-xxx.c */
	else if (continue_flag == 2)
	{
		LOGI("onRequest: %s, Send sync AT cmd ", requestToString(request));
	}
}

/**
 * Called by atchannel when an unsolicited line appears
 * This is called on atchannel's reader thread. AT commands may
 * not be issued here
 */
static void onUnsolicited (const char *s, const char *smsPdu)
{
	int service;

	LOGI("onUnsolicited:%s", s);

	/* Ignore unsolicited responses until we're initialized.
	 * This is OK because the RIL library will poll for initial state
	 */
	if (sState == RADIO_STATE_UNAVAILABLE)
	{
		return;
	}

	service = judgeServiceOnUnsolicited(s); //one channel for one service
	switch (service)
	{
	case SERVICE_CC:
		onUnsolicited_cc(s);
		break;
	case SERVICE_DEV:
		onUnsolicited_dev(s);
		break;
	case SERVICE_MM:
		onUnsolicited_mm(s);
		break;
	case SERVICE_MSG:
		onUnsolicited_msg(s, smsPdu);
		break;
	case SERVICE_PS:
		onUnsolicited_ps(s);
		break;
	case SERVICE_SIM:
		onUnsolicited_sim(s);
		break;
	case SERVICE_SS:
		onUnsolicited_ss(s);
		break;
	default:
		LOGW("%s: Unexpected service type:%d", __FUNCTION__, service);
	}
}


void onConfirm(int channelID, ATResponse* response, int cookie)
{
	int service, done = 0;
	struct requestSession* session = findSessionByCookie(channelID, cookie);

	MYLOG("%s entry", __FUNCTION__);
	/* return value: 0 means the RIL request needs more handle, so we do'nt call postCallback after it.
	 *    1 means the RIL request has been completed, so we call postCallback after it
	 */
#ifdef DKB_CP
	service = channelID; //one channel for one service
	switch (service)
	{
	case SERVICE_CC:
		done = onConfirm_cc(response, session);
		break;
	case SERVICE_DEV:
		done = onConfirm_dev(response, session);
		break;
	case SERVICE_MM:
		done = onConfirm_mm(response, session);
		break;
	case SERVICE_MSG:
		done = onConfirm_msg(response, session);
		break;
	case SERVICE_PS:
		done = onConfirm_ps(response, session);
		break;
	case SERVICE_SIM:
		done = onConfirm_sim(response, session);
		break;
	case SERVICE_SS:
		done = onConfirm_ss(response, session);
		break;
	default:
		LOGW("%s: Unexpected service type:%d", __FUNCTION__, service);
	}
#elif defined BROWNSTONE_CP
	if (channelID >= 0 && channelID < CHANNEL_TOTAL)
	{
		done = session->callback(response, session);
	}
	else
	{
		LOGW("%s: Unexpected channel ID:%d", __FUNCTION__, channelID);
	}
#endif

	MYLOG("%s done=%d", __FUNCTION__, done);
	if (done == 1)
	{
		postCallback(session->channelID, session);
	}
	MYLOG("%s exit", __FUNCTION__);
	return;
}
