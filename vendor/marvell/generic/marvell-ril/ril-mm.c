/******************************************************************************
 * *(C) Copyright 2008 Marvell International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/


#include "marvell-ril.h"
#include <cutils/properties.h>

static RegState sCregState = { -1, -1, -1 }, sCgregState = { -1, -1, -1 };
static OperInfo sOperInfo = { -1, "\0", "\0", "\0", -1 };
static int sScreenState = 0;   //default screen state = OFF, it will turn to ON after RIL is initialized
static int sCSQ[2] = { 99, 99 };
int AfterCPReset = 0;
#define MAX_NEIGHBORING_CELLS 6 //max neighboring cell number is set as 6 in defualt
static RIL_NeighboringCell sNeighboringCell[MAX_NEIGHBORING_CELLS]; 
static int sCellNumber = 0;

/*
 * RIL request network type
 *
 * 0 for GSM/WCDMA (WCDMA preferred)
 * 1 for GSM only
 * 2 for WCDMA only
 * 3 for GSM/WCDMA (auto mode, according to PRL)
 * 4 for CDMA and EvDo (auto mode, according to PRL)
 * 5 for CDMA only
 * 6 for EvDo only
 * 7 for GSM/WCDMA, CDMA, and EvDo (auto mode, according to PRL)
 */

/*
 * AT*BAND network Mode
 *
 * 0 for GSM only
 * 1 for WCDMA only
 * 2 for GSM/WCDMA (auto mode)
 * 3 for GSM/WCDMA (GSM preferred)
 * 4 for GSM/WCDMA (WCDMA preferred)
 */


/* map ril request network type to AT*BAND network mode */
static const int RILNetworkTypeTostarBandNetworkType[8] =
{
	4,  /* GSM/WCDMA (WCDMA preferred) */
	0,  /* GSM only */
	1,  /* WCDMA only */
	2,  /* GSM/WCDMA (auto mode, according to PRL) */
	-1, /* CDMA and EvDo (auto mode, according to PRL) */
	-1, /* CDMA only */
	-1, /* EvDo only */
	-1  /* GSM/WCDMA, CDMA, and EvDo (auto mode, according to PRL) */
};

/* map AT*BAND network mode to ril request network type */
static const int starBandNetworkTypeToRILNetworkType[5] =
{
	1,  /* GSM only */
	2,  /* WCDMA only */
	3,  /* GSM/WCDMA (auto mode) */
	-1, /* GSM/WCDMA (GSM preferred) */
	0   /* GSM/WCDMA (WCDMA preferred) */
};

static void libConvertActToRilState(int AcT, char *state);
static int getNeighboringCellId(RIL_Token token);
void reportCellInfo(void *param);
int callback_RequestOperator(ATResponse* response, struct requestSession* session);
int callback_RequestRegistrationState(ATResponse* response, struct requestSession* session);
int callback_RequestGprsRegistrationState(ATResponse* response, struct requestSession* session);
int callback_SetNetworkSelection(ATResponse* response, struct requestSession* session);
int callback_QueryAvailableNetworks(ATResponse* response, struct requestSession* session);
int callback_RequestSignalStrength(ATResponse* response, struct requestSession* session);
int callback_QueryNetworkSelectionMode(ATResponse* response, struct requestSession* session);
int callback_RequestGetNeighboringCellId(ATResponse* response, struct requestSession* session);
int callback_GetPreferredNetworkType(ATResponse* response, struct requestSession* session);


extern void disableInterface(int cid);
extern void disablePPPInterface(int cid);


RIL_AT_Map table_mm[] = {
	{ RIL_REQUEST_SCREEN_STATE,		       NO_RESULT,	      NULL				    }, //Local implementation
	{ RIL_REQUEST_OPERATOR,			       MULTILINE,	      callback_RequestOperator		    },
	{ RIL_REQUEST_REGISTRATION_STATE,	       SINGLELINE,	      callback_RequestRegistrationState	    },
	{ RIL_REQUEST_GPRS_REGISTRATION_STATE,	       SINGLELINE,	      callback_RequestGprsRegistrationState },
	{ RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE,    SINGLELINE,	      callback_QueryNetworkSelectionMode    },
	{ RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC, NO_RESULT,	      callback_SetNetworkSelection	    },
	{ RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL,    NO_RESULT,	      callback_SetNetworkSelection	    },
	{ RIL_REQUEST_QUERY_AVAILABLE_NETWORKS,	       SINGLELINE,	      callback_QueryAvailableNetworks	    },
	{ RIL_REQUEST_SIGNAL_STRENGTH,		       SINGLELINE,	      callback_RequestSignalStrength	    },
	{ RIL_REQUEST_SET_LOCATION_UPDATES,	       NO_RESULT,	      callback_DefaultResponse		    },
	{ RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE,      NO_RESULT,	      callback_DefaultResponse		    },
	{ RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE,      SINGLELINE,	      callback_GetPreferredNetworkType	   },
	{ RIL_REQUEST_GET_NEIGHBORING_CELL_IDS,	       SINGLELINE,	      callback_RequestGetNeighboringCellId },
	{ RIL_REQUEST_SELECT_BAND,		       NO_RESULT,	      callback_DefaultResponse },
};

