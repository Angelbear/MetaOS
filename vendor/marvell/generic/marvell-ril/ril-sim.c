/******************************************************************************
 * *(C) Copyright 2008 Marvell International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/


#include "marvell-ril.h"

static int sSimStatus = SIM_NOT_READY;
static int stk_app_inited = 0;
static int stk_CP_inited = 0;

static RIL_AppType sAppType = RIL_APPTYPE_UNKNOWN;


/* Enum value of STK cmd type and STK ind msg type, should be same as in sim_api.h */
typedef enum StkCmdTypeTag
{
	STK_CMD_ENABLE_SIMAT = 0,
	STK_CMD_DOWNLOAD_PROFILE = 1,
	STK_CMD_GET_CAP_INFO = 2,
	STK_CMD_GET_PROFILE = 3,
	STK_CMD_SEND_ENVELOPE = 4,

	STK_CMD_PROACTIVE  = 11,
	STK_CMD_SETUP_CALL = 12,
	STK_CMD_DISPLAY_INFO = 13,
	STK_CMD_END_SESSION = 14,

	STK_TYPE_INVALID
} StkCmdType;

/* Internal func */
int callback_GetSimStatus(ATResponse* response, struct requestSession* session);
int callback_RequestSimIO(ATResponse* response, struct requestSession* session);
int callback_RequestGetIMSI(ATResponse* response, struct requestSession* session);
int callback_RequestFacilityLock(ATResponse* response, struct requestSession* session);
int callback_RequestEnterSimPin(ATResponse* response, struct requestSession* session);
int callback_RequestEnterSimPuk(ATResponse* response, struct requestSession* session);
int callback_RequestEnterSimPin2(ATResponse* response, struct requestSession* session);
int callback_RequestEnterSimPuk2(ATResponse* response, struct requestSession* session);
int callback_RequestChangeSimPin(ATResponse* response, struct requestSession* session);
int callback_RequestChangeSimPin2(ATResponse* response, struct requestSession* session);
int callback_RequestStkGetProfile(ATResponse* response, struct requestSession* session);
int callback_RequestStkSendEnvelope(ATResponse* response, struct requestSession* session);
int callback_RequestLeftPinRetry(ATResponse* response, struct requestSession* session);

static void getAppType(void *param);

RIL_AT_Map table_sim[] = {
	{ RIL_REQUEST_SIM_IO,					SINGLELINE,    callback_RequestSimIO	       },
	{ RIL_REQUEST_GET_SIM_STATUS,				SINGLELINE,    callback_GetSimStatus	       },
	{ RIL_REQUEST_SET_FACILITY_LOCK,	 	NO_RESULT, 		callback_RequestFacilityLock	 },
	{ RIL_REQUEST_ENTER_SIM_PIN,				NO_RESULT,     callback_RequestEnterSimPin     },
	{ RIL_REQUEST_ENTER_SIM_PUK,				NO_RESULT,     callback_RequestEnterSimPuk     },
	{ RIL_REQUEST_ENTER_SIM_PIN2,				NO_RESULT,     callback_RequestEnterSimPin2    },
	{ RIL_REQUEST_ENTER_SIM_PUK2,				NO_RESULT,     callback_RequestEnterSimPuk2    },
	{ RIL_REQUEST_CHANGE_SIM_PIN,				NO_RESULT,     callback_RequestChangeSimPin    },
	{ RIL_REQUEST_CHANGE_SIM_PIN2,				NO_RESULT,     callback_RequestChangeSimPin2   },
	{ RIL_REQUEST_GET_IMSI,					NUMERIC,       callback_RequestGetIMSI	       },
	{ RIL_REQUEST_ENTER_NETWORK_DEPERSONALIZATION,		NO_RESULT,     callback_RequestEnterSimPin     },
	/* STK related */
	{ RIL_REQUEST_STK_GET_PROFILE,				SINGLELINE,    callback_RequestStkGetProfile   },
	{ RIL_REQUEST_STK_SET_PROFILE,				SINGLELINE,    callback_DefaultResponse	       },
	{ RIL_REQUEST_STK_SEND_ENVELOPE_COMMAND,		SINGLELINE,    callback_RequestStkSendEnvelope },
	{ RIL_REQUEST_STK_SEND_TERMINAL_RESPONSE,		NO_RESULT,     callback_DefaultResponse	       },
	{ RIL_REQUEST_STK_HANDLE_CALL_SETUP_REQUESTED_FROM_SIM, NO_RESULT,     callback_DefaultResponse	       },
	{ RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING,		NO_RESULT,     NULL			       },
};

