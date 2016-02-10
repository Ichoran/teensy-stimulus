# teensy-stimulus

Teensy Stimulus allows Teensy 3.1 boards to provide stimulus trains suitable for animal behavior experiments.

## Overview

Teensy Stimulus allows a Teensy 3.1 or 3.2 board to function as a simple stimulus generator.  It listens for commands via serial-over-USB, then executes them.  It has a time resolution of 0.1 milliseconds, a maximum protocol length of 1,000,000 seconds, and can run up to 24 digital output channels and one analog output channel (for sinewaves or triangle waves of frequencies up to 1 KHz).

## Constructing Stimuli

A stimulus protocol consists of one or more **stimulus trains** which execute sequentially.  Each train consists of an **initial delay** followed by one or more repeats of a timed **stimulus on** phase followed by a timed **stimulus off**.  The pattern repeats until the total **duration** of the stimulus train has elapsed (counting the initial delay).

Digital stimuli consist of repeated **pulse on** and **pulse off** blocks.  Analog stimuli consist of a **frequency** and a **shape**.

Due to the limitations of the Teensy 3.1 analog/digital outputs, one cannot mix analog and digital outputs on the same channel.  Furthermore, a maximum of 254 stimulus trains may be used across all output channels.

TODO: example picture goes here.

## Communicating with a Teensy Stimulus device

Teensy Stimulus has a simple serial protocol for communications.

All commands start with the symbol `~`.  Variable-length commands end with the symbol `$`, but most commands are fixed-length.

All returned values start with the symbol `~`.  Variable-length return values end with the symbol `$`, but most return values are fixed-length.

No command or return value may be longer than 60 bytes in addition to the starting `~` and ending `$` if any.  (So the longest possible message is 62 bytes.)

### Identifying a Teensy Stimulus device

If you send the command `~:`, a Teensy Stimulus device will respond with a string that starts with `~stim1.0 ` (the version number may be later) plus a device-specific identifying string (that has been set with the Teensy Stimulus EEPROM Programmer)

### Output channels

Digital output channels are specified by letter, starting with `A`.  Analog output channels are specified by number, starting with `0`.

### Durations and Other Numbers

Teensy Stimulus understands durations as six decimal digits to make a number from 000000-999999 followed by a single-letter unit.  Valid units are `s` for seconds, `m` for milliseconds, and `u` for microseconds.  Thus, every duration takes seven characters to specify.

Other numbers use either a decimal text format or hexatrigecimal (base 36) based on extended hexidecimal: 0-35 are represented by `0` to `9` and then `A` through `Z`.  Digital pin numbers, digital output channels, and the like, are specified this way.  Capitalization matters: lower case letters may not appear in a number.

### Specifying a Stimulus Train

Stimulus trains can be specified either as a single long command or piece by piece.

#### Checking Which Outputs are Valid

By default, 24 digital outputs (the maximum) and one analog output are available.  However, these may not be wired up to anything in a particular device.  Thus, a device can be programmed to accept only a subset of outputs.

To check which outputs are valid, send the command `~*`.  The Teensy will respond with a list of valid output channels between `~` and `$` characters.  For example, `~0123456789ABCDEF@$` would mean that the first sixteen output channels, plus the analog output channel (`@`), are available.

Note that the 24th digital output, `N`, is usually hooked up to the Teensy 3.1's LED pin.  (Pin number 13, or `D` in hexatrigecimal.)

#### Specifying Stimulus Train Parameters

All commands to specify a stimulus train start with `~` followed by the number of the output channel: `0` - `9`, then `A` - `N` for digital channels (be careful to not confuse capital `i` with one!); or `@` for the analog channel.

The third character, a lower case letter, specifies which parameter to set.

Then, any values are given.  Typically, this is a seven-character duration specification (6 decimal digits plus units character).

An example for each parameter is given below:

