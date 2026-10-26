/* SPDX-License-Identifier: GPL-2.0-only */
/* TI TAS2559 legacy container layout, checked without accessing hardware.
 * Layout reference: TI/Xiaomi tas2559-core.c, LineageOS commit
 * 7231883a368604f90806e167890e00f515a9927c. This checks structure, not
 * checksum authenticity, calibration suitability, or safe register values.
 */
#ifndef TAS2559_CONTAINER_H
#define TAS2559_CONTAINER_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#else
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#endif

struct tas2559_cursor {
	const unsigned char *data;
	size_t size;
	size_t pos;
	unsigned int driver;
};

struct tas2559_container_info {
	unsigned int driver;
	unsigned int plls;
	unsigned int programs;
	unsigned int configurations;
	unsigned int calibrations;
};

static inline int tas2559_take(struct tas2559_cursor *c, size_t n,
			       const unsigned char **out)
{
	if (c->pos > c->size || n > c->size - c->pos)
		return -EINVAL;
	if (out)
		*out = c->data + c->pos;
	c->pos += n;
	return 0;
}

static inline int tas2559_number(struct tas2559_cursor *c, unsigned int bytes,
				 unsigned int *out)
{
	const unsigned char *p;
	unsigned int i, value = 0;

	if (bytes > 4 || tas2559_take(c, bytes, &p))
		return -EINVAL;
	for (i = 0; i < bytes; i++)
		value = (value << 8) | p[i];
	*out = value;
	return 0;
}

static inline int tas2559_string(struct tas2559_cursor *c)
{
	while (c->pos < c->size) {
		if (!c->data[c->pos++])
			return 0;
	}
	return -EINVAL;
}

static inline int tas2559_named(struct tas2559_cursor *c)
{
	return tas2559_take(c, 64, NULL) || tas2559_string(c) ? -EINVAL : 0;
}

static inline int tas2559_commands(const unsigned char *p, size_t bytes)
{
	size_t pos = 0, count, padded;
	unsigned int offset;

	while (pos < bytes) {
		if (bytes - pos < 4)
			return -EINVAL;
		offset = p[pos + 2];
		if (offset <= 0x7f || offset == 0x81) {
			pos += 4;
			continue;
		}
		if (offset != 0x85)
			return -EINVAL;
		count = ((unsigned int)p[pos] << 8) | p[pos + 1];
		pos += 4;
		/* A burst has a three-byte address followed by count data bytes.
		 * The original executor rounds this payload up to four bytes.
		 */
		if (!count || bytes - pos < 3)
			return -EINVAL;
		offset = p[pos + 2];
		if (offset > 127 || count > 128 - offset)
			return -EINVAL;
		padded = (3 + count + 3) & ~(size_t)3;
		if (padded > bytes - pos)
			return -EINVAL;
		pos += padded;
	}
	return 0;
}

/* Locate a four-byte calibration coefficient without executing commands. */
static inline int tas2559_coefficient(const unsigned char *p, size_t bytes,
		unsigned int book, unsigned int page, unsigned int reg,
		unsigned int *value)
{
	size_t pos = 0, count, delta;
	unsigned int offset;

	if (!p || !value || book > 255 || page > 255 || reg > 124 ||
	    tas2559_commands(p, bytes))
		return -EINVAL;
	while (pos < bytes) {
		offset = p[pos + 2];
		if (offset != 0x85) {
			pos += 4;
			continue;
		}
		count = ((unsigned int)p[pos] << 8) | p[pos + 1];
		pos += 4;
		offset = p[pos + 2];
		if (p[pos] == book && p[pos + 1] == page && reg >= offset) {
			delta = reg - offset;
			if (delta <= count && count - delta >= 4) {
				const unsigned char *v = p + pos + 3 + delta;

				*value = ((unsigned int)v[0] << 24) |
					 ((unsigned int)v[1] << 16) |
					 ((unsigned int)v[2] << 8) | v[3];
				return 0;
			}
		}
		pos += (3 + count + 3) & ~(size_t)3;
	}
	return -ENOENT;
}

