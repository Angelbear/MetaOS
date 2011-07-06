/******************************************************************************
 * *(C) Copyright 2008 Marvell International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/


#include "marvell-ril.h"

/* Save last +CLCC unsol msg string */
static char sLastClccUnsolBuf[100] = "";

int callback_GetCurrentCalls(ATResponse* response, struct requestSession* session);
int callback_RequestGetCallCost(ATResponse* response, struct requestSession* session);


/* Note: For call control AT response, upper layer will call GET_CURRENT_CALLS and determine success that way */
RIL_AT_Map table_cc[] = {
	{ RIL_REQUEST_GET_CURRENT_CALLS,		    MULTILINE,					callback_GetCurrentCalls    },
	{ RIL_REQUEST_LAST_CALL_FAIL_CAUSE,		    SINGLELINE,					callback_ReturnOneInt	    },
	{ RIL_REQUEST_DIAL,				    NO_RESULT,					callback_DefaultResponse    },
	{ RIL_REQUEST_DIAL_VT, 				NO_RESULT,					callback_DefaultResponse	},
	{ RIL_REQUEST_ANSWER,				    NO_RESULT,					callback_DefaultResponse    },
	{ RIL_REQUEST_HANGUP,				    NO_RESULT,					callback_DefaultSuccess	    },
	{ RIL_REQUEST_HANGUP_VT,					NO_RESULT,					callback_DefaultSuccess 	},
	{ RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND,	    NO_RESULT,					callback_DefaultSuccess	    },
	{ RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND,  NO_RESULT,					callback_DefaultSuccess	    },
	{ RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE, NO_RESULT,					callback_DefaultResponse    },
	{ RIL_REQUEST_CONFERENCE,			    NO_RESULT,					callback_DefaultResponse    },
	{ RIL_REQUEST_SEPARATE_CONNECTION,		    NO_RESULT,					callback_DefaultResponse    },
	{ RIL_REQUEST_EXPLICIT_CALL_TRANSFER,		    NO_RESULT,					callback_DefaultResponse    },
	{ RIL_REQUEST_DTMF,				    NO_RESULT,					callback_DefaultResponse    },
	{ RIL_REQUEST_DTMF_START,			    NO_RESULT,					callback_DefaultResponse    },
	{ RIL_REQUEST_DTMF_STOP,			    NO_RESULT,					callback_DefaultResponse    },
	{ RIL_REQUEST_SET_MUTE,				    NO_RESULT,					callback_DefaultResponse    },
	{ RIL_REQUEST_GET_MUTE,				    SINGLELINE,					callback_ReturnOneInt	    },
	{ RIL_REQUEST_UDUB,				    NO_RESULT,					callback_DefaultResponse    },
	/* Accumulated call meter related */
	{RIL_REQUEST_SET_ACM,				    NO_RESULT, 					callback_DefaultResponse    },
	{RIL_REQUEST_GET_ACM,				    SINGLELINE,					callback_RequestGetCallCost },
	{RIL_REQUEST_SET_AMM,				    NO_RESULT,					callback_DefaultResponse    },
	{RIL_REQUEST_GET_AMM,				    SINGLELINE,					callback_RequestGetCallCost },
	{RIL_REQUEST_SET_CPUC,				    NO_RESULT,					callback_DefaultResponse    },
	{RIL_REQUEST_GET_CPUC,				    SINGLELINE,					callback_RequestGetCallCost },
};