int onRequest_mm (int request, void *data, size_t datalen, RIL_Token token, char *cmdString,
		  ATCommandType *pType, char *prefix, requestCallback *pCallback)
{
	int continue_flag = 1;

	switch (request)
	{
	/* Local implementation of SCREEN_STATE by local varialble, which will decide whether report unsol msg of CSQ and CREG/CGREG */
	case RIL_REQUEST_SCREEN_STATE:
	{
		sScreenState = ((int *)data)[0];

		if (sScreenState)
			//notify CP that AP will wake
			at_send_command_sync(CHANNEL_ID(DEV), "AT*POWERIND=0", NO_RESULT, NULL, NULL, NULL);
		else
			//notify CP that AP will sleep
			at_send_command_sync(CHANNEL_ID(DEV), "AT*POWERIND=1", NO_RESULT, NULL, NULL, NULL);

		RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
		continue_flag = 0;
		break;
	}

	case RIL_REQUEST_REGISTRATION_STATE:
	{
		if ((sCregState.stat != -1) && (sCregState.lac != -1) && (sOperInfo.act != -1))
		{
			char * responseStr[4];
			char AccessTech[2];
			asprintf(&responseStr[0], "%d", sCregState.stat);
			asprintf(&responseStr[1], "%x", sCregState.lac);
			asprintf(&responseStr[2], "%x", sCregState.cid);

			/* Convert AcT value of 3GPP spec to GPRS reg state defined in ril.h */
			libConvertActToRilState(sOperInfo.act, AccessTech);
			responseStr[3] = AccessTech;

			LOGD("%s: Return local saved registration state", __FUNCTION__);
			RIL_onRequestComplete(token, RIL_E_SUCCESS, responseStr, sizeof(responseStr));
			FREE(responseStr[0]);
			FREE(responseStr[1]);
			FREE(responseStr[2]);
			continue_flag = 0;
		}
		else
		{
			strcpy(cmdString, "AT+CREG?");
			strcpy(prefix, "+CREG:");
		}
		break;
	}

	case RIL_REQUEST_GPRS_REGISTRATION_STATE:
	{
		if ((sCgregState.stat != -1) && (sCgregState.lac != -1) && (sOperInfo.act != -1))
		{
			char * responseStr[4];
			char gprsState[2];
			asprintf(&responseStr[0], "%d", sCgregState.stat);
			asprintf(&responseStr[1], "%x", sCgregState.lac);
			asprintf(&responseStr[2], "%x", sCgregState.cid);
			/* Convert AcT value of 3GPP spec to GPRS reg state defined in ril.h */
			libConvertActToRilState(sOperInfo.act, gprsState);
			responseStr[3] = gprsState;

			LOGD("%s: Return local saved GPRS registration state. GPRS state=%s", __FUNCTION__, responseStr[3]);
			RIL_onRequestComplete(token, RIL_E_SUCCESS, responseStr, sizeof(responseStr));
			FREE(responseStr[0]);
			FREE(responseStr[1]);
			FREE(responseStr[2]);
			continue_flag = 0;
		}
		else
		{
			strcpy(cmdString, "AT+CGREG?");
			strcpy(prefix, "+CGREG:");
		}
		break;
	}

	case RIL_REQUEST_OPERATOR:
	{
		if ( (sOperInfo.operLongStr[0] != '\0') && (sOperInfo.operShortStr[0] != '\0') &&  (sOperInfo.operNumStr[0] != '\0') )
		{
			char *result[3];
			result[0] =  &(sOperInfo.operLongStr[0]);
			result[1] =  &(sOperInfo.operShortStr[0]);
			result[2] =  &(sOperInfo.operNumStr[0]);
			LOGD("%s: Return local saved operator info", __FUNCTION__);
			RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(result));
			continue_flag = 0;
		}
		else
		{
			strcpy(cmdString, "AT+COPS=3,0;+COPS?;+COPS=3,1;+COPS?;+COPS=3,2;+COPS?");
			strcpy(prefix, "+COPS:");
		}
		break;
	}

	case RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE:
	{
		if (sOperInfo.mode != -1)
		{
			int result = 0;
			LOGD("%s: Return local saved network selection mode", __FUNCTION__);
			RIL_onRequestComplete(token, RIL_E_SUCCESS, &(sOperInfo.mode), sizeof(int));
			continue_flag = 0;
		}
		else
		{
			strcpy(cmdString, "AT+COPS?");
			strcpy(prefix, "+COPS:");
		}
		break;
	}

	case RIL_REQUEST_SIGNAL_STRENGTH:
	{
		strcpy(cmdString, "AT+CSQ");
		strcpy(prefix, "+CSQ:");
		break;
	}

	case RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC:
	{
		if (sOperInfo.mode == 0)
		{
			LOGD("%s: Current network is already automatically selected", __FUNCTION__);
			RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
			continue_flag = 0;
		}
		else
		{
			strcpy(cmdString, "AT+COPS=0");

			/* Reset all local saved reg info to NULL */
			resetLocalRegInfo();
		}
		break;
	}
	case RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL:
	{
		const char *oper;
		oper = (const char *)data;

		/* If manual selected network has been registered, not necessary to send AT cmd again */
		if (strcmp(oper, sOperInfo.operNumStr) == 0)
		{
			LOGD("%s: Current operator is already %s", __FUNCTION__, oper);
			RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
			continue_flag = 0;
		}
		else
		{
			sprintf(cmdString, "AT+COPS=1,2,%s", oper);

			/* Reset all local saved reg info to NULL */
			resetLocalRegInfo();
		}
		break;
	}

	case RIL_REQUEST_QUERY_AVAILABLE_NETWORKS:
	{
		strcpy(cmdString, "AT+COPS=?");
		strcpy(prefix, "+COPS:");
		break;
	}

	case RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE:
	{
		int networkType;
		int starBandNetworkType;
		networkType = ((int *)data)[0];

		if(networkType < 0 || networkType >= (int)sizeof(RILNetworkTypeTostarBandNetworkType)/(int)sizeof(RILNetworkTypeTostarBandNetworkType[0]))
			starBandNetworkType = -1;
		else
			starBandNetworkType = RILNetworkTypeTostarBandNetworkType[networkType];

		if(starBandNetworkType < 0)
		{
			RIL_onRequestComplete(token, RIL_E_MODE_NOT_SUPPORTED, NULL, 0);
			continue_flag = 0;
		}
		else
		{
			sprintf(cmdString, "AT*BAND=%d", starBandNetworkType);
		}

		break;
	}

	case RIL_REQUEST_GET_NEIGHBORING_CELL_IDS:
	{
		continue_flag = getNeighboringCellId(token);

		if(continue_flag == 1)
		{
			strcpy(cmdString, "AT+EEMGINFO?");
			strcpy(prefix, "+EEMGINFO :");

		}
		
		break;
	}

	case RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE:
	{
		strcpy(cmdString, "AT*BAND?");
		strcpy(prefix, "*BAND:");

		break;
	}


	case RIL_REQUEST_SET_LOCATION_UPDATES:
	{
		int req = ((int*)data)[0];
		if (req == 1)
		{
			sprintf(cmdString, "AT+CREG=2");
		}
		else if (req == 0)
		{
			sprintf(cmdString, "AT+CREG=1");
		}
		else
		{
			LOGW("%s:RIL_REQUEST_SET_LOCATION_UPDATES: invalid para:%d\n", __FUNCTION__, req);
			continue_flag = 0;
		}
		break;
	}

	case RIL_REQUEST_SELECT_BAND:
	{
		int req = ((int*)data)[0];
		int mode = -1;
		int band;

		switch(req)
		{
		case 1: /* GSM900 */
		{
			mode = MODE_GSM_ONLY;
			band = GSMBAND_PGSM_900;
			break;
		}

		case 2: /* GSM850 */
		{
			mode = MODE_GSM_ONLY;
			band = GSMBAND_GSM_850;
			break;
		}

		case 3: /* PCS */
		{
			mode = MODE_GSM_ONLY;
			band = GSMBAND_PCS_GSM_1900;
			break;
		}

		case 4: /* DCS */
		{
			mode = MODE_GSM_ONLY;
			band = GSMBAND_DCS_GSM_1800;
			break;
		}

		case 6: /* GSM ONLY */
		{
			mode = MODE_GSM_ONLY;
			band = 0;
			break;
		}

		case 7: /* WCDMA I */
		{
			mode = MODE_UMTS_ONLY;
			band = UMTSBAND_BAND_1;
			break;
		}

		case 8: /* WCDMA II */
		{
			mode = MODE_UMTS_ONLY;
			band = UMTSBAND_BAND_2;
			break;
		}

		case 9: /* WCDMA III */
		{
			mode = MODE_UMTS_ONLY;
			band = UMTSBAND_BAND_3;
			break;
		}

		case 10: /* WCDMA IV */
		{
			mode = MODE_UMTS_ONLY;
			band = UMTSBAND_BAND_4;
			break;
		}

		case 11: /* WCDMA V */
		{
			mode = MODE_UMTS_ONLY;
			band = UMTSBAND_BAND_5;
			break;
		}

		case 12: /* WCDMA VI */
		{
			mode = MODE_UMTS_ONLY;
			band = UMTSBAND_BAND_6;
			break;
		}

		case 13: /* WCDMA VII */
		{
			mode = MODE_UMTS_ONLY;
			band = UMTSBAND_BAND_7;
			break;
		}

		case 14: /* WCDMA VIII */
		{
			mode = MODE_UMTS_ONLY;
			band = UMTSBAND_BAND_8;
			break;
		}

		case 15: /* WCDMA VIIII */
		{
			mode = MODE_UMTS_ONLY;
			band = UMTSBAND_BAND_9;
			break;
		}

		case 16: /* WCDMA X */
		{
			/* not support */
			mode = -1;
			break;
		}

		case 17: /* WCDMA ONLY */
		{
			mode = MODE_UMTS_ONLY;
			band = 0;
			break;
		}

		case 18: /* Automatic */
		{
			mode = MODE_DUAL_MODE_AUTO;
			band = 0;
			break;
		}

		default:
		{
			mode = -1;
			break;
		}
		}

		if(mode == -1)
		{
			LOGW("%s: band not support: %d\n", __FUNCTION__, req);
			RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
			continue_flag = 0;
		}
		else
		{
			sprintf(cmdString, "AT*BAND=%d,%d", mode, band);
		}

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
		if ((index = getTableIndex(table_mm, request, NUM_ELEMS(table_mm))) != -1)
		{
			*pType = table_mm[index].type;
			*pCallback = table_mm[index].callback;
		}
		else
		{
			LOGW("%s: Can't find the request in table:%d\n", __FUNCTION__, request);
			continue_flag = 0;
		}
	}

	return continue_flag;
}

