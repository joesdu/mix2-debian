// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tas2559-container.h"

static unsigned char buf[2048];
static size_t used;

static void number(unsigned int value, unsigned int bytes)
{
	while (bytes) {
		bytes--;
		buf[used++] = value >> (bytes * 8);
	}
}

static void named(void)
{
	memset(buf + used, 0, 65);
	used += 65;
}

static void block(unsigned int version)
{
	number(0, 4);
	if (version >= 0x200)
		number(0, 4);
	number(1, 4);
	number(0x00000101, 4);
}

static void image(unsigned int version)
{
	named();
	number(1, 2);
	block(version);
}

static size_t fixture(unsigned int version)
{
	used = 0;
	number(0x35353532, 4);
	number(0, 4);
	number(0, 4);
	number(0, 4);
	number(0, 4);
	number(version, 4);
	number(0, 4);
	named();
	number(0, 4);
	number(4, 4);
	number(1, 2);
	named();
	block(version);
	number(1, 2);
	named();
	number(0, 3);
	image(version);
	number(1, 2);
	named();
	if (version >= 0x300 || (version >= 0x101 && version < 0x200))
		number(3, 2);
	number(0, 2);
	number(48000, 4);
	image(version);
	buf[4] = used >> 24;
	buf[5] = used >> 16;
	buf[6] = used >> 8;
	buf[7] = used;
	return used;
}

int main(void)
{
	struct tas2559_container_info info;
	unsigned int versions[] = { 0x100, 0x101, 0x200, 0x300 };
	unsigned char burst[] = { 0, 2, 0x85, 0, 0, 0, 126, 1, 2, 0, 0, 0 };
	unsigned char invalid[] = { 0, 0, 0x82, 0 };
	size_t n, i, j;

	assert(tas2559_validate_container(NULL, 0, &info) == -EINVAL);
	for (j = 0; j < sizeof(versions)/sizeof(versions[0]); j++) {
		n = fixture(versions[j]);
		assert(!tas2559_validate_container(buf, n, &info));
		assert(info.plls == 1 && info.programs == 1 && info.configurations == 1);
		/* Rewrite the declared size too, so truncation exercises section bounds. */
		for (i = 0; i < n; i++) {
			buf[4] = i >> 24; buf[5] = i >> 16;
			buf[6] = i >> 8; buf[7] = i;
			assert(tas2559_validate_container(buf, i, &info) == -EINVAL);
		}
		n = fixture(versions[j]);
		memset(buf + 92, 0xff, n - 92);
		assert(tas2559_validate_container(buf, n, &info) == -EINVAL);
	}
	/* Short bursts may be valid commands but cannot contain a 32-bit coefficient. */
	{
		unsigned int value = 0;
		unsigned char coeff[] = { 0, 4, 0x85, 0, 0, 0, 100, 0x80, 2, 3, 4, 0 };

		assert(!tas2559_coefficient(coeff, sizeof(coeff), 0, 0, 100, &value));
		assert(value == 0x80020304U);
		assert(tas2559_coefficient(coeff, sizeof(coeff), 0, 0, 101, &value) == -ENOENT);
		assert(tas2559_coefficient(burst, sizeof(burst), 0, 0, 124, &value) == -ENOENT);
		assert(tas2559_coefficient(coeff, 8, 0, 0, 100, &value) == -EINVAL);
		coeff[1] = 2;
		assert(tas2559_coefficient(coeff, sizeof(coeff), 0, 0, 100, &value) == -ENOENT);
	}
	assert(!tas2559_commands(burst, sizeof(burst)));
	for (i = 1; i < sizeof(burst); i++)
		assert(tas2559_commands(burst, i) == -EINVAL);
	burst[6] = 127;
	assert(tas2559_commands(burst, sizeof(burst)) == -EINVAL);
	burst[1] = 0;
	assert(tas2559_commands(burst, sizeof(burst)) == -EINVAL);
	assert(tas2559_commands(invalid, sizeof(invalid)) == -EINVAL);
	/* Exercise mutations in every byte of a valid container under sanitizers. */
	for (j = 0; j < sizeof(versions)/sizeof(versions[0]); j++) {
		n = fixture(versions[j]);
		for (i = 0; i < n; i++) {
			unsigned char saved = buf[i];
			unsigned int value;

			for (value = 0; value < 256; value++) {
				int ret;

				buf[i] = value;
				ret = tas2559_validate_container(buf, n, &info);
				assert(ret == 0 || ret == -EINVAL);
			}
			buf[i] = saved;
		}
	}
	puts("PASS: four format versions, every truncation and byte mutation, missing string terminator, burst bounds and unknown opcode");
	return 0;
}
