/******************************************************************************
 * *(C) Copyright 2008 Marvell International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/


#include "marvell-ril.h"

int callback_RequestSendSMS(ATResponse* response, struct requestSession* session);
int callback_GetSMSCAddress(ATResponse* response, struct requestSession* session);

RIL_AT_Map table_msg[] = {
	{ RIL_REQUEST_SEND_SMS,		    SINGLELINE,				callback_RequestSendSMS	       },
	{ RIL_REQUEST_SEND_SMS_EXPECT_MORE, SINGLELINE,				callback_RequestSendSMS	       },
	{ RIL_REQUEST_SMS_ACKNOWLEDGE,	    NO_RESULT,				callback_DefaultResponse       },
	{ RIL_REQUEST_WRITE_SMS_TO_SIM,	    SINGLELINE,				callback_ReturnOneInt	       },
	{ RIL_REQUEST_DELETE_SMS_ON_SIM,    NO_RESULT,				callback_DefaultResponse       },
	{RIL_REQUEST_REPORT_SMS_MEMORY_STATUS, NO_RESULT,			callback_DefaultResponse	},
	{ RIL_REQUEST_GET_SMSC_ADDRESS,	    SINGLELINE,				callback_GetSMSCAddress        },
	{ RIL_REQUEST_SET_SMSC_ADDRESS,	    NO_RESULT,				callback_DefaultResponse       },

};

int onRequest_msg (int request, void *data, size_t datalen, RIL_Token token, char *cmdString,
		   char *smsPdu, ATCommandType *pType, char *prefix, requestCallback *pCallback)
{
	int continue_flag = 1;

	switch (request)
	{
	case RIL_REQUEST_SEND_SMS:
	case RIL_REQUEST_SEND_SMS_EXPECT_MORE:
	{
		const char *smsc, *tpdu;
		int tpLayerLength;
		char temp[20];

		/* Send +CMMS=<n> to set local variable in AT cmd server */
		if (request == RIL_REQUEST_SEND_SMS_EXPECT_MORE)
		{
			strcpy(cmdString, "AT+CMMS=1;");
		}
		else if (request == RIL_REQUEST_SEND_SMS)
		{
			strcpy(cmdString, "AT+CMMS=0;");
		}

		smsc = ((const char **)data)[0];
		tpdu = ((const char **)data)[1];
		tpLayerLength = strlen(tpdu) / 2;
		/* NULL for default SMSC */
		if (smsc == NULL) smsc = "00";

		sprintf(temp, "+CMGS=%d", tpLayerLength);
		strcat(cmdString, temp);
		sprintf(smsPdu, "%s%s", smsc, tpdu);
		strcpy(prefix, "+CMGS:");
		break;
	}

	case RIL_REQUEST_SMS_ACKNOWLEDGE:
	{
		int ackSuccess;
		int memFull;
		ackSuccess = ((int *)data)[0];
		memFull = ((int*)data)[1];

		if (ackSuccess == 1)
		{
			strcpy(cmdString, "AT*CNMA=0");
		}
		else
		{
			if(memFull == 0xd3)
				strcpy(cmdString, "AT*CNMA=1");
			else
				strcpy(cmdString, "AT*CNMA=2");
		}
		break;
	}

	case RIL_REQUEST_WRITE_SMS_TO_SIM:
	{
		RIL_SMS_WriteArgs *p_args;
		const char *smsc;
		int length, err;

		p_args = (RIL_SMS_WriteArgs *)data;
		length = strlen(p_args->pdu) / 2;
		sprintf(cmdString, "AT+CMGW=%d,%d", length, p_args->status);

		smsc = p_args->smsc;
		/* NULL for default SMSC */
		if (smsc == NULL) smsc = "00";
		sprintf(smsPdu, "%s%s", smsc, p_args->pdu);
		strcpy(prefix, "+CMGW:");
		break;
	}

	case RIL_REQUEST_DELETE_SMS_ON_SIM:
	{
		sprintf(cmdString, "AT+CMGD=%d", ((int *)data)[0]);
		break;
	}

	case RIL_REQUEST_REPORT_SMS_MEMORY_STATUS:
	{
		int memRstFull = ((int *)data)[0];
		if(memRstFull)
		{
			sprintf(cmdString, "AT*RSTMEMFULL");
		}
		else
		{
			RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
			continue_flag = 0;
		}
		break;
	}

	case RIL_REQUEST_SET_SMSC_ADDRESS:
	{
		char *sca = (char *)data;
		int tosca;

		if(sca[0] == '+')
			tosca = 145;
		else
			tosca = 129;

		sprintf(cmdString, "AT+CSCA=\"%s\",%d", sca, tosca);
		break;
	}

	case RIL_REQUEST_GET_SMSC_ADDRESS:
	{
		sprintf(cmdString, "AT+CSCA?");
		strcpy(prefix, "+CSCA:");
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
		if ((index = getTableIndex(table_msg, request, NUM_ELEMS(table_msg))) != -1)
		{
			*pType = table_msg[index].type;
			*pCallback = table_msg[index].callback;
		}
		else
		{
			LOGW("%s: Can't find the request in table:%d\n", __FUNCTION__, request);
			continue_flag = 0;
		}
	}

	return continue_flag;
}