int onRequest_cc (int request, void *data, size_t datalen, RIL_Token token, char *cmdString,
		  ATCommandType *pType, char *prefix, requestCallback *pCallback)
{
	int continue_flag = 1;

	switch (request)
	{
	case RIL_REQUEST_GET_CURRENT_CALLS:
	{
		RIL_RadioState currentState;
		currentState = getRadioState();

		/* Android will call +CLCC to clean variable when RADIO_OFF, return radio_available directly */
		if (( currentState == RADIO_STATE_SIM_NOT_READY ) && ( strlen(sLastClccUnsolBuf) == 0 ))
		{
			RIL_onRequestComplete(token, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
			continue_flag = 0;
		}
		else
		{
			strcpy(cmdString, "AT+CLCC");
			strcpy(prefix, "+CLCC:");
		}
		break;
	}

	case RIL_REQUEST_LAST_CALL_FAIL_CAUSE:
	{
		strcpy(cmdString, "AT+CEER");
		strcpy(prefix, "+CEER:");
		break;
	}

	case RIL_REQUEST_DIAL:
	{
		RIL_Dial *p_dial;
		const char *clir;
		p_dial = (RIL_Dial *)data;

		switch (p_dial->clir)
		{
		case 1:
			clir = "I"; /*invocation*/
			break;
		case 2:
			clir = "i"; /*suppression*/
			break;
		case 0:
		default:
			clir = ""; /*subscription default*/
		}
		sprintf(cmdString, "ATD%s%s;", p_dial->address, clir);
		break;
	}
	
	case RIL_REQUEST_DIAL_VT:
	{
		RIL_Dial *p_dial;
		p_dial = (RIL_Dial *)data;

		LOGV("RIL_REQUEST_DIAL_VT: %s", p_dial->address);
		LOGD("RIL_REQUEST_DIAL_VT: %s", p_dial->address);
		sprintf(cmdString, "ATD%s", p_dial->address);
		break;
	}

	case RIL_REQUEST_ANSWER:
	{
		strcpy(cmdString, "ATA");
		break;
	}

	case RIL_REQUEST_HANGUP:
	{
		int *p_line;
		p_line = (int *)data;

		LOGD("RIL_REQUEST_HANGUP: %d", p_line[0]);
		sprintf(cmdString, "AT+CHLD=1%d", p_line[0]);
		//strcpy(cmdString, "ATH");
		break;
	}

	case RIL_REQUEST_HANGUP_VT:
	{
		int *p_line;
		p_line = (int *)data;
		LOGV("RIL_REQUEST_HANGUP_VT: %d", p_line[0]);
		LOGD("RIL_REQUEST_HANGUP_VT: %d", p_line[0]);
		sprintf(cmdString, "AT+ECHUPVT=%d", p_line[0]);
		break;
	}

	case RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND:
	{
		strcpy(cmdString, "AT+CHLD=0");
		break;
	}

	case RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND:
	{
		strcpy(cmdString, "AT+CHLD=1");
		break;
	}

	case RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE:
		//case RIL_REQUEST_SWITCH_HOLDING_AND_ACTIVE:
	{
		strcpy(cmdString, "AT+CHLD=2");
		break;
	}

	case RIL_REQUEST_CONFERENCE:
	{
		strcpy(cmdString, "AT+CHLD=3");
		break;
	}

	case RIL_REQUEST_UDUB:
	{
		strcpy(cmdString, "AT+CHLD=0");
		break;
	}

	case RIL_REQUEST_DTMF:
	{
		char c = ((char *)data)[0];
		char duration = ((char*)data)[1];
		LOGV("RIL_REQUEST_DTMF:%c,%c", c, duration);
		sprintf(cmdString, "AT+VTS=%c", (int)c);
		break;
	}

	case RIL_REQUEST_DTMF_START:
	{
		char c = ((char *)data)[0];
		LOGV("RIL_REQUEST_DTMF_START:%c", c);
		sprintf(cmdString, "AT+VTS=%c", (int)c);
		break;
	}

	case RIL_REQUEST_DTMF_STOP:
	{
		LOGV("RIL_REQUEST_DTMF_STOP");
		strcpy(cmdString, "AT");
		break;
	}

	case RIL_REQUEST_SET_MUTE:
	{
		int mute = ((int*)data)[0];
		sprintf(cmdString, "AT+CMUT=%d", mute);
		break;
	}

	case RIL_REQUEST_GET_MUTE:
	{
		strcpy(cmdString, "AT+CMUT?");
		strcpy(prefix, "+CMUT:");
		break;
	}

	case RIL_REQUEST_SEPARATE_CONNECTION:
	{
		int party = ((int*)data)[0];
		// Make sure that party is in a valid range.
		// (Note: The Telephony middle layer imposes a range of 1 to 7.
		// It's sufficient for us to just make sure it's single digit.)
		if (party <= 0 || party >= 10)
			party = 1;
		sprintf(cmdString, "AT+CHLD=2%d", party);
		break;
	}

	case RIL_REQUEST_EXPLICIT_CALL_TRANSFER:
	{
		strcpy(cmdString, "AT+CHLD=4");
		break;
	}

	case RIL_REQUEST_SET_ACM:
	{
		const char** strings = (const char**)data;
		switch (datalen/sizeof(char *))  {
			case 1:
				sprintf(cmdString, "AT+CACM=%s", strings[0]);
				break;
			default:
				sprintf(cmdString, "AT+CACM=");
				break;
		}
		break;
	}

	case RIL_REQUEST_GET_ACM:
	{
		sprintf(cmdString, "AT+CACM=?");
		strcpy(prefix, "+CACM:");
		break;
	}

	case RIL_REQUEST_SET_AMM:
	{
		const char** strings = (const char**)data;
		switch (datalen/sizeof(char *))  {
			case 1:
				sprintf(cmdString, "AT+CAMM=%s", strings[0]);
				break;
			case 2:
				sprintf(cmdString, "AT+CAMM=%s, %s", strings[0], strings[1] );
				break;
			default:
				sprintf(cmdString, "AT+CAMM=");
				break;
		}

		break;
	}
	case RIL_REQUEST_GET_AMM:
	{
		sprintf(cmdString, "AT+CAMM=?");
		strcpy(prefix, "+CAMM:");
		break;
	}

	case RIL_REQUEST_SET_CPUC:
	{
		const char** strings = (const char**)data;
		switch (datalen/sizeof(char *)) {
			case 2:
				sprintf(cmdString, "AT+CPUC=%s,%s", strings[0], strings[1]);
				break;
			case 3:
				sprintf(cmdString, "AT+CPUC=%s,%s,%s", strings[0], strings[1], strings[2]);
				break;
			default:
				sprintf(cmdString, "AT+CPUC=");
				break;
		}
		break;
	}

	case RIL_REQUEST_GET_CPUC:
	{
		sprintf(cmdString, "AT+CPUC?");
		strcpy(prefix, "+CPUC:");
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
		if ((index = getTableIndex(table_cc, request, NUM_ELEMS(table_cc))) != -1)
		{
			*pType = table_cc[index].type;
			*pCallback = table_cc[index].callback;
		}
		else
		{
			LOGW("%s: Can't find the request in table:%d\n", __FUNCTION__, request);
			continue_flag = 0;
		}
	}

	return continue_flag;
}

void onUnsolicited_cc (const char *s)
{
	if (strStartsWith(s, "+CRING:") || strStartsWith(s, "RING")
	    || strStartsWith(s, "NO CARRIER") || strStartsWith(s, "+CCWA"))
	{
		RIL_onUnsolicitedResponse( RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED, NULL, 0);
	}

	else if (strStartsWith(s, "+CLCC:") )
	{
		RIL_onUnsolicitedResponse( RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED, NULL, 0);
	}
}

int onConfirm_cc(ATResponse* response, struct requestSession* session)
{
	int done = session->callback(response, session);

	return done;
}

/*
   static void sendCallStateChanged(void *param)
   {
    RIL_onUnsolicitedResponse ( RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED, NULL, 0);
   }*/

static int convertClccStateToRILState(int state, RIL_CallState *p_state)
{
	switch (state)
	{
	case 0: *p_state = RIL_CALL_ACTIVE;   return 0;
	case 1: *p_state = RIL_CALL_HOLDING;  return 0;
	case 2: *p_state = RIL_CALL_DIALING;  return 0;
	case 3: *p_state = RIL_CALL_ALERTING; return 0;
	case 4: *p_state = RIL_CALL_INCOMING; return 0;
	case 5: *p_state = RIL_CALL_WAITING;  return 0;
	default: return -1;
	}
}

/**
 * Note: directly modified line and has *p_call point directly into
 * modified line. AT response format like:
 *  +CLCC: 1,0,2,0,0,\"+18005551212\",145
 *        index,isMT,state,mode,isMpty(,number,TOA)?
 */
static int getInfoFromClccLine(char *line, RIL_Call *p_call)
{
	int err, state, mode;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &(p_call->index));
	if (err < 0) goto error;

	err = at_tok_nextbool(&line, &(p_call->isMT));
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &state);
	if (err < 0) goto error;

	err = convertClccStateToRILState(state, &(p_call->state));
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &mode);
	if (err < 0) goto error;

	p_call->isVoice = (mode == 0);

	err = at_tok_nextbool(&line, &(p_call->isMpty));
	if (err < 0) goto error;

	if (at_tok_hasmore(&line))
	{
		err = at_tok_nextstr(&line, &(p_call->number));
		/* tolerate null here */
		if (err < 0) return 0;

		//[Jerry] why?? (inherited code)
		// Some lame implementations return strings
		// like "NOT AVAILABLE" in the CLCC line
		if ((p_call->number != NULL) && (0 == strspn(p_call->number, "+0123456789")))
		{
			p_call->number = NULL;
		}

		err = at_tok_nextint(&line, &p_call->toa);
		if (err < 0) goto error;
	}

	return 0;

 error:
	LOGE("invalid CLCC line\n");
	return -1;
}


