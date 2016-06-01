# teensy-stimulus

Teensy Stimulus allows Teensy 3.1 boards to provide stimulus trains suitable for animal behavior experiments.

## Overview

Teensy Stimulus allows a Teensy 3.1 or 3.2 board to function as a simple stimulus generator.  It listens for commands via serial-over-USB, then executes them.  It has a time resolution of 1 microsecond (target accuracy 100 microseconds), a maximum protocol length of 1,000,000 seconds, and can run up to 24 digital output channels and one analog output channel (for sinewaves or triangle waves of frequencies up to 1 KHz).

## Constructing Stimuli

A stimulus protocol consists of one or more **stimulus trains** which execute sequentially.  Each train consists of an **initial delay** followed by one or more repeats of a timed **stimulus on** phase followed by a timed **stimulus off**.  The pattern repeats until the total **duration** of the stimulus train has elapsed (counting the initial delay).

Digital stimuli consist of repeated **pulse on** and **pulse off** blocks.  Analog stimuli consist of a **frequency** and an **amplitude**.

Due to the limitations of the Teensy 3.1 analog/digital outputs, one cannot mix analog and digital outputs on the same channel.  Furthermore, a maximum of 254 stimulus trains may be used across all output channels.

TODO: example picture goes here.

## Communicating with a Teensy Stimulus device

Teensy Stimulus has a simple serial protocol for communications.

A fixed-length command starts with the symbol `~`.  The following byte or bytes determine how long the command is (content-dependent).

A variable-length command starts with the symbol `^` and ends with the symbol `$`.

The symbols ~, ^, and $ will ONLY appear at the beginning (`~`, `^`) or end (`$`) of a command.

Returned values start with the symbol `~` if they are fixed-length, or start with `^` and end with `$` if they are variable length.

No command or return value may be longer than 60 bytes in addition to the starting `^` and ending `$` if any.  (So the longest possible message is 62 bytes.)  This constraint is to ensure that the command fits within one USB packet sent/received by the board.

### Identifying a Teensy Stimulus device

If you send the command `~:`, a Teensy Stimulus device will respond with a string that starts with `~stim1.0 ` (the version number may be later) plus a device-specific identifying string (that has been set with the Teensy Stimulus EEPROM Programmer)

### Output channels

Each digital output channel is identified by a capital letter that refers to its pin number.  Pins 14 through 23 are lettered `A` through `J`; pins 0 through 13 continue with `K` through `X`.  Note that `X` is the Teensy 3.1 LED pin.  Note also that case is important: lower case letters do **not** refer to output channels.

The analog output channel is specified by `Z` and is on pin A14/DAC.

### Durations and Other Numbers

Teensy stimulus measures all times in seconds.  A _duration_ is given by eight decimal digits including an optional decimal point `.`.  A leading zero is required for times less than one second, and all values must be padded with zeros.  Thus, `0.0000001` is the shortest non-zero time possible and `99999999` is the longest (about three years); `1.000000` and `00000001` are two different ways to specify one second.

Note also that the internal clock on the Teensy 3.1 is not accurate to one part in ten million (or a hundred million), so the accuracy of these estimates should not be taken too seriously.  Synchronization should not be performed by dead reckoning alone, but by querying the internal clock of the Teensy.

### Specifying a Stimulus Train

Stimulus trains can be specified either as a single long command or piece by piece, as described below.

#### Specifying Stimulus Train Parameters

All commands to specify a stimulus train start with `~` followed by the output channel: `A` to `X` for digital channels or `Z` for the analog channel.

The third character, a lower case letter, specifies which parameter to set.

Then, any values are given.  Typically, this is an eight-character duration specification.

An example for each parameter is given below:

| Quantity | Command Char | Value | Analog/Digital? | Example | Explanation |
|----------|--------------|-------|-----------------|---------|-------------|
| total duration | `t` | duration | either  | `~At00000120` | Output `A`'s stimulus train will end after 120 seconds |
| initial delay  | `d` | duration | either  | `~Ad00000030` | Output `A` will wait 30 seconds before doing anything |
| stimulus on    | `s` | duration | either  | `~Ay0000.300` | Each stimulus for `A` will be on for 300 ms |
| stimulus off   | `z` | duration | either  | `~An0005.700` | Then `A` will wait 5700 ms (for a 6 second interval total) |
| pulse on       | `p` | duration | digital | `~Ap0.004500` | Within `A`'s stimulus, turn on the signal for 4.5 ms... |
| pulse off      | `q` | duration | digital | `~Aq0.005500` | ...then off for 5.5 ms (10 ms total, for 30 pulses / stimulus) |
| wave period    | `w` | duration | analog  | `~Zw0.004170` | Analog output for 239.8 Hz (note: minimum period is 1 ms) |
| wave amplitude | `a` | 0000-2047 | analog | `~Za2000`    | Near-maximum amplitude wave. |
| upright        | `u` | none     | either  | `~Au`        | Invert the normal sign: digital signals start high and go low, sine starts down instead of up |
| inverted       | `i` | none     | either  | `~Ai`        | Invert the normal sign: digital signals start high and go low, sine starts down instead of up |
| sinusoidal     | `l` | none     | analog  | `~Zl`        | Analog output will be sinusoidal (overrides `r`) |
| triangular     | `r` | none     | analog  | `~Zr`        | Analog output will be triangular (overrides `l`) |

