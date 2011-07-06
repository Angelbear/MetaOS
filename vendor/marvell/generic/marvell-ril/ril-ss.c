/******************************************************************************
 * *(C) Copyright 2008 Marvell International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/


#include "marvell-ril.h"

int callback_QueryClip(ATResponse* response, struct requestSession* session);
int callback_RequestGetClir(ATResponse* response, struct requestSession* session);
int callback_QueryFacilityLock(ATResponse* response, struct requestSession* session);
int callback_QueryCallWaiting(ATResponse* response, struct requestSession* session);
int callback_QueryCallForwardStatus(ATResponse* response, struct requestSession* session);
int callback_GetCnap(ATResponse* response, struct requestSession* session);

RIL_AT_Map table_ss[] = {
	{ RIL_REQUEST_QUERY_FACILITY_LOCK,	 MULTILINE, 	callback_QueryFacilityLock		 },
	{ RIL_REQUEST_SEND_USSD,		 NO_RESULT,	    callback_DefaultResponse	     },
	{ RIL_REQUEST_CANCEL_USSD,		 NO_RESULT,	    callback_DefaultResponse	     },
	{ RIL_REQUEST_QUERY_CLIP,		 SINGLELINE,	    callback_QueryClip		     },
	{ RIL_REQUEST_GET_CLIR,			 SINGLELINE,	    callback_RequestGetClir	     },
	{ RIL_REQUEST_SET_CLIR,			 NO_RESULT,	    callback_DefaultResponse	     },
	{ RIL_REQUEST_QUERY_CALL_FORWARD_STATUS, MULTILINE,	    callback_QueryCallForwardStatus  },
	{ RIL_REQUEST_SET_CALL_FORWARD,		 NO_RESULT,	    callback_DefaultResponse	     },
	{ RIL_REQUEST_QUERY_CALL_WAITING,	 MULTILINE,	    callback_QueryCallWaiting	     },
	{ RIL_REQUEST_SET_CALL_WAITING,		 NO_RESULT,	    callback_DefaultResponse	     },
	{ RIL_REQUEST_CHANGE_BARRING_PASSWORD,	 NO_RESULT,	    callback_DefaultResponse	     },
	{ RIL_REQUEST_SET_SUPP_SVC_NOTIFICATION, NO_RESULT,	    callback_DefaultResponse	     },
	{ RIL_REQUEST_GET_CNAP,                  SINGLELINE,        callback_GetCnap                 },
};

int onRequest_ss (int request, void *data, size_t datalen, RIL_Token token, char *cmdString,
		  ATCommandType *pType, char *prefix, requestCallback *pCallback)
{
	int continue_flag = 1;

	switch (request)
	{
	case RIL_REQUEST_SEND_USSD:
	{
		const char *ussdRequest = (char *)(data);
		sprintf(cmdString, "AT+CUSD=1,%s,15", ussdRequest);
		break;
	}

	case RIL_REQUEST_CANCEL_USSD:
	{
		strcpy(cmdString, "AT+CUSD=2");
		break;
	}

	case RIL_REQUEST_GET_CLIR:
	{
		strcpy(cmdString, "AT+CLIR?");
		strcpy(prefix, "+CLIR:");
		break;
	}

	case RIL_REQUEST_SET_CLIR:
	{
		int i = ((int *)data)[0];
		sprintf(cmdString, "AT+CLIR=%d", i);
		break;
	}

	case RIL_REQUEST_QUERY_CALL_FORWARD_STATUS:
	{
		RIL_CallForwardInfo * info = (RIL_CallForwardInfo *)data;
		if (info->serviceClass == 0){
			sprintf(cmdString, "AT+CCFC=%d,%d", info->reason, info->status);
		}
		else
		{
			sprintf(cmdString, "AT+CCFC=%d,%d,,,%d",
				info->reason, info->status, info->serviceClass);
		}
		strcpy(prefix, "+CCFC:");
		break;
	}

	case RIL_REQUEST_SET_CALL_FORWARD:
	{
		RIL_CallForwardInfo * info = (RIL_CallForwardInfo *)data;
		if (info->serviceClass == 0)
		{
			sprintf(cmdString, "AT+CCFC=%d,%d,%s,%d,7",
				info->reason, info->status, info->number, info->toa);
		}
		else
		{
			sprintf(cmdString, "AT+CCFC=%d,%d,%s,%d,%d",
				info->reason, info->status, info->number, info->toa, info->serviceClass);
		}

		if (info->timeSeconds > 0)
		{
			char time[10];
			sprintf(time, ",,,%d", info->timeSeconds);
			strcat(cmdString, time);
		}
		break;
	}

	case RIL_REQUEST_QUERY_CALL_WAITING:
	{
		int class = ((int *)data)[0];
		sprintf(cmdString, "AT+CCWA=1,2,%d", class);
		strcpy(prefix, "+CCWA:");
		break;
	}

	case RIL_REQUEST_SET_CALL_WAITING:
	{
		int action = ((int *)data)[0];
		int class = ((int *)data)[1];

		if (class == 0) class = 7;
		sprintf(cmdString, "AT+CCWA=1,%d,%d", action, class);
		break;
	}
	case RIL_REQUEST_QUERY_FACILITY_LOCK:
	{
		char* code = ((char**)data)[0];
		char* password = ((char**)data)[1];
		char* class = ((char**)data)[2];

		if (class[0] == '0')
		{
			sprintf(cmdString, "AT+CLCK=%s,2,%s,7", code, password);
		}
		else
		{
			sprintf(cmdString, "AT+CLCK=%s,2,%s,%s", code, password, class);
		}
		strcpy(prefix, "+CLCK:");
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

	case RIL_REQUEST_CHANGE_BARRING_PASSWORD:
	{
		char* code = ((char**)data)[0];
		char* oldPass = ((char**)data)[1];
		char* newPass = ((char**)data)[2];

		sprintf(cmdString, "AT+CPWD=%s,%s,%s", code, oldPass, newPass);
		break;
	}

	case RIL_REQUEST_QUERY_CLIP:
	{
		strcpy(cmdString, "AT+CLIP?");
		strcpy(prefix, "+CLIP:");
		break;
	}

	case RIL_REQUEST_SET_SUPP_SVC_NOTIFICATION:
	{
		int flag = ((int *)data)[0];
		sprintf(cmdString, "AT+CSSN=%d,%d", flag, flag);
		break;
	}

	case RIL_REQUEST_GET_CNAP:
	{
		strcpy(cmdString, "AT+CNAP?");
		strcpy(prefix, "+CNAP");
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
		if ((index = getTableIndex(table_ss, request, NUM_ELEMS(table_ss))) != -1)
		{
			*pType = table_ss[index].type;
			*pCallback = table_ss[index].callback;
		}
		else
		{
			LOGW("%s: Can't find the request in table:%d\n", __FUNCTION__, request);
			continue_flag = 0;
		}
	}

	return continue_flag;
}

/*
 * From RFC 2044:
 *
 * UCS-4 range (hex.)           UTF-8 octet sequence (binary)
 * 0000 0000-0000 007F   0xxxxxxx
 * 0000 0080-0000 07FF   110xxxxx 10xxxxxx
 * 0000 0800-0000 FFFF   1110xxxx 10xxxxxx 10xxxxxx
 * 0001 0000-001F FFFF   11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
 * 0020 0000-03FF FFFF   111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
 * 0400 0000-7FFF FFFF   1111110x 10xxxxxx ... 10xxxxxx

  * UCS-2 range (hex.)           UTF-8 octet sequence (binary)
 * 0000-007F                      0xxxxxxx
 * 0080-07FF                      110xxxxx 10xxxxxx
 * 0800-FFFF                       1110xxxx 10xxxxxx 10xxxxxx
 
 */