int onRequest_sim (int request, void *data, size_t datalen, RIL_Token token, char *cmdString,
		   ATCommandType *pType, char *prefix, requestCallback *pCallback)
{
	int continue_flag = 1;

	switch (request)
	{
	case RIL_REQUEST_SIM_IO:
	{
		RIL_SIM_IO *p_args;
		p_args = (RIL_SIM_IO *)data;

		if (p_args->data == NULL)
		{
			if(p_args->path == NULL)
			{	    
		    		sprintf(cmdString, "AT+CRSM=%d,%d,%d,%d,%d",
					p_args->command, p_args->fileid, p_args->p1, p_args->p2, p_args->p3);
			}
			else
			{
			    	sprintf(cmdString, "AT+CRSM=%d,%d,%d,%d,%d,,%s",
					p_args->command, p_args->fileid, p_args->p1, p_args->p2, p_args->p3, p_args->path); 
			}
		}
		else
		{
			if(p_args->path == NULL)
			{
		    		sprintf(cmdString, "AT+CRSM=%d,%d,%d,%d,%d,%s",
					p_args->command, p_args->fileid, p_args->p1, p_args->p2, p_args->p3, p_args->data);
			}
			else
			{
				sprintf(cmdString, "AT+CRSM=%d,%d,%d,%d,%d,%s,%s",
					p_args->command, p_args->fileid, p_args->p1, p_args->p2, p_args->p3, p_args->data, p_args->path);
			}
		}
		strcpy(prefix, "+CRSM:");
		break;
	}

	case RIL_REQUEST_GET_SIM_STATUS:
	{
		strcpy(cmdString, "AT+CPIN?");
		strcpy(prefix, "+CPIN:");
		break;
	}
	
	case RIL_REQUEST_SET_FACILITY_LOCK:
	{
		char* code = ((char**)data)[0];
		char* action = ((char**)data)[1];
		char* password = ((char**)data)[2];
		char* class = ((char**)data)[3];

		if (class[0] == '0')
		{
			sprintf(cmdString, "AT+CLCK=%s,%s,%s,7", code, action, password);
		}
		else
		{
			sprintf(cmdString, "AT+CLCK=%s,%s,%s,%s", code, action, password, class);
		}
		break;
	}

	case RIL_REQUEST_ENTER_SIM_PIN:
	case RIL_REQUEST_ENTER_SIM_PIN2:
	{
		char * pin = ((char**)data)[0];
		sprintf(cmdString, "AT+CPIN=%s", pin);
		strcpy(prefix, "+CPIN:");
		break;
	}


	case RIL_REQUEST_ENTER_SIM_PUK:
	case RIL_REQUEST_ENTER_SIM_PUK2:
	{
		char * puk = ((char**)data)[0];
		char * newPin = ((char**)data)[1];
		sprintf(cmdString, "ATD**05*%s*%s*%s#", puk, newPin, newPin);
	}
		break;

	case RIL_REQUEST_CHANGE_SIM_PIN:
	{
		char * oldPass = ((char**)data)[0];
		char * newPass = ((char**)data)[1];
		sprintf(cmdString, "AT+CPIN=%s,%s", oldPass, newPass);
		break;
	}

	case RIL_REQUEST_CHANGE_SIM_PIN2:
	{
		char * oldPass = ((char**)data)[0];
		char * newPass = ((char**)data)[1];
		sprintf(cmdString, "AT+CPWD=\"P2\",%s,%s", oldPass, newPass);
		break;
	}

	case RIL_REQUEST_GET_IMSI:
	{
		strcpy(cmdString, "AT+CIMI");
		break;
	}

	case RIL_REQUEST_ENTER_NETWORK_DEPERSONALIZATION:
	{
		char* code = ((char**)data)[0];
		sprintf(cmdString, "AT+CLCK=\"PN\",0,%s", code);
		strcpy(prefix, "+CLCK:");
		break;
	}

	case RIL_REQUEST_STK_GET_PROFILE:
	{
		int cmdType = STK_CMD_GET_PROFILE;
		sprintf(cmdString, "AT+MSTK=%d", cmdType);
		strcpy(prefix, "+MSTK:");
		break;
	}

	case RIL_REQUEST_STK_SET_PROFILE:
	{
		int cmdType = STK_CMD_DOWNLOAD_PROFILE;
		sprintf(cmdString, "AT+MSTK=%d,%s", cmdType, (char *)data);
		break;
	}

	case RIL_REQUEST_STK_SEND_ENVELOPE_COMMAND:
	{
		int cmdType = STK_CMD_SEND_ENVELOPE;
		sprintf(cmdString, "AT+MSTK=%d,%s", cmdType, (char *)data);
		strcpy(prefix, "+MSTK:");
		break;
	}

	case RIL_REQUEST_STK_SEND_TERMINAL_RESPONSE:
	{
		int cmdType = STK_CMD_PROACTIVE;
		sprintf(cmdString, "AT+MSTK=%d,%s", cmdType, (char *)data);
		break;
	}

	case RIL_REQUEST_STK_HANDLE_CALL_SETUP_REQUESTED_FROM_SIM:
	{
		int cmdType = STK_CMD_SETUP_CALL;
		int acceptFlag = ((int *)data)[0];
		sprintf(cmdString, "AT+MSTK=%d,%d", cmdType, acceptFlag);
		break;
	}

	case RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING:
	{

		if(stk_CP_inited == 1)//only if STK service is ready and SIM is ready, download profile and set ready
		{
			at_send_command_sync(CHANNEL_ID(SIM), "AT+MSTK=1, FFFFFFFFFF0F1FFF7F031F1F439090E703000F00; +MSTK=2", SINGLELINE, NULL, NULL, NULL);
		}
		stk_app_inited = 1;
		RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
		continue_flag = 0;
		break;
	}

	default:
	{
		LOGW("%s:invalid request:%d\n", __FUNCTION__, request);
		continue_flag = 0;
	}
	}

	/* Set AT cmd type and callback func */
	if (continue_flag)
	{
		int index;
		if ((index = getTableIndex(table_sim, request, NUM_ELEMS(table_sim))) != -1)
		{
			*pType = table_sim[index].type;
			*pCallback = table_sim[index].callback;
		}
		else
		{
			LOGW("%s: Can't find the request in table:%d\n", __FUNCTION__, request);
			continue_flag = 0;
		}
	}

	return continue_flag;
}

