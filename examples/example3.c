#include <stdio.h>
#include "pico/stdlib.h"
#include "dshot.h"

struct dshot_controller controller[4];

void telemetry_callback(void *context, int channel_unused, enum dshot_telemetry_type type, int value)
{
	/* the "unused" channel is always zero since there is only one channel per controller. */
	int channel = (int) context;
	/* do not report zero erpm */
	if (type == DSHOT_TELEMETRY_ERPM && value == 0)
		return;

	switch (type) {
	case DSHOT_TELEMETRY_ERPM:
		printf("channel %d: ERPM is %d\n", channel, value);
		break;
	case DSHOT_TELEMETRY_VOLTAGE:
		printf("channel %d: Voltage is %d V\n", channel, value);
		break;
	case DSHOT_TELEMETRY_CURRENT:
		printf("channel %d: Current is %d A\n", channel, value);
		break;
	case DSHOT_TELEMETRY_TEMPERATURE:
		printf("channel %d: Temperature is %d C\n", channel, value);
		break;
	}
}

int main(void)
{
	int time, i;

	stdio_init_all();

	/* initialize 4 channels, one per state machine, with DSHOT1200 on pio0,
	 * with 1 channels each starting on pin 2 */
	for (i = 0; i < 4; i++) {
		dshot_controller_init(&controller[i], 1200, pio0, i, 2 + i, 1);

		/* register a telemetry callback */
		dshot_register_telemetry_cb(&controller[i], telemetry_callback, (void *) i);
	}

	while (true) {
		time = to_ms_since_boot(get_absolute_time()) % 4000;
		/* at the beginning of the first second, spin the first motor. On the second second, spin the second motor, etc */
		for (i = 0; i < 4; i++) {
			if (time == 3500)
				dshot_command(&controller[i], 0, DSHOT_EXTENDED_TELEMETRY_ENABLE);
			else if (time > (i * 1000) && time < (i * 1000 + 100))
				dshot_throttle(&controller[i], 0, 100);
			else
				dshot_throttle(&controller[i], 0, 0);

			/* start the state machines */
			dshot_loop_async_start(&controller[i]);
		}

		/* read the results from the state machines and process telemetry */
		for (i = 0; i < 4; i++)
			dshot_loop_async_complete(&controller[i]);
	}

	return 0;
}
