/* SPDX-License-Identifier: GPL-2.0-only */
/* Offline policy for the first Chiron programming experiment, not playback. */
#ifndef TAS2559_PROGRAM_POLICY_H
#define TAS2559_PROGRAM_POLICY_H

#include "tas2559-container.h"

/* Exact control writes observed in the stock program-0/config-4 image.
 * Power/mute/reset/selector writes from a blob are deliberately not accepted.
 * DSP memory contents still require trusted, device-specific firmware.
 */
static inline bool tas2559_diagnostic_byte(unsigned int device,
		unsigned int book, unsigned int page, unsigned int reg,
		unsigned int value)
{
	static const unsigned char controls[][5] = {
		{1, 0, 253, 13, 0x0d}, {1, 0, 254, 40, 0x00},
		{1, 0, 0, 8, 0x00}, {1, 0, 0, 10, 0x08},
		{1, 0, 0, 52, 0x03}, {1, 0, 0, 34, 0x03},
		{1, 100, 0, 70, 0x02},
		{1, 0, 1, 68, 0x1b}, {1, 0, 1, 98, 0x20},
		{1, 0, 1, 100, 0x81}, {1, 0, 1, 101, 0x81},
		{1, 0, 1, 103, 0x40}, {1, 0, 1, 102, 0x80},
		{1, 0, 1, 70, 0x1d}, {1, 0, 1, 69, 0x1c},
		{1, 0, 1, 77, 0x25}, {1, 0, 1, 65, 0x01},
		{1, 0, 1, 66, 0x01}, {1, 0, 1, 67, 0x15},
		{1, 0, 0, 6, 0x7c}, {1, 0, 0, 43, 0x03},
		{1, 100, 0, 40, 0x80},
		{1, 0, 1, 115, 0x0f}, {1, 0, 1, 116, 0x00},
		{1, 100, 0, 27, 0x01}, {1, 100, 0, 28, 0x30},
		{1, 100, 0, 29, 0x00}, {1, 100, 0, 30, 0x00},
		{1, 100, 0, 32, 0x03}, {1, 100, 0, 34, 0x08},
		{1, 100, 0, 2, 0x10}, {1, 100, 0, 33, 0x08},
		{1, 100, 0, 1, 0x08}, {1, 100, 0, 43, 0x00},
		{1, 100, 0, 44, 0x40}, {1, 100, 0, 31, 0x20},
		{1, 100, 0, 42, 0x00},
		{1, 0, 0, 47, 0x2a}, {1, 0, 0, 45, 0x17},
		{1, 0, 2, 6, 0x01},
		{2, 0, 0, 73, 0x0c}, {2, 0, 0, 60, 0x33},
		{2, 0, 0, 2, 0x02}, {2, 0, 0, 21, 0x01},
		{2, 0, 0, 15, 0x01}, {2, 0, 0, 16, 0x10},
		{2, 0, 0, 9, 0x83}, {2, 0, 0, 4, 0x5f},
		{2, 0, 0, 54, 0x32}, {2, 0, 0, 34, 0x3f},
		{2, 0, 0, 8, 0x01},
	};
	size_t i;

	if (!reg || reg > 127 || (!page && reg == 127))
		return false;
	if (device == 1 && reg >= 8 &&
	    ((book == 100 && page >= 1 && page <= 46) ||
	     (book == 120 && page >= 12 && page <= 49) ||
	     (book == 140 && ((page >= 1 && page <= 25) ||
			      (page >= 42 && page <= 56))) ||
	     (book == 0 && page >= 50 && page <= 61)))
		return true;
	for (i = 0; i < sizeof(controls) / sizeof(controls[0]); i++)
		if (controls[i][0] == device && controls[i][1] == book &&
		    controls[i][2] == page && controls[i][3] == reg &&
		    controls[i][4] == value)
			return true;
	return false;
}

static inline int tas2559_diagnostic_commands(unsigned int device,
		const unsigned char *data, size_t bytes)
{
	size_t pos = 0, count, i;
	unsigned int book, page, reg;
	const unsigned char *p;

	if ((device != 1 && device != 2) || !data || !bytes || bytes > 65536 ||
	    tas2559_commands(data, bytes))
		return -EINVAL;
	while (pos < bytes) {
		p = data + pos;
		book = p[0];
		page = p[1];
		reg = p[2];
		pos += 4;
		/* Selected blocks contain no delays. Reject rather than sleeping. */
		if (reg == 0x81)
			return -EPERM;
		count = 1;
		if (reg == 0x85) {
			count = (book << 8) | page;
			p = data + pos;
			book = p[0];
			page = p[1];
			reg = p[2];
			pos += (3 + count + 3) & ~(size_t)3;
		}
		for (i = 0; i < count; i++)
			if (!tas2559_diagnostic_byte(device, book, page, reg + i, p[3 + i]))
				return -EPERM;
	}
	return 0;
}
#endif
