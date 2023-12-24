#include <stdio.h>
#include "pico/stdlib.h"
#include "dshot.h"

struct dshot_controller controller0;

void telemetry_callback(void *context, int channel, enum dshot_telemetry_type type, int value)
{
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
	int time;

	stdio_init_all();

	/* initialize controller 0 with DSHOT600 on pio0, state machine 0
	 * with 4 channels starting on pin 2 */
	dshot_controller_init(&controller0, 600, pio0, 0, 2, 4);

	/* register a telemetry callback */
	dshot_register_telemetry_cb(&controller0, telemetry_callback, NULL);

	while (true) {
		time = to_ms_since_boot(get_absolute_time()) % 4000;
		/* at the beginning of the first second, spin the first motor. On the second second, spin the second motor, etc */
		for (int i = 0; i < 4; i++) {
			if (time > (i * 1000) && time < (i * 1000 + 100))
				dshot_throttle(&controller0, i, 100);
			else
				dshot_throttle(&controller0, i, 0);
		}

		if (time == 3000) {
			/* enable EDT if it hasn't been enabled yet ... */
			dshot_command(&controller0, 0, DSHOT_EXTENDED_TELEMETRY_ENABLE);
		}

		/* run the dshot loop */
		dshot_loop(&controller0);
	}

	return 0;
}
