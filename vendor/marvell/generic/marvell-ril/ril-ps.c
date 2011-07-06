/******************************************************************************
 * *(C) Copyright 2008 Marvell International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/

#include "marvell-ril.h"
#include <cutils/properties.h>

extern void enableInterface(int cid);
extern void disableInterface(int cid);
extern int configureInterface(char* address);
extern void deconfigureInterface();
extern void setDNS(char* dns);

extern int enablePPPInterface(int cid, const char *user, const char *passwd, char* ipaddress);
extern void disablePPPInterface(int cid);
extern int getInterfaceAddr(int cid, const char* ifname, char* ipaddress);

static void onPDPContextListChanged(void *param);
static int getPsConnectInfo(int *pCid, char *ipAddr);

static char g_user[64];
static char g_passwd[64];
static char g_apn[64];

int callback_SetupDefaultPDP(ATResponse* response, struct requestSession* session);
int callback_RequestDeactivateDefaultPDP(ATResponse* response, struct requestSession* session);
int callback_RequestPDPContextList(ATResponse* response, struct requestSession* session);
void syncSetupDefaultPDPConnection(RIL_Token token, const char* apn, const char* user, const char* passwd);

RIL_AT_Map table_ps[] = {
	{ RIL_REQUEST_SETUP_DATA_CALL,		 NO_RESULT,	       callback_SetupDefaultPDP			  },
	{ RIL_REQUEST_DEACTIVATE_DATA_CALL,	 NO_RESULT,	       callback_RequestDeactivateDefaultPDP	  },
	{ RIL_REQUEST_DATA_CALL_LIST,		 MULTILINE,	       callback_RequestPDPContextList		  },
	{ RIL_REQUEST_LAST_DATA_CALL_FAIL_CAUSE, SINGLELINE,	       callback_ReturnOneInt			  },
	{ RIL_REQUEST_FAST_DORMANCY,		 NO_RESULT,	       callback_DefaultResponse			  },
};

int onRequest_ps (int request, void *data, size_t datalen, RIL_Token token, char *cmdString,
		  ATCommandType *pType, char *prefix, requestCallback *pCallback)
{
	int continue_flag = 1;

	switch (request)
	{
	case RIL_REQUEST_SETUP_DATA_CALL:
	{

		LOGD("RIL_REQUEST_SETUP_DATA_CALL: indicator:%s", ((const char **)data)[0]);
		LOGD("RIL_REQUEST_SETUP_DATA_CALL: apn:%s", ((const char **)data)[2]);
		LOGD("RIL_REQUEST_SETUP_DATA_CALL: auth type:%s", ((const char **)data)[5]);

		if (((const char **)data)[3] != NULL)
		{
			strcpy(g_user, ((const char **)data)[3]);
			LOGD("RIL_REQUEST_SETUP_DATA_CALL: user: %s!\n", g_user);
		}
		else
		{
			g_user[0] = '\0';
			LOGD("RIL_REQUEST_SETUP_DATA_CALL: user: NULL!\n");
		}

		if (((const char **)data)[4] != NULL)
		{
			strcpy(g_passwd, ((const char **)data)[4]);
			LOGD("RIL_REQUEST_SETUP_DATA_CALL: passwd: %s!\n", g_passwd);
		}
		else
		{
			g_passwd[0] = '\0';
			LOGD("RIL_REQUEST_SETUP_DATA_CALL: passwd: NULL!\n");
		}

		/* For PPP, we only support one CID; for Direct IP, we can support multiple CID */
		syncSetupDefaultPDPConnection(token, ((const char **)data)[2], ((const char **)data)[3], ((const char **)data)[4]);

		/* Save last time APN name */
		strcpy(g_apn, ((const char **)data)[2]);
		continue_flag = 0;

		break;
	}
	case RIL_REQUEST_DEACTIVATE_DATA_CALL:
	{
		/*We only need to deactive the specific CID */
		const char* cid = ((const char **)data)[0];
		ATResponse *p_response = NULL;
		int err;
		char value[PROPERTY_VALUE_MAX];

		sprintf(cmdString, "AT+CGACT=0,%s", cid);
		/* Here we change to use sync way for better adaption for Application part */
		err = at_send_command_sync(CHANNEL_ID(PS), cmdString, NO_RESULT, NULL, NULL, &p_response);

		if (err < 0 || p_response->success == 0)
		{
			LOGW("Fail to deactive PS DATA CALL: %s", cid);
		}
		at_response_free(p_response);

		property_get("marvell.ril.ppp.enabled", value, "1");
		if (atoi(value))
			disablePPPInterface(atoi(cid));
		else
			disableInterface(atoi(cid));

		/* In any case, we still report success to higher layer */
		RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
		g_apn[0] = '\0';
		continue_flag = 0;
		break;
	}

	case RIL_REQUEST_LAST_DATA_CALL_FAIL_CAUSE:
	{
		strcpy(cmdString, "AT+PEER");
		strcpy(prefix, "+PEER:");
		break;
	}

	case RIL_REQUEST_DATA_CALL_LIST:
	{
		strcpy(cmdString, "AT+CGDCONT?");
		strcpy(prefix, "+CGDCONT:");
		break;
	}

	case RIL_REQUEST_FAST_DORMANCY:
	{
		strcpy(cmdString, "AT*FASTDORM");
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
		if ((index = getTableIndex(table_ps, request, NUM_ELEMS(table_ps))) != -1)
		{
			*pType = table_ps[index].type;
			*pCallback = table_ps[index].callback;
		}
		else
		{
			LOGW("%s: Can't find the request in table:%d\n", __FUNCTION__, request);
			continue_flag = 0;
		}
	}

	return continue_flag;
}

