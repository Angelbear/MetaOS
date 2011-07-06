/*
[]======================================================================[]

FILE:
	auxiliary.h

DESC:
	this file will include the auxiliary functions such as:
	token label modification function,
	container query function,

NOTE:
	you should include "cryptoki.h" before include this header file

[]======================================================================[]
*/

#ifndef __AUXILIARY_H__
#define __AUXILIARY_H__

#ifdef __cplusplus
extern "C" {
#endif


// ES_EVENT_XXXX must in range of 0x0001 to 0xFFFF
// Event code returned in the parameter pulEvent of function E_WaitForSlotEvent
#define ES_EVENT_TOKEN_INSERTED			0x0001
#define ES_EVENT_TOKEN_REMOVED			0x0002
#define ES_EVENT_OBJ_CREATE				0x0003
#define ES_EVENT_OBJ_DELETE				0x0004
#define ES_EVENT_OBJ_UPDATE				0x0005
#define ES_EVENT_PIN_CHANGED			0x0006
#define ES_EVENT_PIN_BLOCKED			0x0007
#define ES_EVENT_TOKEN_NAME				0x0008
#define ES_EVENT_CARDSTATE_CHANGED		0x0009
#define ES_EVENT_CARD_TIMEOUT			0x000A

#define ES_EVENT_READ_BEGIN				0x0100
#define ES_EVENT_READ_END				0x0101
#define ES_EVENT_READ_ERR				0x0102

#define ES_EVENT_WRITE_BEGIN			0x0103
#define ES_EVENT_WRITE_END				0x0104
#define ES_EVENT_WRITE_ERR				0x0105

#define ES_EVENT_GEN_KEYPAIR_BEGIN		0x0106
#define ES_EVENT_GEN_KEYPAIR_END		0x0107
#define ES_EVENT_GEN_KEYPAIR_ERR		0x0108

#define ES_EVENT_TOKEN_LOWINIT_BEGIN	0x0109
#define ES_EVENT_TOKEN_LOWINIT_END		0x010A
#define ES_EVENT_TOKEN_LOWINIT_ERR		0x010B

#define ES_EVENT_TOKEN_INIT_BEGIN		0x010C
#define ES_EVENT_TOKEN_INIT_END			0x010D
#define ES_EVENT_TOKEN_INIT_ERR			0x010E

#define ES_EVENT_TOKEN_BLANK_BEGIN		0x010F
#define ES_EVENT_TOKEN_BLANK_END		0x0110
#define ES_EVENT_TOKEN_BLANK_ERR		0x0111
#define ES_EVENT_FUS_SESSION_DISCONNECT	0x0112
#define ES_EVENT_FUS_SESSION_CONNECT	0x0113
#define ES_EVENT_FUS_MONITOR_CHANGED	0x0114
// Offset to Call EP_XXX
// Using with the parameter pAuxFunc in function E_GetAuxFunctionList 

#define EP_INIT_TOKEN_PRIVATE			0
#define EP_SET_TOKEN_LABEL				1
#define EP_GET_PIN_INFO					2
#define EP_WAITFORSLOTEVENT				3
#define EP_PARSE_COMBO_CERT				4
#define EP_SET_TOKEN_TIMEOUT			5
#define EP_GET_TOKEN_TIMEOUT			6
#define EP_GET_TOKEN_STATE				7
#define EP_BLANK_TOKEN					8 //5->8
#define EP_GET_DEV_INFO					9 //6->9
//InterPass
#define EP_ENCRYPT_INIT					10
#define EP_ENCRYPT						11
#define EP_ENCRYPT_FINAL				12
#define EP_SET_TOKEN_PARAM				13
//APDU
#define EP_BEGIN_TRANS_APDU				14
#define EP_TRANSEMIT_APDU				15
#define EP_END_TRANS_APDU				16


#define EP_FUNC_MAX_COUNT				20

//Token support system type 高位2字节表示文件系统类型;低位2字节表示子版本
#define ES_DEFAULT_FILE_SYSTEM			0x00000000
#define ES_1K_FAT8_FILE_SYSTEM			0x00010001
#define ES_2K_FAT8_FILE_SYSTEM			0x00010002
#define ES_NG_FAT8_FILE_SYSTEM			0x00010003
#define ES_BUDDY_FILE_SYSTEM			0x00020001
#define ES_PKCS15_FILE_SYSTEM			0x00030001

#pragma pack(push,1)
typedef struct _AUX_INIT_TOKEN_LOWLEVL_PKI
{
	CK_VERSION	version;
	//for 1.0 and later
	char*		strTokenName;
	char*		strOldSOPin;
	char*		strSOPin;
	char*		strUserPin;
	CK_BYTE		ucSOMaxPinEC;
	CK_BYTE		ucUserMaxPinEC;
	CK_BYTE		nRSAKeyPairCount;
	CK_BYTE		nDSAKeyPairCount;
	CK_ULONG	ulPubSize;
	CK_ULONG	ulPrvSize;
	CK_ULONG	ulComputerID;
	//for 1.1 and later
	CK_BYTE		ucCompareOEMID;
	CK_ULONG	ulTokenTimeout;
	//file system: ES_UNDEFINED_FILE_SYSTEM - undefined; ES_FAT8_FILE_SYSTEM - fat8; 
	//			   ES_BUDDY_FILE_SYSTEM - buddy; ES_PKCS15_FILE_SYSTEM - pkcs#15;
	CK_ULONG	ulFileSystemType;
	//for 1.2 and later
	CK_ULONG	ulTC;
	char*		strTransmitCode;
	//for 1.3 and later
	CK_BYTE		nECKeyPairCount;

} AUX_INIT_TOKEN_LOWLEVL_PKI, CK_PTR AUX_INIT_TOKEN_LOWLEVL_PKI_PTR;

typedef struct	_AUX_INIT_TOKEN_LOWLEVL_PKI \
				_AUX_INIT_TOKEN_LOWLEVL_PKI_V11,\
				AUX_INIT_TOKEN_LOWLEVL_PKI_V11, \
				CK_PTR AUX_INIT_TOKEN_LOWLEVL_PKI_V11_PTR;


typedef struct _AUX_PIN_INFO
{
	CK_BYTE		bSOPinMaxRetries;
	CK_BYTE		bSOPinCurCounter;
	CK_BYTE		bUserPinMaxRetries;
	CK_BYTE		bUserPinCurCounter;
	CK_FLAGS	pinflags;
}AUX_PIN_INFO, * AUX_PIN_INFO_PTR;

typedef struct _AUX_DEV_INFO
{
	CK_VERSION		version;				//struct version it is 1.0 (input)
	//for 1.0 and later
	CK_BYTE			ucProductType;			//0x01 = PKI; 0x02 = DONGLE; 0x04 = NULL
	CK_BYTE			ucFrequency;			//frequency
	CK_VERSION		hardwareVersion;		//Hardware version, same as in ATR;
	CK_BYTE			ucAtr[32];				//ATR value
	CK_ULONG		ulAtrLen;				//ATR length
	CK_BYTE			ucDate[6];				//yy-mm-dd-hh-mm-ss
	CK_BYTE			ucSerialNumber[16];		//Hardware sevial number
	CK_ULONG		ulSNLen;				//sevial number length
	CK_ULONG		ulTotalSpace;			//user's space;
	//for 1.1 and later
	CK_ULONG		ulFileSystemType;
}DEV_INFO, *DEV_INFO_PTR;

#define INIT_DEV_INFO(info)					\
do{											\
	memset(&(info),0,sizeof(info));			\
	(info).version.major = 1;				\
	(info).version.minor = 1;				\
}while(0);

typedef struct _AUX_FUNC_LIST
{
	CK_VERSION	version;  /* Auxiliary Version */
	void*		pFunc[EP_FUNC_MAX_COUNT];
}AUX_FUNC_LIST, CK_PTR AUX_FUNC_LIST_PTR, CK_PTR CK_PTR AUX_FUNC_LIST_PTR_PTR;


struct P12_DATA_BLOB
{
	CK_ULONG cbData; 
	CK_BYTE_PTR pbData;
};

struct AUX_P12_BLOB
{
	P12_DATA_BLOB certBlob; 
	P12_DATA_BLOB pKeyBlob; 
	P12_DATA_BLOB chainBlob; 
};

typedef AUX_P12_BLOB CK_PTR AUX_P12_BLOB_PTR;


#define TAG_TYPE_PCERT		0xA0
#define TAG_TYPE_CACERT			0xA1
#define TAG_TYPE_PUBKEY			0xA2
#define TAG_TYPE_PRIKEY			0xA3
#define TAG_TYPE_FNAME			0xA4
#define TAG_TYPE_FILETYPE		0xA5
#pragma pack(pop)

// Low level initialize the token
typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_InitTokenPrivate)
(	
	CK_SLOT_ID						slotID,		// ID of the token's slot
	CK_VOID_PTR						pInitParam
);