/* Process AT reply of RIL_REQUEST_GET_CURRENT_CALLS */
int callback_GetCurrentCalls(ATResponse* response, struct requestSession* session)
{
	ATLine *p_cur;
	int countCalls;
	int countValidCalls;
	RIL_Call *p_calls;
	RIL_Call **pp_calls;
	int i, err;
	int needRepoll = 0;

	/* If not success, report failure directly */
	if (response->success == 0)
	{
		/* [Jerry] it is better to report failure instead of reporting all zero   ???? */
		countCalls = 0;
		pp_calls = (RIL_Call **)alloca(countCalls * sizeof(RIL_Call *));
		p_calls = (RIL_Call *)alloca(countCalls * sizeof(RIL_Call));
		memset(p_calls, 0, countCalls * sizeof(RIL_Call));
		RIL_onRequestComplete(session->token, RIL_E_SUCCESS, pp_calls, countCalls * sizeof(RIL_Call *));

		//      RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
		return 1;
	}

	/* count the calls */
	for (countCalls = 0, p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next)
	{
		countCalls++;
	}

	/* yes, there's an array of pointers and then an array of structures */
	pp_calls = (RIL_Call **)alloca(countCalls * sizeof(RIL_Call *));
	p_calls = (RIL_Call *)alloca(countCalls * sizeof(RIL_Call));
	memset(p_calls, 0, countCalls * sizeof(RIL_Call));

	/* init the pointer array */
	for (i = 0; i < countCalls; i++)
	{
		pp_calls[i] = &(p_calls[i]);
	}

	/* Analyze AT response and report */
	for (countValidCalls = 0, p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next)
	{
		err = getInfoFromClccLine(p_cur->line, p_calls + countValidCalls);
		if (err != 0) continue;

		countValidCalls++;

#if 0
		if (p_calls[countValidCalls].state != RIL_CALL_ACTIVE
		    && p_calls[countValidCalls].state != RIL_CALL_HOLDING
//             && p_calls[countValidCalls].state != RIL_CALL_INCOMING
		    )
		{
			needRepoll = 1;
		}
#endif

	}

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, pp_calls, countValidCalls * sizeof(RIL_Call *));