static int parseSimStatusString(char *line)
{
	int status, err;
	char *result;

	err = at_tok_start(&line);
	if (err < 0)
	{
		status = SIM_NOT_READY;
		goto done;
	}

	err = at_tok_nextstr(&line, &result);
	if (err < 0)
	{
		status = SIM_NOT_READY;
		goto done;
	}

	if (0 == strcmp(result, "SIM PIN"))
	{
		status = SIM_PIN;
		goto done;
	}
	else if (0 == strcmp(result, "SIM PUK"))
	{
		status = SIM_PUK;
		goto done;
	}
	else if (0 == strcmp(result, "PH-NET PIN"))
	{
		status = SIM_NETWORK_PERSONALIZATION;
		goto done;
	}
	else if (0 == strcmp(result, "REMOVED"))
	{
		status = SIM_ABSENT;
		goto done;
	}
	else if (0 != strcmp(result, "READY"))
	{
		/* we're treating unsupported lock types as "sim absent" */
		status = SIM_ABSENT;
		goto done;
	}
	else
	{
		status = SIM_READY;
	}

 done:
	sSimStatus = status;
	LOGI("[%s]: set sSimStatus=%d", __FUNCTION__, sSimStatus);

	return status;
}

static int parseSimStatus(ATResponse* response)
{
	int status, err;
	char *cpinLine, *cpinResult;

	switch (at_get_cme_error(response))
	{
	case CME_SUCCESS:
	{
		break;
	}
	case CME_SIM_NOT_INSERTED:
	{
		status = SIM_ABSENT;
		goto done;
	}
	case CME_SIM_UNKNOWN_ERROR:
	{
		/* some time, CP will return CME_SIM_UNKNOWN_ERROR if there is no SIM card in slot  */
		if (SIM_ABSENT == sSimStatus)
		{
			status = SIM_ABSENT;
			goto done;
		}
		else
		{
			status = SIM_NOT_READY;
			goto done;
		}
	}
	default:
	{
		status = SIM_NOT_READY;
		goto done;
	}
	}

	/* +CPIN? has succeeded, now look at the result */
	cpinLine = response->p_intermediates->line;
	status = parseSimStatusString(cpinLine);

 done:
	sSimStatus = status;
	LOGI("[%s]: set sSimStatus=%d", __FUNCTION__, sSimStatus);

	return status;
}

