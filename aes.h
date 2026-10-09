#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct { int x; } mbedtls_aes_context;
void mbedtls_aes_init(mbedtls_aes_context *); void mbedtls_aes_free(mbedtls_aes_context *);
int mbedtls_aes_setkey_enc(mbedtls_aes_context *, const unsigned char *, unsigned);
int mbedtls_aes_crypt_ctr(mbedtls_aes_context *, size_t, size_t *, unsigned char *, unsigned char *, const unsigned char *, unsigned char *);
#define MBEDTLS_AES_ENCRYPT 1
int mbedtls_aes_crypt_ecb(mbedtls_aes_context *, int, const unsigned char[16], unsigned char[16]);