void onUnsolicited_mm (const char *s)
{
	char *line = NULL, *response, *plmn = NULL, *linesave = NULL;
	int err;

	/* Process ind msg of signal length */
	if (strStartsWith(s, "+CSQ:"))
	{
		int response[2];
		line = strdup(s);
		linesave = line;
		err = at_tok_start(&line);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &sCSQ[0]);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &sCSQ[1]);
		if (err < 0) goto error;

		/* CP is asserted or resetting, we need to reset our global variables */
		if (sCSQ[0] == 99 && sCSQ[1] == 99)
		{
			char value[PROPERTY_VALUE_MAX];
			sCregState.stat = -1;
			sCgregState.stat = -1;
			sOperInfo.mode = -1;
			sOperInfo.operLongStr[0] = '\0';
			sOperInfo.operShortStr[0] = '\0';
			sOperInfo.operNumStr[0] = '\0';
			sOperInfo.act = -1;
			AfterCPReset = 1;
			/* Init radio state to RADIO OFF  */
			setRadioState(RADIO_STATE_OFF);

			/* Deactive the PDP connection if exists */
			property_get("marvell.ril.ppp.enabled", value, "1");
			if (atoi(value))
				disablePPPInterface(atoi(DEFAULT_CID));
			else
				disableInterface(atoi(DEFAULT_CID));

			response[0] = 67;
			response[1] = 89;
			
			RIL_onUnsolicitedResponse(RIL_UNSOL_SIGNAL_STRENGTH, response, sizeof(response));

		}else
		{
			reportSignalStrength(NULL);
		}
	}

	/* Process ind msg of network registration status */
	else if (strStartsWith(s, "+CREG:") || strStartsWith(s, "+CGREG:"))
	{
		int responseInt[3], num;
		responseInt[1] = -1;
		responseInt[2] = -1;

		line = strdup(s);
		linesave = line;
		err = at_tok_start(&line);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &responseInt[0]);
		if (err < 0) goto error;

		if (at_tok_hasmore(&line))
		{
			err = at_tok_nexthexint(&line, &responseInt[1]);
			if (err < 0) goto error;

			if (at_tok_hasmore(&line))
			{
				err = at_tok_nexthexint(&line, &responseInt[2]);
				if (err < 0) goto error;

				if (at_tok_hasmore(&line))
				{
					err = at_tok_nextint(&line, &(sOperInfo.act));
					if (err < 0) goto error;
				}
			}
		}

		/* Save current reg state for query from upper layer */
		if (strStartsWith(s, "+CREG:"))
		{
			sCregState.stat = responseInt[0];
			sCregState.lac  = responseInt[1];
			sCregState.cid  = responseInt[2];
		}
		else if (strStartsWith(s, "+CGREG:"))
		{
			sCgregState.stat = responseInt[0];
			sCgregState.lac  = responseInt[1];
			sCgregState.cid  = responseInt[2];
		}

		/* Report to upper layer only when screen state is ON */
		if (sScreenState)
		{
			RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED, NULL, 0);
		}
	}

	/* Process ind msg of network time */
	else if (strStartsWith(s, "+NITZ:"))
	{
		linesave = strdup(s);
		response = linesave;
		at_tok_start(&response);

		while (*response == ' ')
			response++;

		plmn = response;

		if (strStartsWith(plmn, "PLMN Long Name"))
		{
			char *longname;
			at_tok_start(&plmn);
			at_tok_nextstr(&plmn, &longname);
			strcpy(sOperInfo.operLongStr, longname);
		}
		else if (strStartsWith(plmn, "PLMN Short Name"))
		{
			char *shortName;
			at_tok_start(&plmn);
			at_tok_nextstr(&plmn, &shortName);
			strcpy(sOperInfo.operShortStr, shortName);
		}

		RIL_onUnsolicitedResponse( RIL_UNSOL_NITZ_TIME_RECEIVED, response, strlen(response));
	}
	else if(strStartsWith(s, "+EEMUMTSINTER:") || strStartsWith(s, "+EEMUMTSINTRA:"))
	{
		//UMTS neighboring cell info
		int loop, i;
		int psc; //Primary Scrambling Code (as described in TS 25.331) in 9 bits in UMTS FDD ,
		int cellparaId; // cellParameterId in UMTS TDD
		int rscp;//Level index of CPICH Received Signal Code Power in UMTS FDD, PCCPCH Received Signal Code Power in UMTS TDD
		
		//TDD case	
		//+EEMUMTSINTER: 0, -826, 0, -792, 1120, 0, 65534, 0, 10071, 71
		//+EEMUMTSINTER: index, pccpchRSCP, utraRssi, sRxLev,mcc, mnc, lac, ci, arfcn, cellParameterId

		//FDD case 
		//+EEMUMTSINTER: 0, -32768, 0, -32768, -144, -760, 65535, 65535, 65534, 0, 10663, 440
		//+EEMUMTSINTER: index, cpichRSCP, utraRssi, cpichEcN0, sQual, sRxLev,mcc, mnc, lac, ci, arfcn, psc

		if(sCellNumber < MAX_NEIGHBORING_CELLS)
		{
			line = strdup(s);
			linesave = line;
			err = at_tok_start(&line);
			if (err < 0) goto error;

			err = at_tok_nextint(&line,&loop);
			if (err < 0) goto error;

			err = at_tok_nextint(&line,&rscp);
			if (err < 0) goto error;

			for(i = 0; i < 7; i++ )
			{
				err = at_tok_nextint(&line,&loop);
				if (err < 0) goto error;
			}

			err = at_tok_nextint(&line,&cellparaId);
			if (err < 0) goto error;

			
			//FDD cases
			if(at_tok_hasmore(&line))
			{
				err = at_tok_nextint(&line,&loop);
				if (err < 0) goto error;

				err = at_tok_nextint(&line,&psc);
				if (err < 0) goto error;

				asprintf(&sNeighboringCell[sCellNumber].cid, "%x", psc);
				sNeighboringCell[sCellNumber].rssi = rscp;
				sCellNumber++;

				
				LOGI("onUnsolicited_mm new cell info cid:%s, rssi: %d", sNeighboringCell[sCellNumber-1].cid, rscp);

			}
			else
			{
				asprintf(&sNeighboringCell[sCellNumber].cid, "%x", cellparaId);
				sNeighboringCell[sCellNumber].rssi = rscp;
				sCellNumber++;

				LOGI("onUnsolicited_mm new cell info cid:%s, rssi: %d", sNeighboringCell[sCellNumber-1].cid, rscp);
			}
			
		}
		else
		{
			LOGV("onUnsolicited_mm ignor cell info ");
		}

	}
	else if(strStartsWith(s, "+EEMGINFONC:"))
	{
		//GSM neighboring cell info
		// +EEMGINFONC: 2, 0, 0, 6334, 0, 0,41, 55, 29, 29, 516, 0, 29
		//+EEMGINFONC: nc_num, mcc, mnc, lac, rac, ci,rx_lv, bsic, C1, C2, arfcn, C31, C32
		int loop, i;
		int lac, ci; //Upper 16 bits is LAC and lower 16 bits is CID (as described in TS 27.005), use lac and ci
		int rssi;//Received RSSI, use rx_lv

		if(sCellNumber < MAX_NEIGHBORING_CELLS)
		{
			line = strdup(s);
			linesave = line;
			err = at_tok_start(&line);
			if (err < 0) goto error;

			for(i = 0; i<3; i++)
			{
				err = at_tok_nextint(&line,&loop);
				if (err < 0) goto error;
			}

			err = at_tok_nextint(&line,&lac);
			if (err < 0) goto error;

			err = at_tok_nextint(&line,&loop);
			if (err < 0) goto error;

			err = at_tok_nextint(&line,&ci);
			if (err < 0) goto error;

			err = at_tok_nextint(&line,&rssi);
			if (err < 0) goto error;

			asprintf(&sNeighboringCell[sCellNumber].cid, "%04x%04x", lac, ci);
			sNeighboringCell[sCellNumber].rssi = rssi;
			sCellNumber++;

		    LOGI("onUnsolicited_mm new cell info cid:%s, rssi: %d", sNeighboringCell[sCellNumber-1].cid, rssi);
		}
		else
		{
			LOGI("onUnsolicited_mm ignor cell info ");
		}

		
	}
	else if(strStartsWith(s, "+EEMUMTSINTERRAT:"))
	{
		LOGI("onUnsolicited_mm: not support +EEMUMTSINTERRAT");
	}
	else if(strStartsWith(s, "++EEMGINFOBASIC:"))
	{
		LOGI("onUnsolicited_mm: not support ++EEMGINFOBASIC");
	}
	else if(strStartsWith(s, "+EEMGINFOSVC:"))
	{
		LOGI("onUnsolicited_mm: not support +EEMGINFOSVC");
	}
	else if(strStartsWith(s, "+EEMGINFOPS:"))
	{
		LOGI("onUnsolicited_mm: not support +EEMGINFOPS");
	}
	else if(strStartsWith(s, "+EEMGINBFTM:"))
	{
		LOGI("onUnsolicited_mm: not support +EEMGINBFTM");
	}
	/* Porcess ind msg of ServiceRestrictionsInd */
	else if (strStartsWith(s, "+MSRI:"))
	{
		if(AfterCPReset)
			RIL_requestTimedCallback(initializeCallback, NULL, &TIMEVAL_0);

	}

	/* Free allocated memory and return */
	if (linesave != NULL) free(linesave);
	return;

 error:
	if (linesave != NULL) free(linesave);
	LOGE("%s: Error parameter in ind msg: %s", __FUNCTION__, s);
	return;
}

