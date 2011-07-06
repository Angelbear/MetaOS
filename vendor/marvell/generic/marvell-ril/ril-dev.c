/******************************************************************************
 * *(C) Copyright 2008 Marvell International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/

#include "marvell-ril.h"

#undef WORKAROUND_FOR_DEMO

void updateRadioState(void);
static int requestRadioPower(void *data, size_t datalen, RIL_Token token);
static void resetRadio(RIL_Token token);
int callback_QueryAvaliableBand(ATResponse* response, struct requestSession* session);
static char s_DeviceIdentity_sv[3] = {0}; // SVN of IMEISV, 2 digits + 1 '\0'
static int callback_DeviceIdentity1(ATResponse* response, struct requestSession* session);
static int callback_DeviceIdentity2(ATResponse* response, struct requestSession* session);

RIL_AT_Map table_dev[] = {
	{ RIL_REQUEST_RADIO_POWER,		 NO_RESULT,		       NULL			  },    //sync AT cmd
	{ RIL_REQUEST_RESET_RADIO,		 NO_RESULT,		       NULL			  },    //sync AT cmd
	{ RIL_REQUEST_GET_IMEI,			 SINGLELINE,		       callback_ReturnOneString	  },
	{ RIL_REQUEST_GET_IMEISV,		 SINGLELINE,		       callback_ReturnOneString	  },
	{ RIL_REQUEST_BASEBAND_VERSION,		 SINGLELINE,		       callback_ReturnOneString	  },

	/* Todo. */
	/* [Jerry] Issue: the band mode definition in Android is different with 3GPP spec +CPWC */
	{ RIL_REQUEST_SET_BAND_MODE,		 NO_RESULT,		       callback_DefaultResponse	  },
	{ RIL_REQUEST_QUERY_AVAILABLE_BAND_MODE, SINGLELINE,	callback_QueryAvaliableBand	  },
	{ RIL_REQUEST_DEVICE_IDENTITY,		 SINGLELINE,		       callback_DeviceIdentity1	  },

	/* Not supported. Reserved for OEM-specific uses and not used now */
	{ RIL_REQUEST_OEM_HOOK_RAW,		 NO_RESULT,		       callback_DefaultResponse	  },
	{ RIL_REQUEST_OEM_HOOK_STRINGS,		 NO_RESULT,		       callback_DefaultResponse	  },
};