Note that in analog outputs, only an integer number of wave half-periods are executed within the on time of a stimulus.  If a half-wave would not complete by the time a stimulus was to turn off, that half-wave will be skipped.  This is done to avoid high-frequency artifacts as an output suddenly vanishes.  Note also that analog outputs have a maximum range of 0-3.3V, so "off" will be 1.65V.  If you connect a 10 uF capacitor in-line with the output pin, you should effectively remove the 1.65V offset.  Note also that the maximum current is very low; an amplifier is needed to run a stimulus device.

Note also that very short stimuli may fail to complete as expected.  The software is designed to have a timing accuracy of around 100 microseconds.  Setting a pulse on duration of 5 microseconds is possible, but unlikely to produce the desired output.

#### Complete Stimulus Train Specification

A basic digital stimulus train can be set using the `=` command followed by durations for `t`, `d`, `s`, `z`, `p`, and `q`.  Each duration must be separated by `;`.

An annotated example is given below.

```
  /------------------------------------------------------- Train specification command
  |            /------------------------------------------ Initial delay
  |            |                  /----------------------- Stimulus off duration
  |            |                  |                 /----- Pulse off duration
  v         vvvvvvvv          vvvvvvvv          vvvvvvvv
~A=00000120;00000030;000000.3;000005.7;0.004500;0.005500
 ^ ^^^^^^^^          ^^^^^^^^          ^^^^^^^^
 |    |                 |                  \-------------- Pulse on duration
 |    |                 \--------------------------------- Stimulus on duration
 |    \--------------------------------------------------- Total stimulus train duration
 \-------------------------------------------------------- Output channel specifier
```

Analog stimuli must be set piece by piece.  Digital stimuli set all at once can be modified by further commands (to invert polarity, for instance).

To immediately run the command on that single channel, discarding all other settings, use `*` in place of `=`.

#### Chained Stimulus Trains

Stimulus trains can be chained one after another.

To clear all trains for one stimulus channel, use `.`, e.g. `~A.`  The channel `A` will be set to have only a single train, with all values set to zero.  To clear everything from all stimulus channels, use `~.`.

To add a new train after the existing specified one, use `+`, e.g. `~A+`.  All commands regarding this output pin will apply only to the second train after this command executes.  The second train will be initialized with zero values, and will begin executing as soon as the time on the first train elapses.

A maximum of 254 trains can be stored across all pins.  Each of the 25 initial pins reserves one train to begin with, leaving 229 free for extensions.

### Error States

The Teensy Stimulus state machine contains a single error state.  The machine can enter this state in response to invalid input that is dangerous to ignore: requesting a channel that is not there, trying to specify more states than are allowed, or setting parameters into an already-running protocol.  When in an error state, the system will accept commands but not parse any of them save for `~$` which will return `~!` if there is an error and `~.` if not; for `~?` which will report the error state (as `^error message here$` where the message hopefully contains some information about what went wrong); and for `~.` which will reset and clear the error state (at which point it can no longer be read out).

### Executing and Querying a Stimulation Protocol

To run a stimulation protocol on all defined channels, send the command `~!`.  To run on only a single defined channel, send the command `~A!` where `A` is the channel letter.  This will also clear the programs on all other pins.

To terminate a stimulation protocol in progress, send the command `~/`.  To terminate a single channel while leaving any others still running, use `~A/`.  Once terminated, a channel cannot be restarted.

After a run is complete, the previous program remains intact.  Use `~.` to reset all commands, or `~A.` to do it channel by channel.

To ask the board to tell what time point it is at, send the command `~?`.  If there is no error it will respond with `~12345678.123456` where elapsed duration is specified in seconds plus microseconds; if the board has started and is running it will always report at least one elapsed microsecond.  If the stimulus protocol has not yet been started or has already finished, it will return `~00000000.000000`.  If the system has encountered an error,it will return `^an error message here$`.

To query the state machine that runs an individual channel, send the command `~A$` for channel `A` (likewise for the others).  The board will respond with `~A`, followed by a number from `'0'` to `'3'` indicating its state (0 = not running, 1 = running but stimulus off, 2 = running and stimulus is on but not in a pulse, 3 = running and stimulus is on and in a pulse).  This is then followed by a `;` and the number of the stimulus train (in three digits), counting up from 0.

