/*
[]=========================================================================[]
	
FILE:
	cryptoki_ext.h

DESC:
	The advanced cryptoki.h header file created by FTSafe to add the
	SSF33 algorithm disigned by China.

[]=========================================================================[]
*/

#ifndef __CRYPTOKI_FT_H__
#define __CRYPTOKI_FT_H__

#if defined(WIN32)
#include "cryptoki_win32.h"
#elif defined(linux) || defined(macintosh)
#include "cryptoki_linux.h"
#else
#	error "Support WIN32/Linux/MacOSx only. :)"
#endif

// to process the container name, add a defined property to PKCS#11
#define CKO_CONTAINER_NAME		(CKO_VENDOR_DEFINED + 0x0043544E)  // "CTN"
#define CKA_CONTAINER_NAME 		(CKA_VENDOR_DEFINED+0x00455053)  // "EPS"
#define CKA_REAL_KEY_INDEX		(CKA_VENDOR_DEFINED+0x00455054)  // "EPS" + 1

#define CKR_CONTAINER_EXIST		CKR_VENDOR_DEFINED + 0x0043544E // "CTN"
#define CKR_CONTAINER_NOT_EXIST	CKR_VENDOR_DEFINED + 0x0043544F


// common definition
#define SSF33_BLOCK_LEN			16
#define SSF33_KEY_LEN			16

// PKCS#11 key type for SSF33
#define CKK_SSF33			CKK_VENDOR_DEFINED + 33

// Mechanism for SSF33
#define CKM_SSF33_KEY_GEN		CKM_VENDOR_DEFINED + 33
//#define CKM_SSF33			CKM_VENDOR_DEFINED + 34
#define CKM_SSF33_CBC			CKM_VENDOR_DEFINED + 35
#define CKM_SSF33_ECB			CKM_VENDOR_DEFINED + 36
#define CKM_SSF33_CBC_PAD		CKM_VENDOR_DEFINED + 37
//#define CKM_SSF33_MAC_GENERAL	CKM_VENDOR_DEFINED + 38
//#define CKM_SSF33_MAC			CKM_VENDOR_DEFINED + 39

#define SCB2_BLOCK_LEN			16
#define SCB2_KEY_LEN			32

#define  CKK_SCB2			CKM_VENDOR_DEFINED+40
// Mechanism for SSF33
#define CKM_SCB2_KEY_GEN		CKM_VENDOR_DEFINED + 41
#define CKM_SCB2_CBC			CKM_VENDOR_DEFINED + 42
#define CKM_SCB2_ECB			CKM_VENDOR_DEFINED + 43
#define CKM_SCB2_CBC_PAD		CKM_VENDOR_DEFINED + 44

// CK_SSF33_CBC_PARAMS provides the parameters to the CKM_SSF33_CBC mechanism */
typedef struct CK_SSF33_CBC_PARAMS {
  CK_BYTE       iv[SSF33_BLOCK_LEN];            // IV for CBC mode
} CK_SSF33_CBC_PARAMS;

typedef CK_SSF33_CBC_PARAMS CK_PTR CK_SSF33_CBC_PARAMS_PTR;

// CK_SSF33_MAC_GENERAL_PARAMS provides the parameters for the
// CKM_SSF33_MAC_GENERAL mechanism 
typedef struct CK_SSF33_MAC_GENERAL_PARAMS {
  CK_ULONG      ulMacLength;      // Length of MAC in bytes
} CK_SSF33_MAC_GENERAL_PARAMS;

typedef CK_SSF33_MAC_GENERAL_PARAMS CK_PTR CK_SSF33_MAC_GENERAL_PARAMS_PTR;



#endif //__CRYPTOKI_FT_H__