/* Do some init work after receiving msg to indicate of CP PBK module is ready */
void InitPbkAndStk(void* param)
{
	/* Select PBK location first for Marvell modem, otherwise other PBK related AT command may work abnormally */
	at_send_command_sync(CHANNEL_ID(SIM), "AT+CPBS=\"SM\"", NO_RESULT, NULL, NULL, NULL);

	if(stk_app_inited == 1) //only if STK service is ready and SIM is ready, download profile and set ready
	{
	/* Init STK and download ME's capability to SIM */
	at_send_command_sync(CHANNEL_ID(SIM), "AT+MSTK=1, FFFFFFFFFF0F1FFF7F031F1F439090E703000F00; +MSTK=2", SINGLELINE, NULL, NULL, NULL);
	}
	
	stk_CP_inited = 1;
}

/* Do some init work after SIM is ready */
static void onSimInitReady(void* param)
{
	sSimStatus = SIM_READY;
	LOGI("[%s]: set sSimStatus=%d", __FUNCTION__, sSimStatus);

	int status = getRadioState();
	if ((status != RADIO_STATE_OFF) && (status != RADIO_STATE_UNAVAILABLE) && (status != RADIO_STATE_SIM_READY))
	{
		setRadioState(RADIO_STATE_SIM_READY);
	}

	/* Always send SMS messages directly to the TE, refer to onSmsInitReady() in ril-msg.c */
	at_send_command_sync(CHANNEL_ID(MSG), "AT+CNMI=1,2,2,1,1", NO_RESULT, NULL, NULL, NULL);
}

static int processStkUnsol(int cmdType, char *data)
{
	if (cmdType < 10)
	{
		LOGE("[%s]: error happens in +MSTK: %d, cmdType should be greater than 10", __FUNCTION__, cmdType);
		return -1;
	}

	/*SIM originated proactive command to ME */
	else if ((cmdType == STK_CMD_PROACTIVE) && (data != NULL) && (strlen(data) > 0))
	{
		RIL_onUnsolicitedResponse( RIL_UNSOL_STK_PROACTIVE_COMMAND, data, strlen(data));
	}

	/*SIM originated call setup request */
	else if ((cmdType == STK_CMD_SETUP_CALL) && (data != NULL) && (strlen(data) > 0))
	{
		RIL_onUnsolicitedResponse( RIL_UNSOL_STK_EVENT_NOTIFY, data, strlen(data));
	}

	/*SIM originated event notify, such as display sth in UI screen */
	else if ((cmdType == STK_CMD_DISPLAY_INFO) && (data != NULL) && (strlen(data) > 0))
	{
		RIL_onUnsolicitedResponse( RIL_UNSOL_STK_EVENT_NOTIFY, data, strlen(data));
	}

	/*SIM originated to notify ME that STK session is terminated by SIM */
	else if ((cmdType == STK_CMD_END_SESSION))
	{
		RIL_onUnsolicitedResponse( RIL_UNSOL_STK_SESSION_END, NULL, 0);
	}

	else
	{
		LOGE("[%s]: error happens in +MSTK: %d, %s. ", __FUNCTION__, cmdType, data);
		return -1;
	}

	return 0;

}