The Teensy Stimulus board will make a best effort to obey all the parameters set for it, but as the code does not form a hard real-time operating system, it may fail to switch at precisely the times requested.  To query an individual channel for error metrics, send the command `~A#` (for channel `A`).  It will respond with 8 numbers (after a `~`):

1. The number of stimuli that should have started (9 digits)
2. The number of stimuli that were missed (6 digits)
3. The number of pulses that should have started (9 digits)
4. The number of pulses that were missed (6 digits)
5. Max error for starting a pulse, in microseconds (5 digits)
6. Max error for ending a pulse, in microseconds (5 digits)
7. Total number of microseconds off for starting pulses (10 digits)
8. Total number of microseconds off for ending pulses (10 digits)

Overall, the reply is 61 bytes long.

For analog outputs, the difference between the target time for changing voltage and the actual time is reported instead of starting pulse edge values, and there are no stats for ending pulses.  This data will be preserved after the run is complete.  Since this reporting is expensive and may itself induce timing errors, it is recommended to only call this between stimuli or during debugging.

## Visual feedback

The Teensy board contains an on-board LED which will blink to report on its status.

If the device is improperly initialized, the LED will blink slowly (1s on, 1s off).

If the device is waiting to receive commands to set up stimuli and/or start running, the LED will flash briefly twice a second.  When a stimulus protocol has finished, these flashes will be especially brief (dim).

If the device has encountered an error while running, the LED will flash very quickly (five times a second).

While a stimulus protocol is running, the LED will be off by default.  If there is a protocol running on that output and there is no error, the LED will do whatever the protocol tells it to do.

## Loading the Teensy Stimulus program onto a Teensy 3.1 or later board

TODO: write this.

## Implementation Details

TODO: write this.

## Setting EEPROM Values for A Particular Device

TODO: write this.

## Complete Teensy Stimulator Command Reference

### Channel-Independent Commands

**Command string starts with `~`, followed by command char.**

#### No Parameters

| Command | Command Char | Result? | Additional Description |
|---------|--------------|---------|------------------------|
| Check errors | `$` | 2 chars: `~!` if error, otherwise `~.` | |
| Abort        | `.` | None | Stops any running protocol.  Also clears error state. |
| Run          | `!` | None | Starts all protocols running.  Sets error state if there is no protocol or one is running. |
| Report       | `?` | 15 chars: `~01234567.654321` or `^error message$` | Timestamp is 0 if not running. |
| Identity     | `:` | 10-22 chars: `^stim1.0 ` + message + `$` | message should contain identity of unit |

### Channel-Dependent Commands

**Command string starts with `~`, followed by channel number: `A` to `X` are digital channels; `Z` is analog.**

#### No Parameters

| Command | Command Char | Result? | Additional Description |
|---------|--------------|---------|------------------------|
| Abort run       | `.` | None | Turns off this output channel.  Remaining protocol (if any) continues. |
| Run alone       | `!` | None | Runs this output channel protocol alone.  Sets error state if another protocol is running. |
| Check state     | `$` | 5 chars: one run level, `;`, three digit train number | See text for details |
| Check quality   | `#` | 60 chars: 8 decimal numbers | See text for details |
| Usual polarity  | `u` | None | Stimuli are low-to-high (digital) or waveform is normal (analog). |
| Invert polarity | `i` | None | Stimuli are high-to low (digital) or waveform is upside-down (analog).  Raises error if running. |
| Sinusoidal      | `s` | None | Analog stimulus should be sinusoidal.  Raises error if running or applied to digital channel. |
| Triangular      | `r` | None | Analog stimulus should be triangular.  Raises error if running or applied to digital channel. |
| Append train    | `+` | None | Existing stimulus train is saved and a new one is set to follow the existing one |

#### With Parameters

| Command | Command Char | Parameter | Result? | Additional Description |
|---------|--------------|-----------|---------|------------------------|
| Set total time        | `t` | 8 chars: duration | None | |
| Set initial delay     | `d` | 8 chars: duration | None | |
| Set stimulus on time  | `y` | 8 chars: duration | None | |
| Set stimulus off time | `n` | 8 chars: duration | None | |
| Set pulse on time     | `p` | 8 chars: duration | None | Digital channels only. |
| Set pulse off time    | `q` | 8 chars: duration | None | Digital channels only. |
| Set period            | `w` | 8 chars: duration | None | Analog channels only.  Minimum period 1 ms. |
| Set amplitude         | `a` | 4 chars: amplitude | None | Analog channels only.  Max value 2047.  Time unit is ignored. |
| Set full protocol     | `=` | 53 chars: 6x8 durations + 5 `;` | None | Digital channels only. |
| Set and run protocol  | `*` | 53 chars: same as `=` | None | Raises error if protocol is already running. Runs this channel alone. |