void onUnsolicited_ps (const char *s)
{
	/* Really, we can ignore NW CLASS and ME CLASS events here,
	 * but right now we don't since extranous
	 * RIL_UNSOL_DATA_CALL_LIST_CHANGED calls are tolerated
	 */
	/* can't issue AT commands here -- call on main thread */
	if (strStartsWith(s, "+CGEV:"))
	{
		RIL_requestTimedCallback(onPDPContextListChanged, NULL, NULL);
	}
}

int onConfirm_ps(ATResponse* response, struct requestSession* session)
{
	int done = session->callback(response, session);

	return done;
}

/* Internal tool to parse PDP context list.
 * Sample: +CGDCONT: 1,"IP","cmnet","10.60.176.183",0,0,802110030100108106d38870328306d38814cb,
 * Input para: line (as above exsample)
 * Output para: pCid, type, apn, address, dns
 * Note: for the output para with type char *, the space is allocated in this func
 */
int parsePDPContexstList(char *line, int *pCid, char **pType, char **pApn, char **pAddress, char **pDns)
{
	int err, ignore;
	char *out, *type, *apn, *address, *dns;

	type = apn = address = dns = NULL;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, pCid);
	if (err < 0) goto error;

	err = at_tok_nextstr(&line, &out);
	if (err < 0) goto error;
	if (pType != NULL)
	{
		type = malloc(strlen(out) + 1);
		strcpy(type, out);
		*pType = type;
	}

	err = at_tok_nextstr(&line, &out);
	if (err < 0) goto error;
	if (pApn != NULL)
	{
		apn = malloc(strlen(out) + 1);
		strcpy(apn, out);
		*pApn = apn;
	}

	err = at_tok_nextstr(&line, &out);
	if (err < 0) goto error;
	if (pAddress != NULL)
	{
		address = malloc(strlen(out) + 1);
		strcpy(address, out);
		*pAddress = address;
	}

	err = at_tok_nextint(&line, &ignore);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &ignore);
	if (err < 0) goto error;

	err = at_tok_nextstr(&line, &out);
	if (err < 0) goto error;
	if (pDns != NULL)
	{
		dns = malloc(strlen(out) + 1);
		strcpy(dns, out);
		*pDns = dns;
	}

	return 0;

 error:
	if (type) free(type);
	if (apn) free(apn);
	if (address) free(address);
	if (dns) free(dns);
	if (pType) *pType = NULL;
	if (pApn) *pApn = NULL;
	if (pAddress) *pAddress = NULL;
	if (pDns) *pDns = NULL;
	return err;
}

static void freePDPContext(RIL_Data_Call_Response *pdpResponses, int num)
{
	int i;
	char *type, *apn, *address;

	for (i = 0; i < num; i++)
	{
		type = pdpResponses[i].type;
		apn  = pdpResponses[i].apn;
		address = pdpResponses[i].address;

		FREE(type);
		FREE(apn);
		FREE(address);
	}
	FREE(pdpResponses);
}