int onConfirm_mm(ATResponse* response, struct requestSession* session)
{
	int done = session->callback(response, session);

	return done;
}

/* Process AT reply of RIL_REQUEST_SIGNAL_STRENGTH */
int callback_RequestSignalStrength(ATResponse* response, struct requestSession* session)
{
	int err;
	int result[2];
	char *line;

	if (response->success == 0 || response->p_intermediates == NULL)
	{
		RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
		goto error;
	}

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
	LOGE("%s: Format error in this AT response", __FUNCTION__);
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Parse  AT reply of +CREG or +CGREG
 * Output: responseInt[0] : <stat>
		  responseInt[1] : <lac>
		  responseInt[2] : <cid>
		  responseInt[3] : <AcT>
 */
int parseResponseWithMoreInt(ATResponse* response, int responseInt[], int *pNum)
{
	int err = 0, num;
	char *line, *p;

	if (response->success == 0 || response->p_intermediates == NULL)
	{
		goto error;
	}

	line = response->p_intermediates->line;
	err = at_tok_start(&line);
	if (err < 0) goto error;

	num = 0;
	while (at_tok_hasmore(&line))
	{
		if(num == 2 || num == 3) //for <lac>,<cid>
		{
			err = at_tok_nexthexint(&line, &(responseInt[num]));
		}
		else
		{
			err = at_tok_nextint(&line, &(responseInt[num]));
		}
		if (err < 0) goto error;
		num++;
	}

	/* AT Reply format: +CREG: <n>,<stat>[,<lac>,<ci>[,<AcT>]] (Take +CREG: as example in following comments)   */
	switch (num)
	{
	case 2: /* +CREG: <n>, <stat> */
	{
		/* responseInt[1] is stat, copy to responseInt[0]. <lac> and <ci> are unavailable, <AcT> is unknown  */
		responseInt[0] = responseInt[1];
		responseInt[1] = -1;
		responseInt[2] = -1;
		responseInt[3] = -1;
		break;
	}
	case 4: /* +CREG: <n>, <stat>, <lac>, <cid> */
	case 5: /* +CREG: <n>, <stat>, <lac>, <cid>, <AcT> */
	{
		/* Need to change the place */
		responseInt[0] = responseInt[1];
		responseInt[1] = responseInt[2];
		responseInt[2] = responseInt[3];
		if (num == 5)
		{
			responseInt[3] = responseInt[4];
		}
		else
		{
			responseInt[3] = -1;
		}
		break;
	}

	default:
		goto error;
	}

	*pNum = num;

	return 0;

 error:
	return err;
}

/* Process AT reply of RIL_REQUEST_REGISTRATION_STATE */
int callback_RequestRegistrationState(ATResponse* response, struct requestSession* session)
{
	int responseInt[10];
	char * responseStr[4],radiotech[2];
	const char *cmd;
	const char *prefix;
	int num, err;

	/* count number of commas */
	/*commas = 0;
	   for (p = line ; *p != '\0' ;p++)
	   {
	    if (*p == ',') commas++;
	   }*/

	err = parseResponseWithMoreInt(response, responseInt, &num);
	if (err < 0) goto error;

	if (responseInt[0] > 5) responseInt[0] = 10; //Register state extention: 10 - Same as 0, but indicates that emergency calls are enabled
	
	asprintf(&responseStr[0], "%d", responseInt[0]);
	if(num > 2)
	{
		asprintf(&responseStr[1], "%x", responseInt[1]);
		asprintf(&responseStr[2], "%x", responseInt[2]);
	}
	else
	{
		responseStr[1] = NULL;
		responseStr[2] = NULL;
	}

	if(num == 5)
	{
		libConvertActToRilState(responseInt[3], radiotech);
		responseStr[3] = radiotech;
	}
	else
		responseStr[3] = NULL;
	
	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, responseStr, sizeof(responseStr));
	FREE(responseStr[0]);
	FREE(responseStr[1]);
	FREE(responseStr[2]);

	/* Save latest reg status locally */
	sCregState.stat = responseInt[0];
	sCregState.lac  = responseInt[1];
	sCregState.cid  = responseInt[2];
	if (num == 5)
		sOperInfo.act = responseInt[3];

	return 1;

 error:
	LOGE("%s: Format error in this AT response", __FUNCTION__);
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_GPRS_REGISTRATION_STATE */
int callback_RequestGprsRegistrationState(ATResponse* response, struct requestSession* session)
{
	int responseInt[10];
	char *responseStr[4], gprsState[2];
	const char *cmd;
	const char *prefix;
	int num, err;

	err = parseResponseWithMoreInt(response, responseInt, &num);
	if (err < 0) goto error;

	asprintf(&responseStr[0], "%d", responseInt[0]);
	if(num > 2)
	{
		asprintf(&responseStr[1], "%x", responseInt[1]);
		asprintf(&responseStr[2], "%x", responseInt[2]);
	}
	else
	{
		responseStr[1] = NULL;
		responseStr[2] = NULL;
	}

	if(num == 5)
	{
		/* Convert AcT value of 3GPP spec to GPRS reg state defined in ril.h */
		libConvertActToRilState(responseInt[3], gprsState);
		responseStr[3] = gprsState;
	}
	else
		responseStr[3] = NULL;
	
	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, responseStr, sizeof(responseStr));
	FREE(responseStr[0]);
	FREE(responseStr[1]);
	FREE(responseStr[2]);

	/* Save latest reg status locally */
	sCgregState.stat = responseInt[0];
	sCgregState.lac  = responseInt[1];
	sCgregState.cid  = responseInt[2];
	if (num == 5)
		sOperInfo.act = responseInt[3];

	return 1;

 error:
	LOGE("%s: Format error in this AT response", __FUNCTION__);
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_OPERATOR */
int callback_RequestOperator(ATResponse* response, struct requestSession* session)
{
	int err;
	int i;
	int skip;
	ATLine *p_cur;
	char *result[3];

	if (strStartsWith(response->finalResponse, "+CME ERROR:") || response->p_intermediates == NULL)
	{
		goto error;
	}

	memset(result, 0, sizeof(result));

	for (i = 0, p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next, i++ )
	{
		char *line = p_cur->line;

		err = at_tok_start(&line);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &(sOperInfo.mode));
		if (err < 0) goto error;

		// If we're unregistered, we may just get a "+COPS: 0" response
		if (!at_tok_hasmore(&line))
		{
			goto unregistered;
		}

		err = at_tok_nextint(&line, &skip);
		if (err < 0) goto error;

		// a "+COPS: 0, n" response is also possible
		if (!at_tok_hasmore(&line))
		{
			result[i] = NULL;
			continue;
		}

		err = at_tok_nextstr(&line, &(result[i]));
		if (err < 0) goto error;

		if (at_tok_hasmore(&line))
		{
			err = at_tok_nextint(&line, &(sOperInfo.act));
			if (err < 0) goto error;
		}
	}

	/* expect 3 lines exactly */
	if (i != 3) goto error;

	/* Save operator info locally */
	strcpy(sOperInfo.operLongStr, result[0]);
	strcpy(sOperInfo.operShortStr, result[1]);
	strcpy(sOperInfo.operNumStr, result[2]);

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, result, sizeof(result));

	/* [Jerry, 2009/01/06] When operator name is available, the network should be registered.
	 * If CP doesn't send indication msg, we need to query CP and update reg info
	 */
	#if 0 
	if ((sCregState.stat != 1) && (sCregState.stat != 5))
	{
		const struct timeval TIMEVAL_30s = { 30, 0 };
		RIL_requestTimedCallback(updateLocalRegInfo, NULL, &TIMEVAL_30s);
	}
	#endif

	return 1;

 unregistered:
	LOGD("RIL_REQUEST_OPERATOR callback: network not registered");
	/* The reason to return RIL_E_SUCCESS instead of RADIO_NOT_AVAILABLE:
	 * GsmServiceStateTracker handlePollStateResult() will cancelPollState if RADIO_NOT_AVAILABLE
	 * is received, which will cause phone.notifyServiceStateChanged() in pollStateDone() never be called,
	 * and it is root cause why after enabling airplane mode, the screen keeps waiting
	 */
	result[0] = NULL;
	result[1] = NULL;
	result[2] = NULL;
	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, result, sizeof(result));
	return 1;

 error:
	LOGE("%s: Error in this AT response", __FUNCTION__);
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE */
int callback_QueryNetworkSelectionMode(ATResponse* response, struct requestSession* session)
{
	int err;
	int result = 0;
	char *line;

	if (response->success == 0 || response->p_intermediates == NULL)
	{
		goto error;
	}

	line = response->p_intermediates->line;

	err = at_tok_start(&line);

	if (err < 0)
	{
		goto error;
	}

	err = at_tok_nextint(&line, &result);

	if (err < 0)
	{
		goto error;
	}

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, &result, sizeof(int));

	sOperInfo.mode = result;

	return 1;
 error:
	LOGE("%s: Respond error, return default value 0: auto selection", __FUNCTION__);
	result = 0;
	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, &result, sizeof(int));
	return 1;
}