char* libUCS2ToUTF8(char* UCS2String)
{
    unsigned int i = 0, len = 0;
	unsigned int inBufLen = 0;
	char* ret = NULL;
	
	if(UCS2String == NULL || strlen(UCS2String)%2 != 0)
		return NULL;

	while(UCS2String[i] != 0 || UCS2String[i+1] != 0)
	{
		i+=2;
	}
	
	inBufLen = i;
	
    for( i = 0; i < inBufLen; i += 2 ) {
      if( (UCS2String[i] == 0x00) && ((UCS2String[i+1] & 0x80) == 0x00) ) len += 1;
      else if( UCS2String[i] < 0x08 ) len += 2;
      else if( ((UCS2String[i] & 0xDC) == 0xD8) ) {
        if( ((UCS2String[i+2] & 0xDC) == 0xDC) && ((inBufLen - i) > 2) ) {
          i += 2;
          len += 4;
        } else {
          return NULL;
        }
      }
      else len += 3;
    }

	if(len > 0)
		ret = malloc(sizeof(char) * (len + 1));
	else
		return NULL;

	if(ret == NULL) 
		return NULL;

    len = 0;

    for( i = 0; i < inBufLen; i += 2 ) {
      if( (UCS2String[i] == 0x00) && ((UCS2String[i+1] & 0x80) == 0x00) ) {
        /* 0000-007F -> 0xxxxxx */
        /* 00000000 0abcdefg -> 0abcdefg */

        ret[len] = UCS2String[i+1] & 0x7F;

        len += 1;
      } else if( UCS2String[i] < 0x08 ) {
        /* 0080-07FF -> 110xxxxx 10xxxxxx */
        /* 00000abc defghijk -> 110abcde 10fghijk */

        ret[len+0] = 0xC0 | ((UCS2String[i] & 0x07) << 2)
                             | ((UCS2String[i+1] & 0xC0) >> 6);
        ret[len+1] = 0x80 | ((UCS2String[i+1] & 0x3F) >> 0);

        len += 2;
      } else if( (UCS2String[i] & 0xDC) == 0xD8 ) {
        int abcde, BCDE;

        if(((UCS2String[i+2] & 0xDC) == 0xDC) && ((inBufLen - i) > 2))
        {
			free(ret);
			return NULL;
        }

        /* D800-DBFF DC00-DFFF -> 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
        /* 110110BC DEfghijk 110111lm nopqrstu ->
           { Let abcde = BCDE + 1 }
           11110abc 10defghi 10jklmno 10pqrstu */

        BCDE = ((UCS2String[i] & 0x03) << 2) | ((UCS2String[i+1] & 0xC0) >> 6);
        abcde = BCDE + 1;

        ret[len+0] = 0xF0 | ((abcde & 0x1C) >> 2);
        ret[len+1] = 0x80 | ((abcde & 0x03) << 4)
                             | ((UCS2String[i+0+1] & 0x3C) >> 2);
        ret[len+2] = 0x80 | ((UCS2String[i+0+1] & 0x03) << 4)
                             | ((UCS2String[i+2+0] & 0x03) << 2)
                             | ((UCS2String[i+2+1] & 0xC0) >> 6);
        ret[len+3] = 0x80 | ((UCS2String[i+2+1] & 0x3F) >> 0);

        i += 2;
        len += 4;
      } else {
        /* 0800-FFFF -> 1110xxxx 10xxxxxx 10xxxxxx */
        /* abcdefgh ijklmnop -> 1110abcd 10efghij 10klmnop */

        ret[len+0] = 0xE0 | ((UCS2String[i] & 0xF0) >> 4);
        ret[len+1] = 0x80 | ((UCS2String[i] & 0x0F) << 2)
                             | ((UCS2String[i+1] & 0xC0) >> 6);
        ret[len+2] = 0x80 | ((UCS2String[i+1] & 0x3F) >> 0);

        len += 3;
      }
    }
	
	ret[len] = '\0';

	LOGV("UTF8 string: len = %d, original len = %d ", len, inBufLen);

    return ret;
}


