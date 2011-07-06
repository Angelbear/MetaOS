/*****************************************************************
 * CPK Crypto Library
 *
 * Author      : "Guan Zhi" <guanzhi1980@gmail.com>
 * Version     : 0.8
 * From	       : 2007-07-21 0.7
 * Last Update : 2007-08-20 0.7
 * Last Update : 2010-05-20 0.8
 *****************************************************************/

#ifndef HEADER_CPK_H
#define HEADER_CPK_H

#include <openssl/err.h>
#include <openssl/x509.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define CPK_LIB_VERSION		"0.8.1"
#define CPK_CMS_VERSION		4
#define CPK_MAX_ID_LENGTH	64


#include <openssl/ossl_typ.h>

typedef struct cpk_master_secret_st {
	long			 version;
	struct x509_name_st	*id;
	X509_ALGOR		*pkey_algor;
	X509_ALGOR		*map_algor;
	ASN1_OCTET_STRING	*secret_factors;
} CPK_MASTER_SECRET;
DECLARE_ASN1_FUNCTIONS(CPK_MASTER_SECRET)

typedef struct cpk_public_params_st {
	long			 version;
	struct x509_name_st	*id;
	X509_ALGOR		*pkey_algor;
	X509_ALGOR		*map_algor;
	ASN1_OCTET_STRING	*public_factors;
} CPK_PUBLIC_PARAMS;
DECLARE_ASN1_FUNCTIONS(CPK_PUBLIC_PARAMS)


X509_ALGOR *CPK_MAP_new_default();
int CPK_MAP_num_factors(const X509_ALGOR *algor);
int CPK_MAP_num_indexes(const X509_ALGOR *algor);
int CPK_MAP_str2index(const X509_ALGOR *algor, const char *str, int *index);

CPK_MASTER_SECRET *CPK_MASTER_SECRET_create(const char *domain_id,
	EVP_PKEY *pkey, X509_ALGOR *map_algor);
CPK_PUBLIC_PARAMS *CPK_MASTER_SECRET_extract_public_params(
	CPK_MASTER_SECRET *master);
EVP_PKEY *CPK_MASTER_SECRET_extract_private_key(CPK_MASTER_SECRET *master,
	const char *id);
EVP_PKEY *CPK_PUBLIC_PARAMS_extract_public_key(CPK_PUBLIC_PARAMS *params,
	const char *id);
CPK_MASTER_SECRET *d2i_CPK_MASTER_SECRET_bio(BIO *bp, CPK_MASTER_SECRET **master);
int i2d_CPK_MASTER_SECRET_bio(BIO *bp, CPK_MASTER_SECRET *master);
CPK_PUBLIC_PARAMS *d2i_CPK_PUBLIC_PARAMS_bio(BIO *bp, CPK_PUBLIC_PARAMS **params);
int i2d_CPK_PUBLIC_PARAMS_bio(BIO *bp, CPK_PUBLIC_PARAMS *params);


/* ERR function (should in openssl/err.h) begin */
#define ERR_LIB_CPK		130
#define ERR_R_CPK_LIB		ERR_LIB_CPK
#define CPKerr(f,r) ERR_PUT_error(ERR_LIB_CPK,(f),(r),__FILE__,__LINE__)
/* end */


void ERR_load_CPK_strings(void);

/* Error codes for the ECIES functions. */

/* Function codes. */
#define CPK_F_CPK_MASTER_SECRET_CREATE			100
#define CPK_F_CPK_MASTER_SECRET_EXTRACT_PUBLIC_PARAMS	101
#define CPK_F_CPK_MASTER_SECRET_EXTRACT_PRIVATE_KEY	102
#define CPK_F_CPK_PUBLIC_PARAMS_EXTRACT_PUBLIC_KEY	103
#define CPK_F_X509_ALGOR_GET1_DSA			104
#define CPK_F_X509_ALGOR_GET1_EC_KEY			105
#define CPK_F_CPK_MAP_new_default			106
#define CPK_F_CPK_MAP_NUM_FACTORS			107
#define CPK_F_CPK_MAP_NUM_INDEXES			108
#define CPK_F_CPK_MAP_STR2INDEX				109


/* Reason codes. */
#define CPK_R_BAD_ARGUMENT				100
#define CPK_R_UNKNOWN_DIGEST_TYPE			101
#define CPK_R_UNKNOWN_CIPHER_TYPE			102
#define CPK_R_UNKNOWN_MAP_TYPE				103
#define CPK_R_UNKNOWN_CURVE				104
#define CPK_R_STACK_ERROR				105
#define CPK_R_DERIVE_KEY_FAILED				106
#define CPK_R_ECIES_ENCRYPT_FAILED			107
#define CPK_R_ECIES_DECRYPT_FAILED			108
#define CPK_R_DER_DECODE_FAILED				109
#define CPK_R_UNSUPPORTED_PKCS7_CONTENT_TYPE		110
#define CPK_R_SET_SIGNER				111
#define CPK_R_SET_RECIP_INFO				112
#define CPK_R_UNABLE_TO_FIND_MESSAGE_DIGEST		113
#define CPK_R_BAD_DATA					114
#define CPK_R_MAP_FAILED				115
#define CPK_R_ADD_SIGNING_TIME				116
#define CPK_R_VERIFY_FAILED				117
#define	CPK_R_UNKNOWN_ECDH_TYPE				118
#define CPK_R_DIGEST_FAILED				119
#define CPK_R_WITHOUT_DECRYPT_KEY			120
#define CPK_R_UNKNOWN_PKCS7_TYPE			121
#define CPK_R_INVALID_ID_LENGTH				122
#define CPK_R_INVALID_PKEY_TYPE				123
#define CPK_R_INVALID_MAP_ALGOR				124


#ifdef  __cplusplus
}
#endif
#endif