void onUnsolicited_sim (const char *s)
{
	char *line = NULL;
	int err;
	char *linesave = NULL;

	/* Receive pin status ind msg */
	if (strStartsWith(s, "+CPIN:"))
	{
		int status;

		linesave = strdup(s);
		status = parseSimStatusString(linesave);
		/* get sim card type after receiving SIM READY */
		if (status == SIM_READY)
			RIL_requestTimedCallback(getAppType, NULL, NULL);
		if(getRadioState() == RADIO_STATE_OFF)
		{
			;//do nothing
		}
		else if (status == SIM_READY)
		{
			const struct timeval TIMEVAL_DELAY = { 1, 0 };
			RIL_requestTimedCallback(onSimInitReady, NULL, &TIMEVAL_DELAY);
		}
		else if (status == SIM_NOT_READY)
		{
			setRadioState(RADIO_STATE_SIM_NOT_READY);
		}
		else
		{
			setRadioState(RADIO_STATE_SIM_LOCKED_OR_ABSENT);
		}
	}

	/* PBK module in CP has finished initialization */
	else if (strStartsWith(s, "+MPBK: 1"))
	{
		/* Update state to SIM ready for safety. Update local variable  */
		RIL_requestTimedCallback(onSimInitReady, NULL, NULL);

		/* Init STK after all SIM IO operations are finished for better efficiency. Send AT command */
		/* If connection with upper RIL is not established, call this function later */
		{
			const struct timeval TIMEVAL_15s = { 15, 0 };
			RIL_requestTimedCallback(InitPbkAndStk, NULL, &TIMEVAL_15s);
		}
	}

	else if (strStartsWith(s, "+MSTK:"))
	{
		int cmdType = 0;
		char *data;

		line = strdup(s);
		linesave = line;
		at_tok_start(&line);
		err = at_tok_nextint(&line, &cmdType);
		if (err < 0) goto error;
		if (at_tok_hasmore(&line))
		{
			err = at_tok_nextstr(&line, &data);
			if (err < 0) goto error;
		}
		err = processStkUnsol(cmdType, data);
		if (err < 0) goto error;
	}

	/* Free allocated memory and return */
	if (linesave != NULL) free(linesave);
	return;

 error:
	LOGE("[%s]: error happens when parsing ind msg %s", __FUNCTION__, s);
	if (linesave != NULL) free(linesave);
	return;
}

int onConfirm_sim(ATResponse* response, struct requestSession* session)
{
	int done = session->callback(response, session);

	return done;
}