char* libIRAToBUF(char* IRAString)
{
	LOGV("%s entry: %s", __FUNCTION__, IRAString);
	char* ret = NULL;
	ret = malloc(sizeof(char) * (strlen(IRAString) / 2 + 2));
	int i = 0;
	unsigned int tmp;
	char tmpbuf[3];
	int length = strlen(IRAString);
	while (i < length)
	{
		memcpy(tmpbuf, &IRAString[i], 2);
		tmpbuf[2] = '\0';
		sscanf(tmpbuf, "%x", &tmp);
		ret[i / 2] = tmp;
		i += 2;
	}
	ret[length / 2] = '\0';
	ret[length / 2 + 1] = '\0';
	LOGV("%s exit: %s", __FUNCTION__, ret);
	return ret;
}

void onUnsolicited_ss (const char *s)
{
	char *line = NULL, *response, *linesave = NULL;
	int err;

	if (strStartsWith(s, "+CSSI:"))
	{
		RIL_SuppSvcNotification* response = alloca(sizeof(RIL_SuppSvcNotification));
		memset(response, 0, sizeof(RIL_SuppSvcNotification));
		response->notificationType = 0;

		line = strdup(s);
		linesave = line;

		at_tok_start(&line);
		err = at_tok_nextint(&line, &response->code);
		if (err < 0) goto error;

		if (at_tok_hasmore(&line))
		{
			err = at_tok_nextint(&line, &response->index);
			if (err < 0) goto error;
		}
		RIL_onUnsolicitedResponse( RIL_UNSOL_SUPP_SVC_NOTIFICATION, response, sizeof(RIL_SuppSvcNotification));
	}

	else if (strStartsWith(s, "+CSSU:"))
	{
		RIL_SuppSvcNotification* response = alloca(sizeof(RIL_SuppSvcNotification));
		memset(response, 0, sizeof(RIL_SuppSvcNotification));
		response->notificationType = 1;

		line = strdup(s);
		linesave = line;

		at_tok_start(&line);
		err = at_tok_nextint(&line, &(response->code));
		if (err < 0) goto error;

		if (at_tok_hasmore(&line))
		{
			at_tok_nextint(&line, &(response->index));
		}
		if (at_tok_hasmore(&line))
		{
			err = at_tok_nextstr(&line, &(response->number));
			if (err < 0) goto error;
		}
		if (at_tok_hasmore(&line))
		{
			at_tok_nextint(&line, &(response->type));
		}
		RIL_onUnsolicitedResponse( RIL_UNSOL_SUPP_SVC_NOTIFICATION, response, sizeof(RIL_SuppSvcNotification));
	}

	else if (strStartsWith(s, "+CUSD:"))
	{
		char* response[2];
		char* IRAString, *UCS2String;
		int ussdDcs;

		line = strdup(s);
		linesave = line;

		at_tok_start(&line);

		err = at_tok_nextstr(&line, &response[0]);
		if (err < 0) goto error;

		err = at_tok_nextstr(&line, &IRAString);
		if (err < 0)
		{
			response[1] = NULL;
			RIL_onUnsolicitedResponse( RIL_UNSOL_ON_USSD, response, sizeof(response));

			if (linesave != NULL) free(linesave);
			return;
		}

		err = at_tok_nextint(&line, &ussdDcs);
		if (err < 0)
		{
			response[1] = NULL;
			RIL_onUnsolicitedResponse( RIL_UNSOL_ON_USSD, response, sizeof(response));
		}
		else
		{
			if(ussdDcs == 17) //UCS2
			{
				UCS2String = libIRAToBUF(IRAString);
				response[1] = libUCS2ToUTF8(UCS2String);
				free(UCS2String);
			}
			else
			{
				response[1] = libIRAToBUF(IRAString);
			}
			
			RIL_onUnsolicitedResponse( RIL_UNSOL_ON_USSD, response, sizeof(response));
			free(response[1]);
		}

	}

	if (linesave != NULL) free(linesave);
	return;

 error:
	if (linesave != NULL) free(linesave);
	LOGW("%s: Error parameter in ind msg: %s", __FUNCTION__, s);
	return;

}