/* Process AT reply of RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC and RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL*/
int callback_SetNetworkSelection(ATResponse* response, struct requestSession* session)
{
	callback_DefaultResponse(response, session);
	RIL_onUnsolicitedResponse( RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_QUERY_AVAILABLE_NETWORKS */
int callback_QueryAvailableNetworks(ATResponse* response, struct requestSession* session)
{
	int err, availableOptNumber, lparen;
	char *line, *p;
	char** result;

	if (response->success == 0 || response->p_intermediates == NULL)
	{
		RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
		return 1;
	}

	line = response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	/* count number of lparen */
	lparen = 0;
	for (p = line; *p != '\0'; p++)
	{
		if (*p == '(') lparen++;
	}

	/*
	 * the response is +COPS:(op1),(op2),...(opn),,(0,1,2,3,4),(0-6)
	 * so available operator count should be num_of_left_parentheses - 2
	 */
	if(lparen > 1)
		availableOptNumber = lparen - 2;
	else
		availableOptNumber = 0;
	LOGV("%s: available operator number:%d", __FUNCTION__, availableOptNumber);
	result = alloca(availableOptNumber * 5 * sizeof(char*));

	int i = 0;
	while (i < availableOptNumber)
	{
		char* status, *longname, *shortname, *numbername, *act;
		at_tok_nextstr(&line, &status);
		LOGV("status:%s", status);
		switch (status[1])
		{
		case '1':
			result[i * 5 + 3] = "available";
			break;
		case '2':
			result[i * 5 + 3] = "current";
			break;
		case '3':
			result[i * 5 + 3] = "forbidden";
			break;
		default:
			result[i * 5 + 3] = "unknown";
		}
		LOGV("state:%s", result[i * 5 + 3]);
		at_tok_nextstr(&line, &result[i * 5]);
		LOGV("longname:%s", result[i * 5]);
		at_tok_nextstr(&line, &result[i * 5 + 1]);
		LOGV("shortname:%s", result[i * 5 + 1]);
		at_tok_nextstr(&line, &result[i * 5 + 2]);
		LOGV("numbername:%s", result[i * 5 + 2]);
		at_tok_nextstr(&line, &act);
		LOGV("act:%s", act);
		switch (act[0])
		{
		case '0':
			result[i * 5 + 4] = "GSM";
			break;
		case '1':
			result[i * 5 + 4] = "GSM_COMPACT";
			break;
		case '2':
			result[i * 5 + 4] = "UTRAN";
			break;
		default:
			result[i * 5 + 4] = "UNKNOWN";
		}
		LOGV("act:%s", result[i * 5 + 4]);
		i++;
	}

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, result, sizeof(char*) * availableOptNumber * 5);
	return 1;

 error:
	LOGE("%s: Format error in this AT response", __FUNCTION__);
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_GET_NEIGHBORING_CELL_IDS */
int callback_RequestGetNeighboringCellId(ATResponse* response, struct requestSession* session)
{
	int err;
	int mode, network;
	char *line;
	const struct timeval TIMEVAL_2s = { 1, 0 };

	if (response->success == 0)
	{
		RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
		return 1;
	}

	//check network type UMTS or GSM
	line = response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &mode);
	if (err < 0) goto error;
	LOGV("EngModeinfo mode:%d", mode);

	err = at_tok_nextint(&line, &network);
	if (err < 0) goto error;
	LOGV("EngModeinfo network:%d", network);

	//reset cell info counter
	sCellNumber = 0;

	//start timer to collect cell info, and then restore engineering mode
	RIL_requestTimedCallback(reportCellInfo, session->token, &TIMEVAL_2s);

	return 1;
	
 error:
	LOGE("%s: Error parameter in response: %s", __FUNCTION__, response->p_intermediates->line);
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/* Process AT reply of RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE */
int callback_GetPreferredNetworkType(ATResponse* response, struct requestSession* session)
{
	int result[1];
	int mode;
	int rilNetworkType;
	char *line;
	int err;

	if (response->success == 0)
	{
		RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
		return 1;
	}

	line = response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &mode);
	if (err < 0) goto error;

	if(mode < 0 || mode >= (int)sizeof(starBandNetworkTypeToRILNetworkType)/(int)sizeof(starBandNetworkTypeToRILNetworkType[0]))
		rilNetworkType = -1;
	else
		rilNetworkType = starBandNetworkTypeToRILNetworkType[mode];

	if(rilNetworkType < 0) goto error;

	LOGI("%s: Preferred Network Type: %d", __FUNCTION__, rilNetworkType);
	result[0] = rilNetworkType;

	RIL_onRequestComplete(session->token, RIL_E_SUCCESS, result, sizeof(result));

	return 1;

 error:
	LOGE("%s: Error parameter in response: %s", __FUNCTION__, response->p_intermediates->line);
	RIL_onRequestComplete(session->token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 1;
}

/*********** External Function called by other files ********************/


void reportCellInfo(void *param)
{
	RIL_Token token = (RIL_Token)param;
	RIL_NeighboringCell * result[MAX_NEIGHBORING_CELLS];
	int i = 0;

	LOGI("reportCellInfo sCellNumber: %d", sCellNumber);

	if(sCellNumber > 0 && sCellNumber <= MAX_NEIGHBORING_CELLS)
	{
		for(i = 0; i < sCellNumber; i++)
			result[i] = &sNeighboringCell[i];
		
		RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sCellNumber* sizeof(RIL_NeighboringCell *));
	}
	else
	{
		RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	}

	//restore Engineering mode 
	at_send_command_sync(CHANNEL_ID(MM), "AT+EEMOPT=4", NO_RESULT, NULL, NULL, NULL);
}


/* Report to upper layer about signal strength (unsol msg of  +CSQ: ) when screen state is ON */
void reportSignalStrength(void *param)
{
	if (sScreenState)
	{
		RIL_onUnsolicitedResponse(RIL_UNSOL_SIGNAL_STRENGTH, sCSQ, sizeof(sCSQ));
	}
}

/* Reset all local saved reginfo and operInfo to NULL, force to update by AT cmd */
void resetLocalRegInfo(void)
{
	sOperInfo.mode = -1;
	sOperInfo.operLongStr[0] = '\0';
	sOperInfo.operShortStr[0] = '\0';
	sOperInfo.operNumStr[0] = '\0';
	sOperInfo.act = -1;
	sCregState.stat = -1;
	sCgregState.stat = -1;
}

/* Set flag whether permit to report CSQ or CREG/CGREG ind msg to RIL */
void setNetworkStateReportOption(int flag)
{
	sScreenState = flag;
}

/* Get registe state: return 1: registered, 0: unregistered */
int isRegistered(void)
{
	int regState;

	if ((sCregState.stat == 1) || (sCregState.stat == 5))
	{
		regState = 1;
	}
	else
	{
		regState = 0;
	}
	return regState;
}

/* Update Local Reg Info, if reg info changed, report network change unsol msg to upper layer  */
void updateLocalRegInfo(void *param)
{
	ATResponse *p_response = NULL;
	int responseInt[10], err, num;
	int oldRegState = sCregState.stat;
	static int query_times = 0;

	/* Update CREG info */
	err = at_send_command_sync(CHANNEL_ID(MM), "AT+CREG?", SINGLELINE, "+CREG:", NULL, &p_response);
	if (err < 0 || p_response->success == 0) goto error;

	err = parseResponseWithMoreInt(p_response, responseInt, &num);
	if (err < 0) goto error;

	if (responseInt[0] > 5) responseInt[0] = 10; //Register state extention: 10 - Same as 0, but indicates that emergency calls are enabled

	/* Save latest reg status locally */
	sCregState.stat = responseInt[0];
	sCregState.lac  = responseInt[1];
	sCregState.cid  = responseInt[2];

	at_response_free(p_response);
	p_response = NULL;

	/* Update CGREG info */
	err = at_send_command_sync(CHANNEL_ID(MM), "AT+CGREG?", SINGLELINE, "+CGREG:", NULL, &p_response);
	if (err < 0 || p_response->success == 0) goto error;

	err = parseResponseWithMoreInt(p_response, responseInt, &num);
	if (err < 0) goto error;

	/* Save latest gprs reg status locally */
	sCgregState.stat = responseInt[0];
	sCgregState.lac  = responseInt[1];
	sCgregState.cid  = responseInt[2];
	if (num == 5)
		sOperInfo.act = responseInt[3];

	if (oldRegState != sCregState.stat)
	{
		RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED, NULL, 0);
		query_times = 0;
	}

	else
	{
		query_times++;
		if ((query_times < 10) &&  (!isRegistered()))
		{
			const struct timeval TIMEVAL_15s = { 15, 0 };
			RIL_requestTimedCallback(updateLocalRegInfo, NULL, &TIMEVAL_15s);
		}
		else
		{
			query_times = 0;
		}
	}

	at_response_free(p_response);
	return;

 error:
	sCregState.stat = -1;
	sCgregState.stat = -1;
	LOGE("%s: Error in sending this AT response", __FUNCTION__);
	at_response_free(p_response);
	return;
}