//[Jerry] Will CP send new +CLCC when call stated changed? Should we poll it again and again?
#if 0
#ifdef POLL_CALL_STATE
	if (countValidCalls) // We don't seem to get a "NO CARRIER" message from
	{                   // smd, so we're forced to poll until the call ends.
#else
	if (needRepoll)
	{
#endif
		RIL_requestTimedCallback(sendCallStateChanged, NULL, &TIMEVAL_CALLSTATEPOLL);
	}
#endif

	return 1;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;

}

int callback_RequestGetCallCost(ATResponse* response, struct requestSession* session)
{
	char responseStr[RIL_ACM_AMM_DIG_LENGTH + 1];
	char *temp;
	char *result[2];
	char *line;
	int err;

	if (response->success == 0) goto error;

	line = response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	switch(session->request)
	{
	case RIL_REQUEST_GET_ACM:
	{
		err = at_tok_nextstr(&line, &temp);
		strcpy(responseStr, temp);
		if (err < 0) goto error;
		RIL_onRequestComplete(session->token, RIL_E_SUCCESS, &responseStr, sizeof(responseStr));
		break;
	}
	case RIL_REQUEST_GET_AMM:
	{
		err = at_tok_nextstr(&line, &temp);
		strcpy(responseStr, temp);
		if (err < 0) goto error;
		RIL_onRequestComplete(session->token, RIL_E_SUCCESS, &responseStr, sizeof(responseStr));
		break;
	}
	case RIL_REQUEST_GET_CPUC:
	{

		err = at_tok_nextstr(&line, &result[0]);
		if (err < 0) goto error;

		err = at_tok_nextstr(&line, &result[1]);
		if (err < 0) goto error;

		RIL_onRequestComplete(session->token, RIL_E_SUCCESS, result, sizeof(result));
		break;
	}
	default:
		goto error;
	}

	return 1;

error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}