/* Prossess RIL_REQUEST_SETUP_DATA_CALL in sync way*/
void syncSetupDefaultPDPConnection(RIL_Token token, const char* apn, const char* user, const char* passwd)
{
	ATResponse *p_response = NULL;
	int err = -1;
	int cid;
	char *result[3];
	char value[PROPERTY_VALUE_MAX];
	char cmdString[MAX_AT_LENGTH];
	char ipaddress[64];

	cid = atoi(DEFAULT_CID);
	property_get("marvell.ril.ppp.enabled", value, "1");
	/* Workaround: if the APN name is same and the current APN is already active, we can return directly */
	result[0] = DEFAULT_CID;
	if (g_apn[0] && strcmp(apn, g_apn) == 0)
	{
		if (atoi(value))
		{
			result[1] = "ppp0";
			err = getInterfaceAddr(cid, "ppp0", ipaddress);
		}
		else
		{
			result[1] = DEFAULT_IFNAME;
			err = getInterfaceAddr(cid, DEFAULT_IFNAME, ipaddress);
		}
		if (err == 0)
		{
			result[2] = ipaddress;
			LOGD("The PDP CID %s is already active: IP address %s for Inteface %s", DEFAULT_CID, ipaddress, result[1]);
			RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(result));
			return;
		}
	}

	/* Step1: Define the CID */
	sprintf(cmdString, "AT+CGDCONT=%s,\"IP\",\"%s\"", DEFAULT_CID, apn );

	err = at_send_command_sync(CHANNEL_ID(PS), cmdString, NO_RESULT, NULL, NULL, &p_response);

	if (err < 0 || p_response->success == 0)
	{
		LOGW("Fail to define the PDP context: %s", DEFAULT_CID);
		sprintf(cmdString, "AT+CGACT=0,%s", DEFAULT_CID);
		at_send_command_sync(CHANNEL_ID(PS), cmdString, NO_RESULT, NULL, NULL, NULL);
		goto error;
	}
	at_response_free(p_response);
	p_response = NULL;

	/* Step2: Active the PDP Context */
	if (atoi(value))
		sprintf(cmdString, "AT+CGDATA=\"PPP\",%s", DEFAULT_CID);
	else
		sprintf(cmdString, "AT+CGDATA=\"\",%s", DEFAULT_CID);

#ifdef DKB_CP
	err = at_send_command_sync(CHANNEL_ID(PS), cmdString, NO_RESULT, NULL, NULL, &p_response);
#elif defined BROWNSTONE_CP
	err = at_switch_data_mode(CHANNEL_DAT, cmdString, &p_response);
#endif

	if (err < 0 || p_response->success == 0)
	{
		LOGW("Fail to activate the PDP context: %s", DEFAULT_CID);
		sprintf(cmdString, "AT+CGACT=0,%s", DEFAULT_CID);
		at_send_command_sync(CHANNEL_ID(PS), cmdString, NO_RESULT, NULL, NULL, NULL);
		sleep(3); //Workaround: it seems CP need sometime to clear the previous PDP context before reactiving it
		goto error;
	}
	at_response_free(p_response);
	p_response = NULL;

	/* Step3: Enable the network interface */
	if (atoi(value))
	{
		//For PPP case
		result[1] = "ppp0";
		result[2] = ipaddress;
		int ret = enablePPPInterface(cid, user, passwd, ipaddress);
		if ( ret == -1)
		{
			/* deactive the CID if it is active, we don't need to care about the result */
			sprintf(cmdString, "AT+CGACT=0,%s", DEFAULT_CID);
			at_send_command_sync(CHANNEL_ID(PS), cmdString, NO_RESULT, NULL, NULL, NULL);
			sleep(3); //Workaround: it seems CP need sometime to clear the previous PDP context before reactiving it
			goto error;
		}
		RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(result));
	}
	else
	{
		//For Direct IP case
		char *line, *dns = NULL, *address = NULL;
		ATLine *p_cur;
		int found = 0;
		err = at_send_command_sync(CHANNEL_ID(PS), "AT+CGDCONT?", MULTILINE, "+CGDCONT:", NULL, &p_response);
		if (err < 0 || p_response->success == 0)
		{
			LOGW("Fail to query the PDP context");
			goto error;
		}

		p_cur = p_response->p_intermediates;
		while (p_cur)
		{
			line = p_cur->line;
			err = parsePDPContexstList(line, &cid, NULL, NULL, &address, &dns);
			if (err == 0 && cid == atoi(DEFAULT_CID))
			{
				found = 1; break;
			}
			p_cur = p_cur->p_next;
		}

		if ( !found )
		{
			LOGW("Fail to find the IP address and dns for the cid: %s", DEFAULT_CID);
			goto error;
		}
		setDNS(dns);
		result[1] = DEFAULT_IFNAME;
		result[2] = address;
		enableInterface(cid);

		int ret = configureInterface(result[2]);
		if (ret == -1)
		{
			FREE(dns); FREE(address); goto error;
		}
		RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(result));
		FREE(dns);
		FREE(address);
	}

	return;

 error:
	at_response_free(p_response);
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return;

}

