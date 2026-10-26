/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include "tas2559-program-policy.h"

int main(void)
{
	unsigned int value, reg;
	unsigned char burst[] = {0, 4, 0x85, 0, 0, 0, 3, 0, 0, 0, 0, 0};
	unsigned char memory[] = {0, 4, 0x85, 0, 140, 47, 40, 1, 2, 3, 4, 0};
	unsigned char delay[] = {0, 1, 0x81, 0};

	for (value = 0; value <= 255; value++) {
		/* No blob may touch the output power, mute, reset or selectors. */
		assert(!tas2559_diagnostic_byte(1, 0, 0, 1, value));
		assert(!tas2559_diagnostic_byte(1, 0, 0, 4, value));
		assert(!tas2559_diagnostic_byte(1, 0, 0, 5, value));
		assert(!tas2559_diagnostic_byte(1, 0, 0, 7, value));
		assert(!tas2559_diagnostic_byte(1, 100, 0, 7, value));
		assert(!tas2559_diagnostic_byte(2, 0, 0, 1, value));
		assert(!tas2559_diagnostic_byte(2, 0, 0, 7, value));
		assert(!tas2559_diagnostic_byte(1, 0, 0, 127, value));
		assert(!tas2559_diagnostic_byte(2, 0, 0, 127, value));
		assert(!tas2559_diagnostic_byte(1, 140, 47, 0, value));
		assert(!tas2559_diagnostic_byte(2, 0, 50, 0, value));
		assert(tas2559_diagnostic_byte(1, 0, 0, 6, value) == (value == 0x7c));
	}
	for (reg = 1; reg < 128; reg++)
		assert(!tas2559_diagnostic_byte(2, 140, 47, reg, 0));
	assert(tas2559_diagnostic_commands(1, burst, sizeof(burst)) == -EPERM);
	assert(tas2559_diagnostic_commands(1, memory, sizeof(memory)) == 0);
	assert(tas2559_diagnostic_commands(2, memory, sizeof(memory)) == -EPERM);
	assert(tas2559_diagnostic_commands(1, memory, sizeof(memory) - 1) == -EINVAL);
	assert(tas2559_diagnostic_commands(1, delay, sizeof(delay)) == -EPERM);
	assert(tas2559_diagnostic_commands(3, memory, sizeof(memory)) == -EINVAL);
	assert(tas2559_diagnostic_commands(1, NULL, 0) == -EINVAL);
	puts("PASS: diagnostic policy rejects all output-control values, selectors, cross-register bursts and delays");
	return 0;
}