static inline int tas2559_block(struct tas2559_cursor *c)
{
	unsigned int type, commands;
	const unsigned char *p;
	size_t bytes;

	if (tas2559_number(c, 4, &type))
		return -EINVAL;
	/* Version >= 0x200 carries P/Y checksum presence and checksum bytes. */
	if (c->driver >= 0x200 && tas2559_take(c, 4, NULL))
		return -EINVAL;
	if (tas2559_number(c, 4, &commands) ||
	    commands > (c->size - c->pos) / 4)
		return -EINVAL;
	bytes = (size_t)commands * 4;
	if (tas2559_take(c, bytes, &p))
		return -EINVAL;
	return tas2559_commands(p, bytes);
}

static inline int tas2559_image(struct tas2559_cursor *c)
{
	unsigned int blocks, i;

	if (tas2559_named(c) || tas2559_number(c, 2, &blocks))
		return -EINVAL;
	for (i = 0; i < blocks; i++) {
		if (tas2559_block(c))
			return -EINVAL;
	}
	return 0;
}

static inline int tas2559_validate_container(const unsigned char *data,
		size_t size, struct tas2559_container_info *info)
{
	struct tas2559_cursor c = { .data = data, .size = size };
	struct tas2559_container_info result = { 0 };
	unsigned int magic, declared, family, device, i, program, pll, devices;
	unsigned int config;

	if (!data || !info || size < 104 || size > 16U * 1024U * 1024U)
		return -EINVAL;
	if (tas2559_number(&c, 4, &magic) || magic != 0x35353532 ||
	    tas2559_number(&c, 4, &declared) || declared != size ||
	    tas2559_take(&c, 12, NULL) ||
	    tas2559_number(&c, 4, &c.driver) ||
	    tas2559_take(&c, 4, NULL) || tas2559_named(&c) ||
	    tas2559_number(&c, 4, &family) || family != 0 ||
	    tas2559_number(&c, 4, &device) || device != 4)
		return -EINVAL;
	result.driver = c.driver;
	if (tas2559_number(&c, 2, &result.plls))
		return -EINVAL;
	for (i = 0; i < result.plls; i++) {
		if (tas2559_named(&c) || tas2559_block(&c))
			return -EINVAL;
	}
	if (tas2559_number(&c, 2, &result.programs))
		return -EINVAL;
	for (i = 0; i < result.programs; i++) {
		if (tas2559_named(&c) || tas2559_take(&c, 3, NULL) ||
		    tas2559_image(&c))
			return -EINVAL;
	}
	if (tas2559_number(&c, 2, &result.configurations))
		return -EINVAL;
	for (i = 0; i < result.configurations; i++) {
		if (tas2559_named(&c))
			return -EINVAL;
		if (c.driver >= 0x300 || (c.driver >= 0x101 && c.driver < 0x200)) {
			if (tas2559_number(&c, 2, &devices) || !devices || devices > 3)
				return -EINVAL;
		}
		if (tas2559_number(&c, 1, &program) ||
		    tas2559_number(&c, 1, &pll) ||
		    program >= result.programs || pll >= result.plls ||
		    tas2559_take(&c, 4, NULL) || tas2559_image(&c))
			return -EINVAL;
	}
	/* Calibration-only containers legitimately have no programs or PLLs:
	 * their indexes refer to the separately loaded main firmware. Cross-file
	 * validation must therefore be done by the caller before programming.
	 */
	if (c.pos < c.size) {
		if (tas2559_number(&c, 2, &result.calibrations))
			return -EINVAL;
		for (i = 0; i < result.calibrations; i++) {
			if (tas2559_named(&c) || tas2559_number(&c, 1, &program) ||
			    tas2559_number(&c, 1, &config) || tas2559_image(&c))
				return -EINVAL;
			if ((result.programs && program >= result.programs) ||
			    (result.configurations && config >= result.configurations))
				return -EINVAL;
		}
	}
	if (c.pos != c.size)
		return -EINVAL;
	*info = result;
	return 0;
}
#endif