// set the token name (label) 
// rule: 
// 1. if (pPin == NULL or ulPinLen == 0), user must have login
// 2. userType != CKU_SO && userType != CKU_USER, user must have login
typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_SetTokenLabel)
(	
	CK_SLOT_ID		slotID,		// ID of the token's slot
	CK_USER_TYPE	userType,	// the user type
	CK_CHAR_PTR		pPin,		// the user pin
	CK_ULONG		ulPinLen,	// length in bytes of the PIN
	CK_UTF8CHAR_PTR	pLabel		// 32-byte token label() (blank padded)
);

typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_GetPinInfo)
(
	CK_SLOT_ID			slotID,			// (IN) ID of the token's slot
	AUX_PIN_INFO_PTR	pPinInfo		// (OUT) pin info of this token
);

typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_WaitForSlotEvent)
(
	CK_FLAGS			flags,		// (IN)  block or non-block mode.
	CK_SLOT_ID_PTR		pSlotId,	// (OUT) ID of the slot which have an event.
	CK_ULONG*			pulEvent,	// (OUT) the event happened.
	CK_ULONG*			pulExtData,
	CK_VOID_PTR			pReserved
);



typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_BlankToken)
(
	CK_SLOT_ID		slotID,		// ID of the token's slot
	CK_UTF8CHAR_PTR	pPin,		// the SO's initial PIN
	CK_ULONG		ulPinLen	// length in bytes of the PIN
	);

typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_GetDevInfo)
(
	CK_SLOT_ID			slotID,		// ID of the token's slot
	DEV_INFO_PTR	pDevInfo	// device info
);

typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_SetTokenTimeout)
(	
	CK_SLOT_ID		slotID,		// (IN) ID of the token's slot
	CK_ULONG		ulTimeout	// (IN) timeout of the token
	);

typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_GetTokenTimeout)
(	
	CK_SLOT_ID		slotID,		// (IN) ID of the token's slot
	CK_ULONG_PTR	ulTimeout	// (OUT) timeout of the token
);

typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_GetTokenState)
(	
	CK_SLOT_ID		slotID,		// (IN) ID of the token's slot
	CK_BBOOL		bTimeout,	// (IN) stat flag, must be set true
	CK_VOID_PTR		pState,		// (IN/OUT) buffer to get stat
	CK_ULONG_PTR	ulStatLen	// (IN/OUT) buffer length	
);

typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_ParseComboCertificate)
(
	CK_BYTE_PTR				pComboCert,		// (IN) buffer to be parsed
	CK_ULONG				ulComboCertLen,	// (IN) buffer length
	CK_BYTE_PTR				pPassword,		// (IN) password (for PKCS#12)
	CK_ULONG				ulPasswordLen,	// (IN) password length	
	CK_BYTE_PTR				pbTlvData,
	CK_ULONG_PTR			pulTlvLen
);

typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_BeginTransaction)
(
	CK_SLOT_ID		slotID	// (IN) ID of the token's slot
);

typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_TransmitAPDU)
(
	CK_SLOT_ID				slotID,		// (IN) (IN) ID of the token's slot
	CK_BYTE_PTR				pbSendBuf,	// (IN) Send apdu cmd data
	CK_ULONG				ulSendLen,	// (IN) Send apdu cmd data length
	CK_BYTE_PTR				pbRecvBuf,	// (OUT) reservie apdu buff data
	CK_ULONG_PTR			pulRecvLen,	// (IN/OUT) IN buffer max length, OUT return data length
	CK_FLAGS				flags,		//flags=0; NO; flags=1, mac_des(3);flags=2 ,enc_des(3)+mac_des(3);
	CK_BYTE_PTR				pbKey,
	CK_ULONG				ulKeyLen
);

typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_EndTransaction)
(
	CK_SLOT_ID		slotID		// (IN) ID of the token's slot
);
CK_DECLARE_FUNCTION(CK_RV, E_GetAuxFunctionList)
(
	AUX_FUNC_LIST_PTR_PTR pAuxFunc
);

typedef
CK_DECLARE_FUNCTION_POINTER(CK_RV, EP_GetAuxFunctionList)
(
	AUX_FUNC_LIST_PTR_PTR pAuxFunc
);

#ifdef __cplusplus
}
#endif

#endif // __AUXILIARY_H__

// EOF