/* Process AT reply of RIL_REQUEST_SETUP_DATA_CALL (step3) */
int _callback_SetupDefaultPDP_step3(ATResponse* response, struct requestSession* session)
{
	int cid;
	char *type, *apn, *address = NULL, *dns;
	char *result[3], *line;
	ATResponse *p_response = response;
	ATLine *p_cur;
	int err, n = 0;
	char value[PROPERTY_VALUE_MAX];

	/* [Jerry] ? If there are several PDP contexts, seems the last address is enabled */
	//for (p_cur = p_response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next)

	/* [Jerry] To avoid potiential memory leak, we only enable first address */
	result[0] = DEFAULT_CID;
	cid = atoi(DEFAULT_CID);
	property_get("marvell.ril.ppp.enabled", value, "1");
	if (atoi(value))
	{
		char ipaddress[64];
		result[1] = "ppp0";
		result[2] = ipaddress;
		int ret = enablePPPInterface(cid, g_user, g_passwd, ipaddress);
		if ( ret == -1) goto error;
		//Waiting for ppp0 is ready
		sleep(3);
	}
	else
	{
		p_cur = p_response->p_intermediates;
		{
			line = p_cur->line;
			err = parsePDPContexstList(line, &cid, NULL, NULL, &address, &dns);
			if (err < 0) goto error;
			setDNS(dns);
			FREE(dns);
			result[1] = DEFAULT_IFNAME;
			result[2] = address;
		}
		enableInterface(cid);

		int ret = configureInterface(result[2]);
		if (ret == -1) goto error;
	}
	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, result, sizeof(result));
	if (address) FREE(address);
	return 1;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_SETUP_DATA_CALL (step2) */
int _callback_SetupDefaultPDP_step2(ATResponse* response, struct requestSession* session)
{
	if (response->success == 0) goto error;
	session->callback = _callback_SetupDefaultPDP_step3;
	session->type = MULTILINE;
	session->needResponse = 1;
	session->channelID = CHANNEL_ID(PS);
	strcpy(session->cmdString, "AT+CGDCONT?");

	LOGV("%s: get response from channel", __FUNCTION__);
	RIL_requestTimedCallback(continueProcessRequest, &descriptions[session->channelID], &TIMEVAL_0);
	return 0;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_SETUP_DATA_CALL: we cannot assume we must use the default CID(1) */
int callback_SetupDefaultPDP(ATResponse* response, struct requestSession* session)
{
	char value[PROPERTY_VALUE_MAX];

	if (response->success == 0) goto error;
	session->callback = _callback_SetupDefaultPDP_step2;
	session->type = NO_RESULT;
	session->channelID = CHANNEL_ID(PS);

	property_get("marvell.ril.ppp.enabled", value, "1");
	if (atoi(value))
		sprintf(session->cmdString, "AT+CGDATA=\"PPP\",%s", DEFAULT_CID);
	else
		sprintf(session->cmdString, "AT+CGDATA=\"\",%s", DEFAULT_CID);

	LOGV("%s: get response from channel", __FUNCTION__);
	RIL_requestTimedCallback(continueProcessRequest, &descriptions[session->channelID], &TIMEVAL_0);
	return 0;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_DEACTIVATE_DATA_CALL */
int callback_RequestDeactivateDefaultPDP(ATResponse* response, struct requestSession* session)
{
	char value[PROPERTY_VALUE_MAX];
	int cid;

	if (response->success == 0) goto error;
	LOGV("%s: get response from channel", __FUNCTION__);

	cid = atoi(DEFAULT_CID);

	property_get("marvell.ril.ppp.enabled", value, "1");
	if (atoi(value))
		disablePPPInterface(cid);
	else
		disableInterface(cid);

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, NULL, 0);
	return 1;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}


/* Process AT reply of RIL_REQUEST_DATA_CALL_LIST */
int callback_RequestPDPContextList(ATResponse* response, struct requestSession* session)
{
	ATLine *p_cur;
	int err, i = 0, num = 0, cid;
	char *line;
	char *type, *apn, *address, *dns;
	RIL_Data_Call_Response *pdpResponses;

	LOGV("%s entry", __FUNCTION__);
	if (response->success == 0)
	{
		RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
		return 1;
	}

	for (p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next)
		num++;
	pdpResponses = calloc(num, sizeof(RIL_Data_Call_Response));

	for (p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next)
	{
		line = p_cur->line;

		err = parsePDPContexstList(line, &cid, &type, &apn, &address, NULL);
		if (err < 0) goto error;

		if (strlen(address) > 0)
			pdpResponses[i].active = 2; /* 0=inactive, 1=active/physical link down, 2=active/physical link up */
		pdpResponses[i].cid = cid;
		pdpResponses[i].type = type;
		pdpResponses[i].apn = apn;
		pdpResponses[i].address = address;
		i++;
	}

	LOGV("%s exit", __FUNCTION__);
	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, pdpResponses, num * sizeof(RIL_Data_Call_Response));
	freePDPContext(pdpResponses, num);
	return 1;

 error:
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	freePDPContext(pdpResponses, num);
	return 1;
}

