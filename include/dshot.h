#ifndef DSHOT_H
#define DSHOT_H

#include <stdint.h>
#include "hardware/pio.h"
#include "hardware/clocks.h"

/* Raspberry Pi Pico has 26 GPIOs */
#define DSHOT_MAX_CHANNELS		 	26
/* Emergency stop is performed after an idle timeout of 500 ms */
#define DSHOT_IDLE_THRESHOLD			(500 * 1000)

/**
 * dshot_telemetry_type - telemetry type which can be received by the eRPM protocol.
 * @DSHOT_TELEMETRY_ERPM: this is the eRPM value reported by the ESC.
 * 	To convert to a regular RPM value with N the number of magnets in the motor, compute
 * 	RPM = eRPM / N
 * @DSHOT_TELEMETRY_VOLTAGE: Voltage in Volt
 * @DSHOT_TELEMETRY_CURRENT: Current in Ampere
 * @DSHOT_TELEMETRY_TEMPERATURE: Temperature in Celsius
 */
enum dshot_telemetry_type {
	DSHOT_TELEMETRY_ERPM,
	DSHOT_TELEMETRY_VOLTAGE,
	DSHOT_TELEMETRY_CURRENT,
	DSHOT_TELEMETRY_TEMPERATURE,
};

/**
 * dshot_commands - DSHOT commands available
 */
enum dshot_commands {
	DSHOT_CMD_BEEP1 = 1,
	DSHOT_CMD_BEEP2 = 2,
	DSHOT_CMD_BEEP3	= 3,
	DSHOT_CMD_BEEP4 = 4,
	DSHOT_CMD_BEEP5	= 5,
	DSHOT_CMD_ESC_INFO = 6,
	DSHOT_CMD_SPIN_DIRECTION_1 = 7,
	DSHOT_CMD_SPIN_DIRECTION_2 = 8,
	DSHOT_CMD_3D_MODE_OFF = 9,
	DSHOT_CMD_3D_MODE_ON = 10,
	DSHOT_CMD_SETTINGS_REQUEST = 11,
	DSHOT_CMD_SAVE_SETTINGS = 12,
	DSHOT_EXTENDED_TELEMETRY_ENABLE	= 13,
	DSHOT_EXTENDED_TELEMETRY_DISABLE = 14,
	DSHOT_CMD_SPIN_DIRECTION_NORMAL = 20,
	DSHOT_CMD_SPIN_DIRECTION_REVERSED = 21,
	DSHOT_CMD_LED0_ON = 22,
	DSHOT_CMD_LED1_ON = 23,
	DSHOT_CMD_LED2_ON = 24,
	DSHOT_CMD_LED3_ON = 25,
	DSHOT_CMD_LED0_OFF = 26,
	DSHOT_CMD_LED1_OFF = 27,
	DSHOT_CMD_LED2_OFF = 28,
	DSHOT_CMD_LED3_OFF = 29,
	DSHOT_AUDIO_STREAM_TOGGLE = 30,
	DSHOT_SILENT_MODE_TOGGLE = 31,
	DSHOT_CMD_SIGNAL_LINE_TELEMETRY_DISABLE	= 32,
	DSHOT_CMD_SIGNAL_LINE_TELEMETRY_ENABLE = 33,
	DSHOT_CMD_SIGNAL_LINE_CONTINUOUS_ERPM_TELEMETRY = 34,
	DSHOT_CMD_SIGNAL_LINE_CONTINUOUS_ERPM_PERIOD_TELEMETRY = 35,
	DSHOT_CMD_SIGNAL_LINE_TEMPERATURE_TELEMETRY = 42,
	DSHOT_CMD_SIGNAL_LINE_VOLTAGE_TELEMETRY = 43,
	DSHOT_CMD_SIGNAL_LINE_CURRENT_TELEMETRY = 44,
	DSHOT_CMD_SIGNAL_LINE_CONSUMPTION_TELEMETRY = 45,
	DSHOT_CMD_SIGNAL_LINE_ERPM_TELEMETRY = 46,
	DSHOT_CMD_SIGNAL_LINE_ERPM_PERIOD_TELEMETRY = 47,
	DSHOT_MAX_COMMAND = 48,
};

/**
 * dshot_statistics - statistics per channel
 * @tx_frames: number of frames transmitted
 * @rx_frames: number of telemetry frames (succesfully) received
 * @rx_timeout: number of telemetry frames timed out, e.g. not received - happens if ESC is not connected or doesn't decode
 * @rx_bad_gcr: number of telemetry frames received with bad GCR, may be a timing problem or bit flips (less likely)
 * @rx_bad_crc: number of telemetry frames received with bad CRC, likely due to bit flips
 * @rx_bad_type: number of telemetry frames with an unsupported type
 */
struct dshot_statistics {
	uint32_t tx_frames;
	uint32_t rx_frames;
	uint32_t rx_timeout;
	uint32_t rx_bad_gcr;
	uint32_t rx_bad_crc;
	uint32_t rx_bad_type;
};

/**
 * dshot_motor - structure for a single motor/channel
 * @value: the dshot frame currently sent to the controller for this motor
 * @last_throttle_frame: backup copy of the last throttle value, when a command is sent
 * @command_counter: counter how often a command is still to be sent
 * @stats: TX/RX statistics structure
 */
