/*
* @author: huangzhishou
* @date: 2021-11-4 17:06:00
*/
#pragma once
#ifndef CORAL_MODULES_FOUNDATION_LEGACY_STREAM_CIPHER_HPP_INCLUDED
#define CORAL_MODULES_FOUNDATION_LEGACY_STREAM_CIPHER_HPP_INCLUDED

#include <cstring>
#define rc4_swap(a, b) (a) ^= (b), (b) ^= (a), (a) ^= (b)
static const int RC4_STATUS_VECTOR_SIZE = 256;
static const int MIN_SECTOR_SIZE = 512;

static const unsigned char rc4_default_key[128] =
{
	0x37, 0x62, 0x1c, 0xbc, 0xca, 0x09, 0x4c, 0xdb,
	0xae, 0x77, 0xa5, 0x94, 0x50, 0x6d, 0xdc, 0xe7,
	0x7e, 0xe2, 0x3f, 0xef, 0x0d, 0x4a, 0xba, 0xae,
	0xfd, 0x77, 0x6d, 0x85, 0x59, 0x91, 0xd3, 0x23,
	0x89, 0x30, 0x7e, 0xe5, 0x4c, 0xd1, 0x4e, 0x8f,
	0x99, 0x87, 0xe4, 0x25, 0x1c, 0x93, 0xdc, 0x29,
	0x62, 0xe9, 0xf8, 0xa2, 0xa8, 0x47, 0x4b, 0x9c,
	0xd0, 0xd8, 0xdd, 0xc1, 0xd0, 0x2d, 0x2d, 0x32,
	0x9e, 0x29, 0x5b, 0x4b, 0x82, 0x2b, 0x4d, 0x70,
	0xb8, 0x46, 0xfc, 0xb4, 0x10, 0x72, 0x9d, 0x9a,
	0xa8, 0xd0, 0x20, 0x67, 0x1e, 0xc3, 0x43, 0x29,
	0x86, 0x16, 0xb0, 0xaf, 0xdf, 0xec, 0xd8, 0x6e,
	0x20, 0xc9, 0xd0, 0x9e, 0xc1, 0x79, 0x40, 0xf8,
	0x8f, 0x79, 0x84, 0x45, 0xc2, 0xaf, 0x56, 0x17,
	0x37, 0xc2, 0xce, 0x26, 0x61, 0xc3, 0x42, 0x16,
	0xbc, 0xad, 0xd2, 0xea, 0xd7, 0x72, 0x22, 0xf9
};

struct RC4_STATUS
{
	int x, y;
	int m[RC4_STATUS_VECTOR_SIZE];
};

void _rc4_init(RC4_STATUS* s, const unsigned char* key, int len)
{
	int i = 0, j = 0;
	static int T[RC4_STATUS_VECTOR_SIZE];
	s->x = 0;
	s->y = 0;

	for (i = 0; i < RC4_STATUS_VECTOR_SIZE; ++i)
	{
		s->m[i] = i;
		T[i] = key[i % len];
	}
	for (i = 0; i < RC4_STATUS_VECTOR_SIZE; ++i)
	{
		j = (j + s->m[i] + T[i]) % RC4_STATUS_VECTOR_SIZE;
		rc4_swap(s->m[i], s->m[j]);
	}
}

void _rc4_crypt(RC4_STATUS* s, unsigned char* data, int len)
{
	int i = 0, x = 0, y = 0, t;
	x = s->x;
	y = s->y;

	for (i = 0; i < len; ++i)
	{
		x = (x + 1) % RC4_STATUS_VECTOR_SIZE;
		y = (y + s->m[x]) % RC4_STATUS_VECTOR_SIZE;
		rc4_swap(s->m[x], s->m[y]);
		t = (s->m[x] + s->m[y]) % RC4_STATUS_VECTOR_SIZE;
		data[i] ^= ((unsigned char)s->m[t]);
	}

	s->x = x;
	s->y = y;
}

void rc4_encrypt(void* buf, int len, const unsigned char* key, int keylen)
{
	unsigned char buffer[MIN_SECTOR_SIZE];
	RC4_STATUS s;
	int i, sizes;

	for (i = 0; i < len; i += MIN_SECTOR_SIZE)
	{
		sizes = len - i >= MIN_SECTOR_SIZE ? MIN_SECTOR_SIZE : len - i;
		memcpy(buffer, ((unsigned char*)buf) + i, MIN_SECTOR_SIZE);
		_rc4_init(&s, key, keylen);
		_rc4_crypt(&s, buffer, MIN_SECTOR_SIZE);
		memcpy(((unsigned char*)buf) + i, buffer, MIN_SECTOR_SIZE);
	}
}

void rc4_decrypt(void* buf, int len, const unsigned char* key, int keylen)
{
	rc4_encrypt(buf, len, key, keylen);
}

#endif // RC4_HPP（RC4 流密码头文件）