/* Do some init work after receiving msg to indicate of CP msg module is ready */
static void onSmsInitReady(void* param)
{
	/* Always send SMS messages directly to the TE
	 * mode = 1 // discard when link is reserved (link should never be reserved)
	 * mt = 2   // most messages routed to TE
	 * bm = 2   // new cell BM's routed to TE
	 * ds = 1   // Status reports routed to TE
	 * bfr = 1  // flush buffer
	 */
	at_send_command_sync(CHANNEL_ID(MSG), "AT+CNMI=1,2,2,1,1", NO_RESULT, NULL, NULL, NULL);

	/* It seems unnecessary to send +CSMS=1 because CP always works in this way and can't change this setting */
	//at_send_command_sync(SERVICE_MSG, "AT+CSMS=1", SINGLELINE, "+CSMS:", NULL, NULL);
}

void onUnsolicited_msg (const char *s, const char *smsPdu)
{
	char *line = NULL, *response;
	int err;
	char* linesave = NULL;

	/* New SMS is reported directly to ME */
	if (strStartsWith(s, "+CMT:"))
	{
		RIL_onUnsolicitedResponse( RIL_UNSOL_RESPONSE_NEW_SMS, smsPdu, strlen(smsPdu));
	}

	/* New SMS is saved in SIM and the index is reported to ME */
	else if (strStartsWith(s, "+CMTI:"))
	{
		int index;
		line = strdup(s);
		linesave = line;
		at_tok_start(&line);
		err = at_tok_nextstr(&line, &response);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &index);
		if (err < 0) goto error;

		RIL_onUnsolicitedResponse( RIL_UNSOL_RESPONSE_NEW_SMS_ON_SIM, &index, sizeof(index));
	}

	/* New SMS status report reported directly to ME */
	else if (strStartsWith(s, "+CDS:"))
	{
		RIL_onUnsolicitedResponse( RIL_UNSOL_RESPONSE_NEW_SMS_STATUS_REPORT, smsPdu, strlen(smsPdu));
	}

	/* New CBM is reported directly to ME */
	else if (strStartsWith(s, "+CBM:"))
	{
	        RIL_onUnsolicitedResponse( RIL_UNSOL_RESPONSE_NEW_BROADCAST_SMS, smsPdu, strlen(smsPdu));
	}

	/* Marvell extended AT cmd to indicate memory of SIM is full  */
	else if (strStartsWith(s, "+MMSG:"))
	{
		int bSmsReady = 0, bSmsFull = 0;
		line = strdup(s);
		linesave = line;
		at_tok_start(&line);
		err = at_tok_nextint(&line, &bSmsReady);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &bSmsFull);
		if (err < 0) goto error;

		if (bSmsFull == 1)
		{
			RIL_onUnsolicitedResponse(RIL_UNSOL_SIM_SMS_STORAGE_FULL, NULL, 0);
		}
	}

	/* Free allocated memory and return */
	if (linesave != NULL) free(linesave);
	return;

 error:
	if (linesave != NULL) free(linesave);
	LOGW("%s: Error parameter in ind msg: %s", __FUNCTION__, s);
	return;

}

int onConfirm_msg(ATResponse* response, struct requestSession* session)
{
	int done = session->callback(response, session);

	return done;
}

/* Process AT reply of RIL_REQUEST_SEND_SMS and RIL_REQUEST_SEND_SMS_EXPECT_MORE */
int callback_RequestSendSMS(ATResponse* response, struct requestSession* session)
{
	int err;
	RIL_SMS_Response result;
	char *line;

	/* MO SMS is sent to network successfully*/
	if (response->success == 1)
	{
		line = response->p_intermediates->line;
		memset(&result, 0, sizeof(result));
		at_tok_start(&line);
		err = at_tok_nextint(&line, &(result.messageRef));
		if (err < 0) goto error;

		if (at_tok_hasmore(&line))
		{
			err = at_tok_nextstr(&line, &(result.ackPDU));
			if (err < 0) goto error;
		}
		RIL_onRequestComplete(session->token, RIL_E_SUCCESS, &result, sizeof(result));
	}

	/* MO SMS fails to send out, report error msg according to +CMS error code */
	else
	{

		RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
#if 0
		int errCause;
		if (strStartsWith(line, "+CMS ERROR:"))
		{
			at_tok_start(&line);
			err = at_tok_nextint(&line, &errCause);
			if (err < 0) goto error;
			if (errCause == 322)
			{
				RIL_onRequestComplete(session->token, RIL_E_SMS_SEND_FAIL_RETRY, NULL, 0);
			}
			else
			{
				RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
			}
		}
		else
		{
			goto error;
		}
#endif
	}

	return 1;

 error:
	LOGE("%s: Error parameter in response: %s", __FUNCTION__, response->p_intermediates->line);
	return 1;
}

int callback_GetSMSCAddress(ATResponse* response, struct requestSession* session)
{
	int result[1];
	char *line;
	char *addr;
	char *sca;
	int tosca;
	int err;

	if (response->success == 0)
	{
		RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
		return 1;
	}

	line = response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextstr(&line, &addr);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &tosca);
	if (err < 0) goto error;

	if (tosca == 145 && addr[0] != '+')
	{
		sca = alloca(sizeof(char) * (strlen(addr) + 1 + 1));
		sca[0] = '+';
		sca[1] = 0;
		strcat(sca, addr);
	}
	else
	{
		sca = addr;
	}

	LOGI("%s: sca: %s, tosca: %d\n", __FUNCTION__, sca, tosca);

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, sca, strlen(sca));

	return 1;

 error:
	LOGE("%s: Error parameter in response: %s", __FUNCTION__, response->p_intermediates->line);
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