struct dshot_motor {
	uint16_t frame;
	uint16_t last_throttle_frame;
	uint8_t command_counter;
	struct dshot_statistics stats;
};

/**
 * dshot_telemetry_callback_t - callback function for telemetry
 * @context: user specified context which was supplied on registration of the callback
 * @channel: channel on the controller on which the telemetry was received
 * @type: type of telemetry
 * @value: value according to the type
 */
typedef void (*dshot_telemetry_callback_t)(void *context, int channel, enum dshot_telemetry_type type, int value);

/**
 * dshot_controller - main controller structure
 * @c: PIO state machine configuration
 * @pio: PIO block on which this controller runs
 * @sm: state machine of the PIO block on which this controller runs
 * @pin: starting GPIO pin, the @num_channels consecutive pins starting with @pin are used
 * @num_channels: number of channels this controller controls
 * @channel: the channel (relative to @pin) this controller is currently operating on
 * @speed: DSHOT speed, e.g. 300 for DSHOT300
 * @motor: motor structures for each channel
 * @command_last_time: timestamp for the last command, used for emergency stop
 * @telemetry_cb: user callback function to receive telemetry data, 0 if unused
 * @telemetry_cb_context: user context for telemetry callback
 */
struct dshot_controller {
	pio_sm_config c;
	PIO pio;
	uint8_t sm;
	uint8_t pin;
	uint8_t num_channels;
	uint8_t channel;
	uint16_t speed;
	struct dshot_motor motor[DSHOT_MAX_CHANNELS];
	absolute_time_t command_last_time;

	dshot_telemetry_callback_t telemetry_cb;
	void *telemetry_cb_context;
};

/**
 * dshot_command - Set a command to be sent on the specified controller/channel
 * @controller: controller on which the command is to be sent
 * @channel: channel on which the command is to be sent
 * @command: command value to be sent
 *
 * The command is sent for a few cycles, and then the previous throttle value is applied again.
 */
void dshot_command(struct dshot_controller *controller, uint16_t channel, uint16_t command);

/**
 * dshot_throttle - Set a throttle value to be sent on the specified controller/channel
 * @controller: controller on which the throttle value is to be sent
 * @channel: channel on which the throttle value is to be sent
 * @throttle: throttle value to be sent
 *
 * The throttle value will be sent until further notice. Note that the throttle value
 * needs to be regularly refreshed to avoid the emergency off.
 */
void dshot_throttle(struct dshot_controller *controller, uint16_t channel, uint16_t throttle);

/**
 * dshot_controller_init - initializes a state machine for DSHOT
 * @controller: controller handle to be initialized (can/should be unitialized memory)
 * @dshot_speed: bitrate in kbit/s, e.g. 600 for DSHOT600
 * @PIO: PIO block to be used, e.g. pio0
 * @sm: PIO State machine to be used (0 to 3)
 * @pin: start pin for the controller, the next consecutive pins will be used
 * @channels: number of channels to be used
 *
 * Initialize a controller to send DSHOT frames (and receive DSHOT telemetry) on one or multiple channels.
 * Telemetry is using the eRPM method (and not the separate wire, like in the KISS telemetry) method.
 * A controller can control a single motor (channels = 1), a 4-in-1 ESC (channels = 4) or even multiple
 * controllers (e.g. four 4-in-1 ESCs, channels = 16). A single state machine is used per controller.
 *
 * Note that dshot_loop() needs to be called in a tight loop, the state machine will not operate on its own.
 */
void dshot_controller_init(struct dshot_controller *controller, uint16_t dshot_speed, PIO pio, uint8_t sm, int pin, int channels);

/**
 * dshot_controller - execute one iteration of the tight loop for a DSHOT controller
 * @controller: controller handle which should execute one tight loop iteration
 *
 * Executes one loop: switch to the next channel (if needed), send one frame and receive/interpret telemetry data if available.
 */
void dshot_loop(struct dshot_controller *controller);

/**
 * dshot_controller_async_start - asynchronously start one iteration of the tight loop for a DSHOT controller
 * @controller: controller handle which should execute one tight loop iteration
 *
 * Start one loop asynchronously: switch to the next channel (if needed), send one frame and return.
 */
void dshot_loop_async_start(struct dshot_controller *controller);

/**
 * dshot_controller_async_complete - asynchronously complete one iteration of the tight loop for a DSHOT controller
 * @controller: controller handle which should execute one tight loop iteration
 *
 * Complete one asynchronous loop: receive/interpret telemetry data if available.
 */
void dshot_loop_async_complete(struct dshot_controller *controller);

/**
 * dshot_register_telemetry_cb - register a callback for telemetry data
 * @controller: controller on which the callback should be registered
 * @telemetry_cb: user supplied telemetry callback function
 * @context: user supplied context which will be provided to the callback function on invocation
 *
 * This function allows to register a callback function to consume telemetry data received by the controller.
 */
void dshot_register_telemetry_cb(struct dshot_controller *controller, dshot_telemetry_callback_t telemetry_cb, void *context);

#endif // DSHOT_H
