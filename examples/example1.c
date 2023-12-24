#include <stdio.h>
#include "pico/stdlib.h"
#include "dshot.h"

struct dshot_controller controller0;

int main(void)
{
	stdio_init_all();

	/* initialize controller 0 with DSHOT600 on pio0, state machine 0
	 * with 1 channel on pin 2 */
	dshot_controller_init(&controller0, 600, pio0, 0, 2, 1);

	while (true) {
		/* beep every 4 seconds, send throttle value of zero otherwise */
		if ((to_ms_since_boot(get_absolute_time()) % 4000) == 0)
			dshot_command(&controller0, 0, DSHOT_CMD_BEEP1);
		else
			dshot_throttle(&controller0, 0, 0);

		/* run the dshot loop */
		dshot_loop(&controller0);
	}

	return 0;
}
