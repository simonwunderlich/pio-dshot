#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/pio.h"
#include "dshot.pio.h"
#include "dshot.h"

uint dshot_pio_prog_offset = 0;

/**
 * dshot_sm_config_set_pin - helper function to (re)set the pin/channel to be used
 * @controller: dshot controller to be set
 * @pin: pin to be used going forward
 */
static void dshot_sm_config_set_pin(struct dshot_controller *controller, int pin)
{
	/* put them into the respective sets */
	sm_config_set_out_pins(&controller->c, pin, 1);
	sm_config_set_set_pins(&controller->c, pin, 1);
	sm_config_set_in_pins(&controller->c, pin);
	sm_config_set_jmp_pin(&controller->c, pin);

	pio_gpio_init(controller->pio, pin);
	gpio_set_pulls(pin, true, false);  // up, down
}

void dshot_controller_init(struct dshot_controller *controller, uint16_t dshot_speed, PIO pio, uint8_t sm, int pin, int channels)
{
	int i;

	memset(controller, 0, sizeof(controller));
	controller->pio = pio;
	controller->sm = sm;
	controller->num_channels = channels;
	controller->speed = dshot_speed;
	controller->pin = pin;
	for (i = 0; i < controller->num_channels; i++)
		dshot_throttle(controller, i, 0);

	if (dshot_pio_prog_offset == 0)
		dshot_pio_prog_offset = pio_add_program(pio0, &pio_dshot_program);

	controller->c = pio_dshot_program_get_default_config(dshot_pio_prog_offset);

	/* set shifting in the right direction */
	sm_config_set_out_shift(&controller->c, false, false, 32);
	sm_config_set_in_shift(&controller->c, false, false, 32);

	dshot_sm_config_set_pin(controller, pin);

	/* Set up clock divider so we use 40 cycles per bit, e.g.
	 * for DSHOT 1200 at 1200 kbit/s, we want to use a clock of 48 MHz */
	sm_config_set_clkdiv(&controller->c, 133000 /(((float)dshot_speed) * 40));

	/* initialize the program */
	pio_sm_init(pio, sm, dshot_pio_prog_offset, &controller->c);

	/* start the state machine */
	pio_sm_set_enabled(pio, sm, true);
}

void dshot_register_telemetry_cb(struct dshot_controller *controller, dshot_telemetry_callback_t telemetry_cb, void *context)
{
	controller->telemetry_cb = telemetry_cb;
	controller->telemetry_cb_context = context;
}

/**
 * dshot_gcr_lookup - converts a 5-bit GCR into the 4-bit integer representation
 * @gcr: gcr code to be translated
 * @error: pointer to an error integer, set if an error has been found
 *
 * Returns the 4-bit integer on success
 * */
static uint32_t dshot_gcr_lookup(int gcr, int *error)
{
	switch (gcr) {
	case 0x19:	return 0;
	case 0x1B:	return 1;
	case 0x12:	return 2;
	case 0x13:	return 3;
	case 0x1D:	return 4;
	case 0x15:	return 5;
	case 0x16:	return 6;
	case 0x17:	return 7;
	case 0x1A:	return 8;
	case 0x09:	return 9;
	case 0x0A:	return 10;
	case 0x0B:	return 11;
	case 0x1E:	return 12;
	case 0x0D:	return 13;
	case 0x0E:	return 14;
	case 0x0F:	return 15;
	}

	*error = 1;
	return 0xfffffff;
}

/**
 * dshot_interpret_erpm_telemetry - interpret a telemetry frame
 * @controller: dshot controller to be used
 * @edt: frame content (in extended telemetry format) to be interpreted
 *
 * Calls the callback function if registered
 */
static void dshot_interpret_erpm_telemetry(struct dshot_controller *controller, uint16_t edt)
{
	struct dshot_motor *motor = &controller->motor[controller->channel];
	enum dshot_telemetry_type type;
	uint16_t e, m;
	int value;

	e = (edt & 0xe000) >> 13;
	m = (edt & 0x1fff) >> 4;

	/* ERPM:
	 *
	 * eee pmmmmmmmm cccc
	 * 123 123456789 1234
	 *
	 * 3 bit: Amount that the following values need to be left shifted in order to get the periods in µs
	 * 9 bit: Period base (or telemetry value)
	 * 4 bit: CRC
	 *
	 * if p == 0, then this is a telemetry frame, with eee:
	 */

	switch ((edt & 0xf000) >> 12) {
	case 0x2:
		type = DSHOT_TELEMETRY_TEMPERATURE;
		value = m;
		break;
	case 0x4:
		type = DSHOT_TELEMETRY_VOLTAGE;
		value = m / 4;
		break;
	case 0x6:
		type = DSHOT_TELEMETRY_CURRENT;
		value = m;
		break;
	case 0x8:
	case 0xA:
	case 0xC:
	case 0xE:
		motor->stats.rx_bad_type++;
		return;
	default:
		type = DSHOT_TELEMETRY_ERPM;
		value = m << e;
		/* convert the period length into eRPM */
		if (value == 0xff80) /* special value reported by ESC */
			value = 0;
		else if (value != 0)
			value = (1000000 * 60) / value;
	}

	motor->stats.rx_frames++;
	if (controller->telemetry_cb)
		controller->telemetry_cb(controller->telemetry_cb_context, controller->channel, type, value);
}

/**
 * dshot_receive - receive a telemetry frame
 * @controller: dshot controller to be used
 * @value: raw frame received on the wire in GCR format
 */