/* Process AT reply of RIL_REQUEST_GET_SIM_STATUS */
int callback_GetSimStatus(ATResponse* response, struct requestSession* session)
{
	static RIL_AppStatus app_status_array[] = {
		/* SIM_ABSENT = 0 */
		{ RIL_APPTYPE_UNKNOWN, RIL_APPSTATE_UNKNOWN,		RIL_PERSOSUBSTATE_UNKNOWN,
		  NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
		/* SIM_NOT_READY = 1 */
		{ RIL_APPTYPE_SIM,     RIL_APPSTATE_DETECTED,		RIL_PERSOSUBSTATE_UNKNOWN,
		  NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
		/* SIM_READY = 2 */
		{ RIL_APPTYPE_SIM,     RIL_APPSTATE_READY,		RIL_PERSOSUBSTATE_READY,
		  NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
		/* SIM_PIN = 3 */
		{ RIL_APPTYPE_SIM,     RIL_APPSTATE_PIN,		RIL_PERSOSUBSTATE_UNKNOWN,
		  NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN },
		/* SIM_PUK = 4 */
		{ RIL_APPTYPE_SIM,     RIL_APPSTATE_PUK,		RIL_PERSOSUBSTATE_UNKNOWN,
		  NULL, NULL, 0, RIL_PINSTATE_ENABLED_BLOCKED, RIL_PINSTATE_UNKNOWN },
		/* SIM_NETWORK_PERSONALIZATION = 5 */
		{ RIL_APPTYPE_SIM,     RIL_APPSTATE_SUBSCRIPTION_PERSO, RIL_PERSOSUBSTATE_SIM_NETWORK,
		  NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN }
	};
	RIL_CardState card_state;
	int num_apps, i, sim_status;
	RIL_CardStatus *p_card_status;

	sim_status = parseSimStatus(response);

	if (sim_status == SIM_ABSENT)
	{
		card_state = RIL_CARDSTATE_ABSENT;
		num_apps = 0;
	}
	else
	{
		card_state = RIL_CARDSTATE_PRESENT;
		num_apps = 1;
	}

	/* Allocate and initialize base card status. */
	p_card_status = malloc(sizeof(RIL_CardStatus));
	p_card_status->card_state = card_state;
	p_card_status->universal_pin_state = RIL_PINSTATE_UNKNOWN;
	p_card_status->gsm_umts_subscription_app_index = RIL_CARD_MAX_APPS;
	p_card_status->cdma_subscription_app_index = RIL_CARD_MAX_APPS;
	p_card_status->num_applications = num_apps;

	/* Initialize application status. */
	for (i = 0; i < RIL_CARD_MAX_APPS; i++)
	{
		p_card_status->applications[i] = app_status_array[SIM_ABSENT];
	}

	if(sim_status != SIM_ABSENT)
	{
		if(sAppType != RIL_APPTYPE_UNKNOWN)
		{
			for(i = SIM_ABSENT + 1; i < (int)sizeof(app_status_array) / (int)sizeof(app_status_array[0]); ++i)
			{
				app_status_array[i].app_type = sAppType;
			}
		}
	}

	/* Pickup the appropriate application status that reflects sim_status for gsm. */
	if (num_apps != 0)
	{
		/* Only support one app, gsm. */
		p_card_status->num_applications = 1;
		p_card_status->gsm_umts_subscription_app_index = 0;

		/* Get the correct app status. */
		p_card_status->applications[0] = app_status_array[sim_status];
	}

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, (char*)p_card_status, sizeof(RIL_CardStatus));

	free(p_card_status);
	return 1;
}


/* Common function to process reply of AT+CPIN= */
static int replyCpin(ATResponse* response, struct requestSession* session, int request)
{
	int retryLeft = -1;

	if (response->success != 0)
	{
		RIL_onRequestComplete(session->token, RIL_E_SUCCESS, &retryLeft, sizeof(int));
		return 1;
	}

	session->callback = callback_RequestLeftPinRetry;
	session->type = SINGLELINE;
	session->needResponse = 1;
	session->channelID = CHANNEL_ID(SIM);
	strcpy(session->cmdString, "AT+EPIN?");
	strcpy(session->responsePrefix, "+EPIN:");


	LOGV("%s: get response from channel", __FUNCTION__);
	RIL_requestTimedCallback(continueProcessRequest, &descriptions[session->channelID], &TIMEVAL_0);
	return 0;

}

/* Process AT reply of RIL_REQUEST_ENTER_SIM_XXX */
int callback_RequestLeftPinRetry(ATResponse* response, struct requestSession* session)
{
	int err, pin1num, pin2num, puk1num, puk2num, retryLeft = -1;
	char *line;

	if (response->success == 0) goto error;

	line = response->p_intermediates->line;
	at_tok_start(&line);
	err = at_tok_nextint(&line, &pin1num);
	if (err < 0) goto error;
	err = at_tok_nextint(&line, &pin2num);
	if (err < 0) goto error;
	err = at_tok_nextint(&line, &puk1num);
	if (err < 0) goto error;
	err = at_tok_nextint(&line, &puk2num);
	if (err < 0) goto error;

	switch (session->request)
	{
	case RIL_REQUEST_ENTER_SIM_PIN:
	case RIL_REQUEST_CHANGE_SIM_PIN:
	case RIL_REQUEST_SET_FACILITY_LOCK:
		retryLeft  = pin1num;
		break;

	case RIL_REQUEST_ENTER_SIM_PIN2:
	case RIL_REQUEST_CHANGE_SIM_PIN2:
		retryLeft  = pin2num;
		break;

	case RIL_REQUEST_ENTER_SIM_PUK:
		retryLeft  = puk1num;
		break;

	case RIL_REQUEST_ENTER_SIM_PUK2:
		retryLeft  = puk2num;
		break;
	}

error:
	LOGI("[%s]: retryLeft =%d", __FUNCTION__, retryLeft);
	RIL_onRequestComplete(session->token, RIL_E_PASSWORD_INCORRECT, &retryLeft, sizeof(int));
	return 1;
}


/* Process AT reply of RIL_REQUEST_SET_FACILITY_LOCK */
int callback_RequestFacilityLock(ATResponse* response, struct requestSession* session)
{
	int ret =  replyCpin(response, session, RIL_REQUEST_SET_FACILITY_LOCK);
	return ret;
}


/* Process AT reply of RIL_REQUEST_ENTER_SIM_PIN */
int callback_RequestEnterSimPin(ATResponse* response, struct requestSession* session)
{
	int ret =  replyCpin(response, session, RIL_REQUEST_ENTER_SIM_PIN);
	return ret;
}

/* Process AT reply of RIL_REQUEST_ENTER_SIM_PUK */
int callback_RequestEnterSimPuk(ATResponse* response, struct requestSession* session)
{
	int ret = replyCpin(response, session, RIL_REQUEST_ENTER_SIM_PUK);
	return ret;
}

/* Process AT reply of RIL_REQUEST_ENTER_SIM_PIN2 */
int callback_RequestEnterSimPin2(ATResponse* response, struct requestSession* session)
{
	int ret = replyCpin(response, session, RIL_REQUEST_ENTER_SIM_PIN2);
	return ret;
}
/* Process AT reply of RIL_REQUEST_ENTER_SIM_PUK2 */
int callback_RequestEnterSimPuk2(ATResponse* response, struct requestSession* session)
{
	int ret = replyCpin(response, session, RIL_REQUEST_ENTER_SIM_PUK2);
	return ret;
}

/* Process AT reply of RIL_REQUEST_CHANGE_SIM_PIN */
int callback_RequestChangeSimPin(ATResponse* response, struct requestSession* session)
{
	int ret = replyCpin(response, session, RIL_REQUEST_CHANGE_SIM_PIN);
	return ret;
}

/* Process AT reply of RIL_REQUEST_CHANGE_SIM_PIN2 */
int callback_RequestChangeSimPin2(ATResponse* response, struct requestSession* session)
{
	int ret = replyCpin(response, session, RIL_REQUEST_CHANGE_SIM_PIN2);
	return ret;
}

/* Process AT reply of RIL_REQUEST_GET_IMSI */
int callback_RequestGetIMSI(ATResponse* response, struct requestSession* session)
{
	if (response->success == 0)
	{
		RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	}
	else
	{
		char *strImsi;
		strImsi = response->p_intermediates->line;
		RIL_onRequestComplete(session->token, RIL_E_SUCCESS, strImsi, sizeof(strImsi));
	}
	return 1;

}

/* Process AT reply of RIL_REQUEST_SIM_IO */
int callback_RequestSimIO(ATResponse* response, struct requestSession* session)
{

	RIL_SIM_IO_Response sr;
	int err;
	RIL_SIM_IO *p_args;
	char *line;

	memset(&sr, 0, sizeof(sr));

	if (response->success == 0) goto error;

	line = response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &(sr.sw1));
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &(sr.sw2));
	if (err < 0) goto error;

	if (at_tok_hasmore(&line))
	{
		err = at_tok_nextstr(&line, &(sr.simResponse));
		if (err < 0) goto error;
	}

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, &sr, sizeof(sr));
	return 1;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