int onConfirm_ss(ATResponse* response, struct requestSession* session)
{
	int done = session->callback(response, session);

	return done;
}

/* Process AT reply of RIL_REQUEST_GET_CLIR */
int callback_RequestGetClir(ATResponse* response, struct requestSession* session)
{
	int result[2];
	char *line;
	int err;

	if (response->success == 0) goto error;

	line = response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &(result[0]));
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &(result[1]));
	if (err < 0) goto error;

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, result, sizeof(result));
	return 1;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_QUERY_CALL_FORWARD_STATUS */
int callback_QueryCallForwardStatus(ATResponse* response, struct requestSession* session)
{
	ATLine *p_cur;
	RIL_CallForwardInfo** result;
	int number = 0;
	int err, i;

	if (response->success == 0) goto error;

	for (p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next) number++;

	LOGD("%s:%d record", __FUNCTION__, number);

	result = alloca(number * sizeof(RIL_CallForwardInfo*));
	i = 0;
	while (i < number)
	{
		result[i] = alloca(sizeof(RIL_CallForwardInfo));
		i++;
	}

	i = 0;
	for (p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next)
	{
		char *line = p_cur->line;
		char* ignore;
		int ignoreSubtype;
		result[i]->status = 0;
		result[i]->reason = 0;
		result[i]->serviceClass = 0;
		result[i]->toa = 0;
		result[i]->number = NULL;
		result[i]->timeSeconds = 0;

		err = at_tok_start(&line);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &(result[i]->status));
		if (err < 0) goto error;
		LOGD("CCFC: status:%d", result[i]->status);

		err = at_tok_nextint(&line, &(result[i]->serviceClass));
		if (err < 0) goto error;
		LOGD("CCFC: serviceClass:%d", result[i]->serviceClass);
		if (!at_tok_hasmore(&line)) continue;

		err = at_tok_nextstr(&line, &(result[i]->number));
		LOGI("CCFC: number:%s", result[i]->number);
		if (err < 0) goto error;
		if (!at_tok_hasmore(&line)) continue;

		err = at_tok_nextint(&line, &(result[i]->toa));
		LOGI("CCFC: type:%d", result[i]->toa);
		if (err < 0) goto error;
		if (!at_tok_hasmore(&line)) continue;

		err = at_tok_nextstr(&line, &ignore);
		if (err < 0) goto error;
		if (!at_tok_hasmore(&line)) continue;

		err = at_tok_nextint(&line, &ignoreSubtype);
		if (err < 0) goto error;
		if (!at_tok_hasmore(&line)) continue;

		err = at_tok_nextint(&line, &(result[i]->timeSeconds));
		if (err < 0) goto error;
		LOGD("CCFC: type:%d", result[i]->timeSeconds);

		i++;
	}
	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, result, sizeof(number * sizeof(RIL_CallForwardInfo*)));
	return 1;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_QUERY_CALL_WAITING */