| Quantity | Command Char | Value | Analog/Digital? | Example | Explanation |
|----------|--------------|-------|-----------------|---------|-------------|
| total duration | `t` | duration | either | `~0t000120s` | Output `0`'s stimulus train will end after 120 seconds |
| initial delay | `d` | duration | either | `~0i030000m` | Output `0` will wait 30 seconds before doing anything |
| stimulus on | `y` | duration | either | `~0y000300m` | Each stimulus for `0` will be on for 300 ms |
| stimulus off | `n` | duration | either | `~0n005700m` | Then `0` will wait 5700 ms (for a 6 second interval total) |
| pulse on | `p` | duration | digital | `~0p004500u` | Within `0`'s stimulus, turn on the signal for 4.5 ms... |
| pulse off | `q` | duration | digital | `~0q005500u` | ...then off for 5.5 ms (10 ms total, for 30 pulses / stimulus) |
| wave period | `w` | duration | analog | `~@w004170u` | Analog output for 239.8 Hz (note: minimum period is 1 ms) |
| wave amplitude | `a` | 0000-2047 | analog | `~@a2000` | Near-maximum amplitude wave. |
| inverted | `i` | none | either | `~0i` | Invert the normal sign: digital signals start high and go low, sine starts down instead of up |
| sinusoidal | `s` | none | analog | `~@s` | Analog output will be sinusoidal (overrides `r`) |
| triangular | `r` | none | analog | `~@r` | Analog output will be triangular (overrides `s`) |

Note that in analog outputs, only an integer number of wave half-periods are executed within a stimulus on time.  If a half-wave would not complete by the time a stimulus was to turn off, that half-wave will be skipped.  This is done to avoid high-frequency artifacts as an output suddenly vanishes.

#### Complete Stimulus Train Specification

All aspects of a stimulus train can be set with a single command (command character `=`).  The format is explained below via an annotated example.

```
  /--------------------------------------------- Complete train specification command
  |          /---------------------------------- Initial delay
  |          |              /------------------- Stimulus off duration
  v       vvvvvvv       vvvvvvv       vvvvvvv--- Pulse off duration (wave amplitude for analog)
~0=000120s000030s000300m005700m004500u005500u;;
 ^ ^^^^^^^       ^^^^^^^       ^^^^^^^       ^^- Shape and/or inversion, padded with `;`
 |    |             |              \------------ Pulse on duration (wave period for analog)
 |    |             \--------------------------- Stimulus on duration
 |    \----------------------------------------- Total stimulus train duration
 \---------------------------------------------- Output channel specifier
```

Analog wave amplitude should be specified using a fake duration format.  Any unit can be used; the value should be between 0 and 2047.

If you wish to have a channel run multiple stimulus trains in succession, you can use the `+` command instead to add additional stimulus trains after the first.  If no initial train has been specified, this will succeed anyway and set the first stimulus train.

```
~0+000120s000030s000300m005700m004500u005500u;;
```

Finally, if you wish to set a single channel and run it at once, use the `!` command:

```
~0!000120s000030s000300m005700m004500u005500u;;
```

If a protocol is already running, it will be terminated and this one will start running instead.

### Error States

The Teensy Stimulus state machine contains a single error state.  The machine can enter this state in response to invalid input that is dangerous to ignore: requesting a channel that is not there, trying to specify more states than are allowed, or setting parameters one by one into an already-running protocol.  When in an error state, the system will accept commands but not parse any of them save for `~$` which will return `~X` if there is an error and `~$` if not; for `~?` which will report the error state (as `~!abcd` where `abcd` contains some information about what went wrong); and for `~.` which will reset the state.  The error can still be read out until some other command is executed, at which point the error information will be cleared.

### Executing and Querying a Stimulation Protocol

To run a stimulation protocol on all defined channels, send the command `~!`.  To run on only a single defined channel, send the command `~0!` where `0` is the channel number.  More channels may not be started until the protocol on that channel has ended.

To terminate a stimulation protocol in progress, send the command `~.`.  To terminate a single channel while leaving any others still running, use `~0.`.  Once terminated, a channel cannot be restarted.

When a stimulation protocol ends, either because of termination or running out of time, it is marked for deletion but isn't actually deleted until new commands are sent to define stimulus trains.  To recover the previous stimulation protocol, send the command `."`.  This must be sent before any new stimuli are defined; otherwise, all will be cleared.  This must also be done after running with `~A!`.

To ask the board to tell what time point it is at, send the command `~?`.  It will respond with `~123456m789012u` where duration is specified in seconds plus microseconds; if the board has started and is running it will always report at least one elapsed microsecond.  If the stimulus protocol has not yet been started or has already finished, it will return `~000000m000000u`.  If the system has encountered an error,it will return `~!` followed by twelve characters that may give a clue as to what went wrong, and terminated with `$`.  (It is not actually variable length; the reply is always exactly 15 characters long.)

To ask the board to reset its internal clock earlier, send the command `~<` followed by a duration.  It will not interrupt a stimulus to do this, but will shrink the gap between stimuli.  To ask the board to reset its internal clock to later, send `~>` followed by a duration.  This provides an approximate way to synchronize timing between the board and other devices.