static void onPDPContextListChanged(void *param)
{

	ATLine *p_cur;
	int err, i = 0, num = 0, cid;
	char *line;
	char *type, *apn, *address, *dns;
	ATResponse *p_response;
	RIL_Data_Call_Response *pdpResponses;

	err = at_send_command_sync(CHANNEL_ID(PS), "AT+CGDCONT?", MULTILINE, "+CGDCONT:", NULL, &p_response);
	if (err != 0 || p_response->success == 0)
	{
		RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED, NULL, 0);
		at_response_free(p_response);
		return;
	}

	for (p_cur = p_response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next)
		num++;
	pdpResponses = calloc(num, sizeof(RIL_Data_Call_Response));

	for (p_cur = p_response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next)
	{
		line = p_cur->line;

		err = parsePDPContexstList(line, &cid, &type, &apn, &address, NULL);
		if (err < 0) goto error;

		if (strlen(address) > 0)
			pdpResponses[i].active = 2; /* 0=inactive, 1=active/physical link down, 2=active/physical link up */
		pdpResponses[i].cid = cid;
		pdpResponses[i].type = type;
		pdpResponses[i].apn = apn;
		pdpResponses[i].address = address;
		i++;
	}

	RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED, pdpResponses, num * sizeof(RIL_Data_Call_Response));
	freePDPContext(pdpResponses, num);
	at_response_free(p_response);
	return;

 error:
	LOGW("%s: Error parameter in response msg: %s", __FUNCTION__, line);
	RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED, NULL, 0);
	freePDPContext(pdpResponses, num);
	at_response_free(p_response);
	return;
}

static int getPsConnectInfo(int *pCid, char *ipAddr)
{
	ATLine *p_cur;
	ATResponse *p_response = NULL;
	int err, num = 0, cid, bConnected = 0;
	char *line, *address;

	err = at_send_command_sync(CHANNEL_ID(PS), "AT+CGDCONT?", MULTILINE, "+CGDCONT:", NULL, &p_response);
	if (err < 0 || p_response->success == 0)
	{
		bConnected = 0;
	}
	else
	{
		for (p_cur = p_response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next)
		{
			num++;
		}

		bConnected = 0;
		if (num > 0)
		{
			line = p_response->p_intermediates->line;
			err = parsePDPContexstList(line, &cid, NULL, NULL, &address, NULL);
			if (err >= 0)
			{
				if (strlen(address) > 0)
				{
					strcpy(ipAddr, address);
					*pCid = cid;
					bConnected = 1;
				}
				FREE(address);
			}
		}
	}
	at_response_free(p_response);
	return bConnected;
}
