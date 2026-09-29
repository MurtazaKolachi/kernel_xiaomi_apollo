/* SPDX-License-Identifier: GPL-2.0 */
/* Host: cc -Wall -Wextra -Werror -o /tmp/ram_gain_test ram_gain_test.c */
#include <assert.h>
#include "../aw8697_gain.h"

int main(void)
{
	unsigned int word;
	unsigned int accepted = 0;

	for (word = 0; word <= 0xffff; ++word) {
		int gain = aw8697_decode_ram_gain(word);

		if (gain >= 0) {
			assert(gain <= 128);
			assert(word == (0x4700U | (unsigned int)gain));
			++accepted;
		}
	}
	assert(accepted == 129);
	assert(aw8697_decode_ram_gain(0x4700) == 0);
	assert(aw8697_decode_ram_gain(0x4780) == 128);
	assert(aw8697_decode_ram_gain(0x4781) == -1);
	assert(aw8697_decode_ram_gain(0xffff) == -1);
	return 0;
}
