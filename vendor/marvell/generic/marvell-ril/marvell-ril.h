/******************************************************************************
 * *(C) Copyright 2008 Marvell International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/

#ifndef MARVELL_RIL_H_
#define MARVELL_RIL_H_

#include <telephony/ril.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <alloca.h>
#include "atchannel.h"
#include "at_tok.h"
#include "misc.h"
#include <getopt.h>
#include <sys/socket.h>
#include <cutils/sockets.h>
#include <termios.h>

#define LOG_TAG "RIL"
#include <utils/Log.h>

#define MAX_AT_RESPONSE   0x1000
#define MAX_PREFIX_LENGTH   32
#define MAX_AT_LENGTH     640   //512   //128
#define MAX_PDU_LENGTH    400   //128

#define DEFAULT_CID          "1"
#define DEFAULT_IFNAME   "ccinet0"

extern const struct timeval TIMEVAL_0;

#define FREE(a)      if (a != NULL) { free(a); a = NULL; }

#ifdef RIL_SHLIB
const struct RIL_Env *s_rilenv;

#define RIL_onRequestComplete(t, e, response, responselen) s_rilenv->OnRequestComplete(t, e, response, responselen)
#define RIL_onUnsolicitedResponse(a, b, c) s_rilenv->OnUnsolicitedResponse(a, b, c)
#define RIL_requestTimedCallback(a, b, c) s_rilenv->RequestTimedCallback(a, b, c)
#endif

#define MAX_SESSION_ID  9999
unsigned int globalSessionID;

typedef enum {
	SIM_ABSENT = 0,
	SIM_NOT_READY = 1,
	SIM_READY = 2, /* SIM_READY means the radio state is RADIO_STATE_SIM_READY */
	SIM_PIN = 3,
	SIM_PUK = 4,
	SIM_NETWORK_PERSONALIZATION = 5
} SIM_Status;

struct requestSession;
typedef int (*requestCallback)(ATResponse* response, struct requestSession* session);
struct requestSession {
	int sessionID;
	int channelID;
	int request;
	char cmdString[MAX_AT_LENGTH];
	requestCallback callback;
	ATCommandType type;
	char responsePrefix[MAX_PREFIX_LENGTH];
	int needResponse;
	RIL_Token token;
	char smsPdu[MAX_PDU_LENGTH];
	struct requestSession* next;
};

enum channel_state {
	CHANNEL_IDLE,
	CHANNEL_BUSY,
};

struct channel_description
{
	int channelID;
	int fd;
	char ttyName[128];
	struct requestSession* lastSession;
	enum channel_state state;
	pthread_mutex_t mutex;
};

extern struct channel_description descriptions[SUPPORTED_CHANNEL_NUMBER];

#ifdef DKB_CP
#define CHANNEL_ID(TYPE) SERVICE_##TYPE
#elif defined BROWNSTONE_CP
#define CHANNEL_ID(TYPE) service_channel_map[SERVICE_##TYPE]
extern int service_channel_map[SERVICE_TOTAL];
#endif

typedef struct RegState_s
{
	int stat;
	int lac;
	int cid;
} RegState;

typedef struct OperInfo_s
{
	int mode;
	char operLongStr[20];
	char operShortStr[10];
	char operNumStr[10];
	int act;
} OperInfo;


typedef struct RIL_AT_Map_s
{
	int request;
	ATCommandType type;
	requestCallback callback;
//  int channelID;  //which channel to use for this request
//    const char responsePrefix[MAX_PREFIX_LENGTH];
//    int needResponse;
} RIL_AT_Map;


/*Extern  I/F implemented in marvell-ril.c */
int callback_DefaultSuccess(ATResponse* response, struct requestSession* session);
int callback_DefaultResponse(ATResponse* response, struct requestSession* session);
int callback_ReturnOneInt(ATResponse* response, struct requestSession* session);
int callback_ReturnOneString(ATResponse* response, struct requestSession* session);
int getTableIndex(RIL_AT_Map table[], int request, int num);
void setRadioState(RIL_RadioState newState);
RIL_RadioState getRadioState();
void continueProcessRequest(void* param);

/* Extern I/F implemented in ril-xxx.c */
int isRadioOn(void);
int isRegistered(void);
void reportSignalStrength(void *param);
void resetLocalRegInfo(void);
void updateLocalRegInfo(void *param);
void initializeCallback(void *param);


// AT*BAND related definition
//
// mode definition
#define MODE_GSM_ONLY 0
#define MODE_UMTS_ONLY 1
#define MODE_DUAL_MODE_AUTO 2
#define MODE_DUAL_MODE_GSM_PREFERRED 3
#define MODE_DUAL_MODE_UMTS_PREFERRED 4

//GSM band bit definition
#define GSMBAND_PGSM_900    0x01
#define GSMBAND_DCS_GSM_1800  0x02
#define GSMBAND_PCS_GSM_1900  0x04
#define GSMBAND_EGSM_900   0x08
#define GSMBAND_GSM_450  0x10
#define GSMBAND_GSM_480 0x20
#define GSMBAND_GSM_850 0x40

//UMTS band bit
#define UMTSBAND_BAND_1 0x01  //IMT-2100
#define UMTSBAND_BAND_2 0x02  //PCS-1900
#define UMTSBAND_BAND_3 0x04  //DCS-1800
#define UMTSBAND_BAND_4 0x08  //AWS-1700
#define UMTSBAND_BAND_5 0x10  //CLR-850
#define UMTSBAND_BAND_6 0x20  //800Mhz
#define UMTSBAND_BAND_7 0x40  //IMT-E 2600
#define UMTSBAND_BAND_8 0x80  //GSM-900
#define UMTSBAND_BAND_9 0x100 //not used

#endif // MARVELL_RIL_H_