static void dshot_receive(struct dshot_controller *controller, uint32_t value)
{
	int bit, i, sum, error = 0;
	uint16_t crc;
	uint32_t gcr, edt;
	struct dshot_motor *motor = &controller->motor[controller->channel];

	if (value == 0) {
		motor->stats.rx_timeout++;
		return;
	}

	/* get 20-bit GCR by shifting the 21-bit received value */
	gcr = (value ^ (value >> 1));

	edt = (dshot_gcr_lookup((gcr >> 15) & 0x1f, &error) << 12) |
	      (dshot_gcr_lookup((gcr >> 10) & 0x1f, &error) << 8) |
	      (dshot_gcr_lookup((gcr >>  5) & 0x1f, &error) << 4) |
	      (dshot_gcr_lookup((gcr)       & 0x1f, &error));
	edt = edt & 0xffff;

	if (error) {
		motor->stats.rx_bad_gcr++;
/*		printf("%s %d: value = %08x, gcr = %08x, edt = %08x, bad gcr, gcr ratio %d %%\n", __func__, __LINE__, value, gcr, edt,
		       (motor->stats.rx_bad_gcr * 100) / (motor->stats.rx_frames + motor->stats.rx_bad_gcr));*/
		return;
	}

	crc = ~((edt >> 4) ^ (edt >> 8) ^ (edt >> 12)) & 0x0f;

	if (crc != (edt & 0x0f)) {
/*		printf("%s %d: value = %08x, crc = %02x (expected %02x), bad crc, crc ratio %d %%\n", __func__, __LINE__, value, crc, (edt & 0x0f),
		       (motor->stats.rx_bad_crc * 100) / (motor->stats.rx_frames + motor->stats.rx_bad_crc));*/
		motor->stats.rx_bad_crc++;
		return;
	}

	dshot_interpret_erpm_telemetry(controller, edt);
}

/**
 * dshot_cycle_channel - switch to the next channel
 * @controller: controller on which the channel switch should be performed
 *
 * Switches to the next channel of the controller and restarts the PIO state machine
 */
static void dshot_cycle_channel(struct dshot_controller *controller)
{
	pio_sm_set_enabled(controller->pio, controller->sm, false);

	controller->channel = (controller->channel + 1) % controller->num_channels;
	dshot_sm_config_set_pin(controller, controller->pin + controller->channel);
	pio_sm_init(controller->pio, controller->sm, dshot_pio_prog_offset, &controller->c);

	pio_sm_set_enabled(controller->pio, controller->sm, true);
}

void dshot_loop_async_start(struct dshot_controller *controller)
{
	struct dshot_motor *motor;

	/* cycling channels is only necessary if there are actually channels to cycle through */
	if (controller->num_channels > 1)
		dshot_cycle_channel(controller);

	motor = &controller->motor[controller->channel];
	if (motor->command_counter > 0) {
		/* if a command is active, decrease the counter. If the command has been sent for the designated
		 * number of cycles, reset to the last throttle value */
		motor->command_counter--;
		if (motor->command_counter == 0)
			motor->frame = motor->last_throttle_frame;
	}

	if (pio_sm_is_tx_fifo_empty(controller->pio, controller->sm)) {
		uint32_t cycles = (25 * controller->speed * 40) / 1000;
		/* The upper 16 bits are used to send the frame. The frame gets inverted as this is inverted DSHOT. */
		pio_sm_put(controller->pio, controller->sm, ~motor->frame << 16);
		/* We need to wait 25us after the transmission to receive telemetry. We have the clock set to ~40 cycles per bit */
		pio_sm_put(controller->pio, controller->sm, cycles);
	}
}

void dshot_loop_async_complete(struct dshot_controller *controller)
{
	uint32_t recv;
	int rxlevel;

	/* the state machine should always push, regardless of whether it received something useful */
	recv = pio_sm_get_blocking(controller->pio, controller->sm);
	dshot_receive(controller, recv);

	/* handle emergency stop */
	if (absolute_time_diff_us(controller->command_last_time, get_absolute_time()) > DSHOT_IDLE_THRESHOLD) {
		for (int i = 0; i < controller->num_channels; i++)
			dshot_throttle(controller, i, 0);
	}

}

void dshot_loop(struct dshot_controller *controller)
{
	dshot_loop_async_start(controller);
	dshot_loop_async_complete(controller);
}

/**
 * dshot_compute_frame - compute the DSHOT frame content
 * @throttle: the throttle or command value to be sent
 * @telemetry: whether this a command (1) or a throttle value (0)
 *
 * Returns the 16 bit frame to be sent for DSHOT
 */
static uint16_t dshot_compute_frame(uint16_t throttle, int telemetry)
{
	uint16_t crc, ret, value;

	/* The frame format is (V = value, T = telemetry, C = CRC)
	 * VVVV VVVV VVVT CCCC
	 */

	value = (throttle << 1) | telemetry;

	crc = (value ^ (value >> 4) ^ (value >> 8));
	/* bidirectional gets the CRC inversed */
	crc = ~crc;

	return value << 4 | (crc & 0x0F);
}

void dshot_command(struct dshot_controller *controller, uint16_t channel, uint16_t command)
{
	struct dshot_motor *motor;

	if (channel >= controller->num_channels)
		return;

	motor = &controller->motor[channel];

	/* set telemetry bit to indicate that this is a command */
	motor->frame = dshot_compute_frame(command, 1);
	motor->command_counter = 100;

	controller->command_last_time = get_absolute_time();
}

void dshot_throttle(struct dshot_controller *controller, uint16_t channel, uint16_t throttle)
{
	struct dshot_motor *motor;

	if (channel >= controller->num_channels)
		return;

	motor = &controller->motor[channel];

	/* telemetry requested is always 0, as we don't want to use the separate telemetry line, but bidirectional dshot instead. */
	motor->frame = dshot_compute_frame(throttle, 0);
	motor->last_throttle_frame = motor->frame;
	motor->command_counter = 0;

	controller->command_last_time = get_absolute_time();
}