int onRequest_dev(int request, void *data, size_t datalen, RIL_Token token, char *cmdString,
		  ATCommandType *pType, char *prefix, requestCallback *pCallback)
{
	int continue_flag = 1;

	switch (request)
	{
	case RIL_REQUEST_RADIO_POWER:
	{
		/* special processing for CFUN, call synchronously */
		continue_flag = requestRadioPower(data, datalen, token);
		break;
	}

	case RIL_REQUEST_RESET_RADIO:
	{
		/* special processing for CFUN, call synchronously */
		resetRadio(token);
		continue_flag = 2;
		break;
	}

	case RIL_REQUEST_GET_IMEI:
	{
		strcpy(cmdString, "AT+CGSN");
		strcpy(prefix, "+CGSN:");
		break;
	}

	case RIL_REQUEST_GET_IMEISV:
	{
		strcpy(cmdString, "AT*CGSN?");
		strcpy(prefix, "*CGSN:");
		break;
	}

	case RIL_REQUEST_BASEBAND_VERSION:
	{
		strcpy(cmdString, "AT+CGMR");
		strcpy(prefix, "+CGMR:");
		break;
	}

	case RIL_REQUEST_QUERY_AVAILABLE_BAND_MODE:
	{
		strcpy(cmdString, "AT*BAND=?");
		strcpy(prefix, "*BAND:");
		break;
	}

	case RIL_REQUEST_DEVICE_IDENTITY:
	{
		// get SV first, then IMEI
		strcpy(cmdString, "AT*CGSN?");
		strcpy(prefix, "*CGSN:");
		break;
	}

	case RIL_REQUEST_OEM_HOOK_RAW:
	case RIL_REQUEST_OEM_HOOK_STRINGS:
	case RIL_REQUEST_SET_BAND_MODE:
	{
		//todo
		RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
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
		if ((index = getTableIndex(table_dev, request, NUM_ELEMS(table_dev))) != -1)
		{
			*pType = table_dev[index].type;
			*pCallback = table_dev[index].callback;
		}
		else
		{
			LOGW("%s: Can't find the request in table:%d\n", __FUNCTION__, request);
			continue_flag = 0;
		}
	}

	return continue_flag;
}

void onUnsolicited_dev (const char *s)
{
	return;
}

int onConfirm_dev(ATResponse* response, struct requestSession* session)
{
	int done = session->callback(response, session);

	return done;
}

/** Query CP power on status, Returns 1 if on, 0 if off, and -1 on error */
int isRadioOn()
{
	ATResponse *p_response = NULL;
	int err;
	char *line;
	char ret = 0;

	err = at_send_command_sync(CHANNEL_ID(DEV), "AT+CFUN?", SINGLELINE, "+CFUN:", NULL, &p_response);
	if (err < 0 || p_response->success == 0)
	{
		// assume radio is off
		goto error;
	}

	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextbool(&line, &ret);
	if (err < 0) goto error;

	at_response_free(p_response);
	return (int)ret;

 error:
	at_response_free(p_response);
	return -1;
}

/* Process RIL_REQUEST_RADIO_POWER */
static int requestRadioPower(void *data, size_t datalen, RIL_Token token)
{
	int onOff, err, status, continue_flag;
	RIL_RadioState currentState;
	ATResponse *p_response = NULL;

	assert(datalen >= sizeof(int *));
	onOff = ((int *)data)[0];

#ifdef WORKAROUND_FOR_DEMO
	RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	return;
#endif

	currentState = getRadioState();

	if ((onOff == 0) && (currentState != RADIO_STATE_OFF))
	{
		err = at_send_command_sync(CHANNEL_ID(DEV), "AT+CFUN=4", NO_RESULT, NULL, NULL, &p_response);
		if (err < 0 || p_response->success == 0) goto error;
		setRadioState(RADIO_STATE_OFF);
		resetLocalRegInfo();
		continue_flag = 2; // sync AT
	}

	else if ((onOff > 0) && (currentState == RADIO_STATE_OFF))
	{
		err = at_send_command_sync(CHANNEL_ID(DEV), "AT+CFUN=1", NO_RESULT, NULL, NULL, &p_response);
		if (err < 0 || p_response->success == 0)
		{
			/* Some stacks return an error when there is no SIM, but they really turn the RF portion on
			 * So, if we get an error, let's check to see if it turned on anyway
			 */
			if (isRadioOn() != 1) goto error;
		}
		updateRadioState();
		continue_flag = 2; // sync AT
	}

	else
	{
		LOGD("Already in current state, return directly");
		continue_flag = 0; // local implementation
	}

	at_response_free(p_response);
	RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	return continue_flag;

 error:
	at_response_free(p_response);
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	continue_flag = 2; // sync AT
	return continue_flag;
}

/* Process RIL_REQUEST_RESET_RADIO */
static void resetRadio(RIL_Token token)
{
	int onOff, err;
	ATResponse *p_response = NULL;

#ifdef WORKAROUND_FOR_DEMO
	RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	return;
#endif

	err = at_send_command_sync(CHANNEL_ID(DEV), "AT+CFUN=4", NO_RESULT, NULL, NULL, &p_response);
	if (err < 0 || p_response->success == 0) goto error;

	setRadioState(RADIO_STATE_OFF);
	resetLocalRegInfo();

	err = at_send_command_sync(CHANNEL_ID(DEV), "AT+CFUN=1", NO_RESULT, NULL, NULL, &p_response);
	if (err < 0 || p_response->success == 0)
	{
		/* Some stacks return an error when there is no SIM, but they really turn the RF portion on
		 * So, if we get an error, let's check to see if it turned on anyway
		 */
		if (isRadioOn() != 1) goto error;
	}

	updateRadioState();

	at_response_free(p_response);
	RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	return;

 error:
	at_response_free(p_response);
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return;
}


/* Process AT reply of RIL_REQUEST_QUERY_AVAILABLE_BAND_MODE */
int callback_QueryAvaliableBand(ATResponse* response, struct requestSession* session)
{
	int err;
	int gsmband, umtsband, result[20];
	char *line;
	char* mode;
	int count = 0;


	if (response->success == 0 || response->p_intermediates == NULL)
	{
	   goto error;
	}

	line = response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0)  goto error;

	err = at_tok_nextstr(&line, &mode);
	if (err < 0)  goto error;

	err = at_tok_nextint(&line, &gsmband);
	if (err < 0)  goto error;

	err = at_tok_nextint(&line, &umtsband);
	if (err < 0)  goto error;


	if((gsmband & GSMBAND_PGSM_900) && (gsmband & GSMBAND_DCS_GSM_1800) && (umtsband & UMTSBAND_BAND_1))
	{
		count++;
		result[count] = 1; //EURO band(GSM-900 / DCS-1800 / WCDMA-IMT-2000)
		
	}

	if((gsmband & GSMBAND_GSM_850) && (gsmband & GSMBAND_PCS_GSM_1900) && ( umtsband & UMTSBAND_BAND_5) && ( umtsband & UMTSBAND_BAND_2))
	{
		count++;
		result[count] = 2; //US band(GSM-850 / PCS-1900 / WCDMA-850 / WCDMA-PCS-1900)
		
	}

	if((umtsband & UMTSBAND_BAND_1) && (umtsband & UMTSBAND_BAND_6))
	{
		count++;
		result[count]= 3; //JPN band(WCDMA-800 / WCDMA-IMT-2000)
	}

	if((gsmband & GSMBAND_PGSM_900) && (gsmband & GSMBAND_DCS_GSM_1800) &&( umtsband & UMTSBAND_BAND_5) && (umtsband & UMTSBAND_BAND_1))
	{
		count++;
		result[count]= 4; //AUS band (GSM-900 / DCS-1800 / WCDMA-850 / WCDMA-IMT-2000)
		
	}

	result[0] = count;
	
   RIL_onRequestComplete(session->token, RIL_E_SUCCESS, result, (count + 1)*sizeof(int));
   return 1;

error:
	LOGE("%s: Format error in this AT response", __FUNCTION__);
   RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, &result, sizeof(int));
   return 1;

}