static int parseStkResponse(char *s, int *pCmdType, char **pData)
{
	char *line = s, *data;
	int err, cmdType;

	at_tok_start(&line);
	err = at_tok_nextint(&line, &cmdType);
	if (err < 0) goto error;
	*pCmdType = cmdType;

	if (at_tok_hasmore(&line))
	{
		err = at_tok_nextstr(&line, &data);
		if (err < 0) goto error;
		*pData = data;
	}
	else
	{
		*pData = NULL;
	}

	return 0;

 error:
	LOGE("[%s]: error happens when parsing response msg %s", __FUNCTION__, s);
	*pData = NULL;
	return -1;
}

/* Process AT reply of RIL_REQUEST_STK_GET_PROFILE */
int callback_RequestStkGetProfile(ATResponse* response, struct requestSession* session)
{
	char *data;
	int cmdType, err;

	if (response->success == 0)
	{
		RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	}
	else
	{
		if (response->finalResponse)
		{
			RIL_onRequestComplete(session->token, RIL_E_SUCCESS, NULL, 0);
			return 1;
		}

		if (!response->p_intermediates)
		{
			RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
			return 1;
		}

		err = parseStkResponse(response->p_intermediates->line, &cmdType, &data);
		if ((err == 0) && (data != NULL) && (cmdType == STK_CMD_GET_PROFILE))
		{
			RIL_onRequestComplete(session->token, RIL_E_SUCCESS, data, sizeof(data));
		}
		else
		{
			RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
		}
	}

	return 1;
}

