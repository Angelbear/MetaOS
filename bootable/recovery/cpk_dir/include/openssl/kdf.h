#ifndef HEADER_KDF_H
#define HEADER_KDF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <openssl/evp.h>

typedef void *(*KDF_FUNC)(const void *in, size_t inlen, void *out, size_t *outlen);

KDF_FUNC KDF_get_x9_63(const EVP_MD *md);


#ifdef __cplusplus
}
#endif
#endif
