/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */
#include "internal/cryptlib.h"
#pragma hdrstop

#ifndef NO_ASN1_OLD

int ASN1_digest(i2d_of_void * i2d, const EVP_MD * type, char * data, uchar * md, uint * len)
{
	uchar * str, * p;
	int i = i2d(data, 0);
	if((str = (uchar*)OPENSSL_malloc(i)) == NULL) {
		ASN1err(ASN1_F_ASN1_DIGEST, ERR_R_MALLOC_FAILURE);
		return 0;
	}
	p = str;
	i2d(data, &p);
	if(!EVP_Digest(str, i, md, len, type, NULL)) {
		OPENSSL_free(str);
		return 0;
	}
	OPENSSL_free(str);
	return 1;
}

#endif

int ASN1_item_digest(const ASN1_ITEM * it, const EVP_MD * type, void * asn, uchar * md, uint * len)
{
	int i;
	uchar * str = NULL;
	i = ASN1_item_i2d((ASN1_VALUE*)asn, &str, it);
	if(!str)
		return 0;
	if(!EVP_Digest(str, i, md, len, type, NULL)) {
		OPENSSL_free(str);
		return 0;
	}
	OPENSSL_free(str);
	return 1;
}