/* Process AT reply of RIL_REQUEST_STK_SEND_ENVELOPE_COMMAND */
int callback_RequestStkSendEnvelope(ATResponse* response, struct requestSession* session)
{
	char *data = NULL;
	int cmdType, err;

	if (response->success == 0)
	{
		RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	}
	else
	{
		if (response->finalResponse)
		{
			RIL_onRequestComplete(session->token, RIL_E_SUCCESS, NULL, 0);
			return 1;
		}

		if (!response->p_intermediates)
		{
			RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
			return 1;
		}

		err = parseStkResponse(response->p_intermediates->line, &cmdType, &data);
		if ((err == 0) && (cmdType == STK_CMD_SEND_ENVELOPE))
		{
			if (data != NULL)
			{
				RIL_onRequestComplete(session->token, RIL_E_SUCCESS, data, sizeof(data));
			}
			else
			{
				RIL_onRequestComplete(session->token, RIL_E_SUCCESS, NULL, 0);
			}
		}
		else
		{
			RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
		}
	}

	return 1;
}

/* External func to query current SIM status */
int getSimStatus(void)
{
	ATResponse *p_response = NULL;
	int err, status;

	err = at_send_command_sync(CHANNEL_ID(DEV), "AT+CPIN?", SINGLELINE, "+CPIN:", NULL, &p_response);
	if (err < 0)
	{
		status = SIM_NOT_READY;
	}
	else
	{
		status = parseSimStatus(p_response);
	}

	at_response_free(p_response);
	return status;
}

/* Update radio state according to SIM state */
void updateRadioState(void)
{
	int simStatus = getSimStatus();

	if ((simStatus == SIM_ABSENT) || (simStatus == SIM_PIN)
	    || (simStatus == SIM_PUK)  || (simStatus == SIM_NETWORK_PERSONALIZATION))
	{
		setRadioState(RADIO_STATE_SIM_LOCKED_OR_ABSENT);
	}
	else if (simStatus == SIM_READY)
	{
		setRadioState(RADIO_STATE_SIM_READY);
	}
	else if (simStatus == SIM_NOT_READY)
	{
		setRadioState(RADIO_STATE_SIM_NOT_READY);
	}
	else
	{
		LOGD("Unexpected branch in %s", __FUNCTION__);
	}
}
/* Sometimes when receiving +CPIN: READY, but network is not registered, to speed up registration,
 * RADIO_STATE_SIM_READY will be sent later after network is registered
 */
void notifyRegisteredToSim(void)
{
	int status = getRadioState();

	if ((sSimStatus == SIM_READY) && (status != RADIO_STATE_SIM_READY))
	{
		setRadioState(RADIO_STATE_SIM_READY);
	}
}

static void getAppType(void *param)
{
	if(sAppType == RIL_APPTYPE_UNKNOWN)
	{
		ATResponse *p_response = NULL;
		int err;

		err = at_send_command_sync(CHANNEL_ID(SIM), "AT*EUICC?", SINGLELINE, "*EUICC:", NULL, &p_response);
		if (err < 0)
		{
			sAppType = RIL_APPTYPE_UNKNOWN;
			goto done;
		}
		else
		{
			char *line = p_response->p_intermediates->line;
			int type;

			err = at_tok_start(&line);
			if (err < 0)
			{
				sAppType = RIL_APPTYPE_UNKNOWN;
				goto done;
			}

			err = at_tok_nextint(&line, &type);
			if (err < 0)
			{
				sAppType = RIL_APPTYPE_UNKNOWN;
				goto done;
			}

			if(type == 0)
				sAppType = RIL_APPTYPE_SIM;
			else if(type == 1)
				sAppType = RIL_APPTYPE_USIM;
			else
				sAppType = RIL_APPTYPE_UNKNOWN;
		}
done:
		at_response_free(p_response);
	}

	LOGD("%s: UICC type: %d\n", __FUNCTION__, sAppType);
}