static int callback_DeviceIdentity1(ATResponse* response, struct requestSession* session)
{
	char *line = NULL;
	char *sv = NULL;
	int err;

	if (response->success == 0 || response->p_intermediates == NULL)
		goto error;

	line = response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0)  goto error;

	err = at_tok_nextstr(&line, &sv);
	if (err < 0)  goto error;

	strncpy(s_DeviceIdentity_sv, sv, 2);
	s_DeviceIdentity_sv[2] = '\0';

	session->callback = callback_DeviceIdentity2;
	session->type = SINGLELINE;
	session->needResponse = 1;
	session->channelID = SERVICE_DEV;
	strcpy(session->cmdString, "AT+CGSN");
	strcpy(session->responsePrefix, "+CGSN:");

	RIL_requestTimedCallback(continueProcessRequest, &descriptions[session->channelID], &TIMEVAL_0);
	return 0;

error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

static int callback_DeviceIdentity2(ATResponse* response, struct requestSession* session)
{
	char *line = NULL;
	char *imei = NULL;
	char *result[4] = {0};
	int err;

	if (response->success == 0 || response->p_intermediates == NULL)
		goto error;

	line = response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0)  goto error;

	err = at_tok_nextstr(&line, &imei);
	if (err < 0)  goto error;

	result[0] = imei;
	result[1] = s_DeviceIdentity_sv;
	result[2] = NULL;
	result[3] = NULL;

	LOGI("%s: imei: %s, sv: %s\n", __FUNCTION__, imei, s_DeviceIdentity_sv);

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, result, sizeof(result));

	return 1;

error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