To query the state machine that runs an individual channel, send the command `~0?` for channel `0` (likewise for the others).  The board will respond with `~`, then the protocol it is running in the same format used to set the channels, followed by four `+` or `-` characters.

The first character indicates whether that channel is running at all (`+` = yes, `-` = no).  The second indicates whether it a stimulus is active or not (`+` = yes, `-` = no).  The third indicates whether a pulse is active within a stimulus (`+` = yes, `-` = no; this is always `+` during a waveform unless part of a final half-wave is being skipped).  The fourth indicates whether there is another stimulus train after this one completes (`+` = yes, `-` = no).  The response is thus 49 characters long: the `~` message start symbol, 6 7-character durations, two characters for shape, and 4 `+` or `-` flags.

If no protocol has been specified, everything will be set to 0.

The Teensy Stimulus board will make a best effort to obey all the parameters set for it, but as the code does not form a hard real-time operating system, it may fail to switch at precisely the times requested.  To query an individual channel for error metrics, send the command `~0#` (for channel `0`).  It will respond with 8 numbers (after a `~`):

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
| Check errors | `$` | 2 chars: `~$` if okay, `~X` if error. |
| Abort        | `.` | None | Stops any running protocol.  Also clears error state. |
| Reset        | `"` | None | Restores previous protocol if applied after a completed run or abort.  Otherwise sets error state. |
| Run          | `!` | None | Starts protocol running.  Sets error state if there is no protocol or one is running. |
| Report       | `?` | 15 chars: `~012345s678901u` or `~!errormessage$` | Timestamp is 0 if not running. |
| Identity     | `:` | 10-22 chars: `~stim1.0 ` + message + `$` | message should contain identity of unit |
| Check outputs| `*` | 2-27 chars: `~` + list of pin numbers (hexatri) + `$` | If present, analog is `@` at 2nd to last char |

#### With Parameters

| Command | Command Char | Parameter | Result? | Additional Description |
|---------|--------------|-----------|---------|------------------------|
| Slow down    | `<` | 7 chars: `012345m` | None | Sets internal clock back (waits for stimulus to end).  Raises error if not running. |
| Speed up     | `>` | 7 chars: `012345m` | None | Sets internal clock forward (waits for stimulus to end).  Raises error if not running. |

### Channel-Dependent Commands

**Command string starts with `~`, followed by channel number: `0` to `9`, then `A` to `N` are digital channels; `@` is analog.**

Sending a command to a channel that is not enabled will raise an error.

#### No Parameters

| Command | Command Char | Result? | Additional Description |
|---------|--------------|---------|------------------------|
| Abort run       | `.` | None | Turns off this output channel.  Remaining protocol (if any) continues. |
| Run alone       | `!` | None | Runs this output channel protocol alone.  Sets error state if another protocol is running. |
| Check protocol  | `?` | 48 chars: 6 durations, 6 one-char flags | See text for details |
| Check quality   | `#` | 60 chars: 8 decimal numbers | See text for details |
| Invert polarity | `i` | None | Stimuli are high-to low (digital) or waveform is upside-down (analog).  Raises error if running. |
| Sinusoidal      | `s` | None | Analog stimulus should be sinusoidal.  Raises error if running or applied to digital channel. |
| Triangular      | `r` | None | Analog stimulus should be triangular.  Raises error if running or applied to digital channel. |

#### With Parameters

| Command | Command Char | Parameter | Result? | Additional Description |
|---------|--------------|-----------|---------|------------------------|
| Set duration          | `d` | 7 chars: duration | None | |
| Set initial delay     | `i` | 7 chars: duration | None | |
| Set stimulus on time  | `y` | 7 chars: duration | None | |
| Set stimulus off time | `n` | 7 chars: duration | None | |
| Set pulse on time     | `p` | 7 chars: duration | None | Digital channels only. |
| Set pulse off time    | `q` | 7 chars: duration | None | Digital channels only. |
| Set period            | `w` | 7 chars: duration | None | Analog channels only.  Minimum period 1 ms. |
| Set amplitude         | `a` | 4 chars: 0123 (decimal) | None | Analog channels only.  Max value 2047. |
| Set full protocol     | `=` | 44 chars: 6x duration + 2 flags or `;` | None | Analog amplitude should use a fake duration (any units). |
| Append protocol       | `+` | 44 chars: same as `=` | None | Same as `=` |
| Set and run protocol  | `!` | 44 chars: same as `=` | None | Raises error if protocol is already running. Runs this channel alone. |