/* Convert AcT value in AT cmd to GPRS reg value defined in ril.h
 *  Input para: AcT (3GPP spec definition):
 *      0	GSM
 *      1	GSM Compact
 *      2	UTRAN
 *      3	GSM w/EGPRS (see NOTE 1)
 *      4	UTRAN w/HSDPA (see NOTE 2)
 *      5	UTRAN w/HSUPA (see NOTE 2)
 *      6	UTRAN w/HSDPA and HSUPA (see NOTE 2)
 *  Output para: state (Refer to ril.h, RIL_REQUEST_GPRS_REGISTRATION_STATE)
  *      0 == unknown
 *      1 == GPRS only
 *      2 == EDGE
 *      3 == UMTS
 *      9 == HSDPA
 *      10 == HSUPA
 *      11 == HSPA
 */
static void libConvertActToRilState(int AcT, char *state)
{

	switch(AcT)
	{
		case 0:
		case 1:
			strcpy(state, "1"); //GPRS only
			break;
			
		case 2: 
			strcpy(state, "3"); // UMTS
			break;

		case 3:
			strcpy(state, "2"); //EDGE
			break;

		case 4:
			strcpy(state, "9"); //HSDPA
			break;

		case 5:
			strcpy(state, "10"); //HSUPA
			break;
			
		case 6:
			strcpy(state, "11"); //HSPA
			break;

		case -1:
		default:
			strcpy(state, "0"); //unknown
			break;
	}

	return;
}

static int getNeighboringCellId(RIL_Token token)
{
	int onOff, err;
	ATResponse *p_response = NULL;

	//if screen state is OFF, CP will not indicate CELL IDS
	if (!sScreenState)
	{
		RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
		return 0;
	}
			
	//save mode 
	err = at_send_command_sync(CHANNEL_ID(MM), "AT+EEMOPT=3", NO_RESULT, NULL, NULL, &p_response);
	if (err < 0 || p_response->success == 0) goto error;

	at_response_free(p_response);

	//Set query mode
	err = at_send_command_sync(CHANNEL_ID(MM), "AT+EEMOPT=1", NO_RESULT, NULL, NULL, &p_response);
	if (err < 0 || p_response->success == 0) goto error;

	at_response_free(p_response);
	return 1;

 error:
	at_response_free(p_response);
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return 0;
}