int callback_QueryCallWaiting(ATResponse* response, struct requestSession* session)
{
	ATLine *p_cur;
	int err;
	int result[2];
	int class;
	int enable;
	int i;

	result[1] = 0;
	if (response->success == 0) goto error;

	for (i = 0, p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next, i++ )
	{
		char *line = p_cur->line;

		err = at_tok_start(&line);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &enable);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &class);
		if (err < 0) goto error;
		if (enable == 1)
			result[1] += class;
	}

	if (result[1] > 0)
	{
		result[0] = 1;
	}
	else
	{
		result[0] = 0;
	}
	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, result, sizeof(result));
	return 1;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_QUERY_FACILITY_LOCK */
int callback_QueryFacilityLock(ATResponse* response, struct requestSession* session)
{
	int result;
	ATLine *p_cur;
	int err, status, classNo, i;

	if (response->success == 0) goto error;
	result = 0;
	for (i = 0, p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next, i++ )
	{
		char *line = p_cur->line;
		err = at_tok_start(&line);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &status);
		if (err < 0) goto error;

		if (!at_tok_hasmore(&line))
		{
			if (status == 1)
				result += 7;
			continue;
		}

		err = at_tok_nextint(&line, &classNo);
		if (err < 0) goto error;
		if (status == 1)
			result += classNo;
	}

	LOGD("result:%d", result);
	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, &result, sizeof(result));
	return 1;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_QUERY_CLIP */
int callback_QueryClip(ATResponse* response, struct requestSession* session)
{
	int err;
	int result;
	int ignore;
	char *line = response->p_intermediates->line;

	if (response->success == 0) goto error;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &ignore);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &result);
	if (err < 0) goto error;
	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, &result, sizeof(result));
	return 1;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_GET_CNAP */
int callback_GetCnap(ATResponse* response, struct requestSession* session)
{
	ATLine *p_cur;
	int err;
	char *result[2];
	char *line = response->p_intermediates->line;

	if (response->success == 0) goto error;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextstr(&line, &result[0]);
	if (err < 0) goto error;

	err = at_tok_nextstr(&line, &result[1]);
	if (err < 0) goto error;

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, result, sizeof(result));
	return 1;

error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

