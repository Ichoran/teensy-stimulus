# Ticklish

Ticklish allows Teensy 3.2 boards to provide stimulus trains suitable for animal behavior experiments.

_Note: Ticklish version 1 is available in the `v1` branch!_

## Overview

Ticklish allows a Teensy 3-series board to function as a simple stimulus generator.  It listens for commands via serial-over-USB, then executes them.  It has a time resolution of 1 microsecond (target accuracy 100 microseconds), a maximum protocol length of 100,000,000 seconds, and can run up to 24 digital output channels and one analog output channel (for sinewaves or triangle waves of frequencies up to 1 KHz).

You can also query the voltage of analog-capable pins not used for output, or the high/low state of digital-capable pins not used for output.  It can also read pulse-series off of a single digital-capable pin at a time.  Neither the LED pin nor the analog-out pin can be queried.

The project contains code to interface with the Teensy in a variety of languages (presently, Scala, C, Rust, Python, and LabView).  See the README files in the directories corresponding to each language to learn more.

## Constructing Stimuli

A stimulus protocol consists of one or more **stimulus trains** which execute sequentially.  Each train consists of an **initial delay** followed by one or more repeats of a timed **stimulus on** phase followed by a timed **stimulus off**.  The pattern repeats until the total **duration** of the stimulus train has elapsed (counting the initial delay).

Digital stimuli consist of repeated **pulse on** and **pulse off** blocks.  Analog stimuli consist of a **frequency** and an **amplitude**.

Due to the limitations of the Teensy 3.2 analog/digital outputs, one cannot mix analog and digital outputs on the same channel.  Furthermore, a maximum of 254 stimulus trains may be used across all output channels.

TODO: example picture goes here.

## Communicating with a Ticklish device

Ticklish has a simple serial protocol for communications.

All commands start with either `~` or `$`, and all commands end with `\n` (newline).  Ticklish uses the same protocol for replying.

Fixed-length commands start with `~`, while variable-length commands start with `$`; fixed-length answers start with `~` while variable-length answers start with `$`.  Only some commands evoke a reply.  All replies are in direct response to a command--there are no asynchronous/delayed responses.

The symbols ~, $, and `\n` (newline) will ONLY appear at the beginning (`~`, `$`) and/or end (`\n`) of a command.  No escape sequences are permitted; if one of the above characters needs to be returned it will be replaced with underscore `_`.

No command or return value may be longer than 60 bytes in addition to the starting `$` and ending `\n` if any.  Thus, the longest possible message is 62 bytes.  This constraint is to ensure that the command fits safely within one 64-byte USB packet sent/received by the board.

**From this point on, the ending `\n` will be omitted from all command and reply strings listed in this document.  Ensure that your code expects and sends `\n` as appropriate!**

### Identifying a Ticklish device

If you send the command `~?`, a Ticklish device will respond with a string that starts with `$Ticklish2.0 ` (the version number may be later) plus a device-specific identifying string, followed by `\n`.  The string will be at most 60 characters long not counting `$` and `\n`, but may be shorter.

### Output channels

Each digital output channel is identified by a capital letter that refers to its pin number.  Pins 14 through 23 are lettered `A` through `J`; pins 0 through 13 continue with `K` through `X`.  Note that `X` is the Teensy 3.2 LED pin.  Note also that case is important: lower case letters do **not** refer to output channels.

The analog output channel is specified by `Z` and is on pin A14/DAC for a Teensy 3.2; for Teensy 3.5 and 3.6, the channels are `Z` for A21/DAC0 and `Y` for A22/DAC1.

Note that pins do not turn on precisely simultaneously even if scheduled at the same time.  Lower-lettered pins turn on before higher-lettered ones; the latency between each pin state change and the next is at most a few microseconds, but if timing of that precision is important, you should measure it and not take the simultaneity for granted.

### Input channels

Pins `A` through `J` can read analog voltage (0-5V) if not being used for output.  Pins `K` through `W` can read digital state (reported as 0.000 or 5.000 voltage) or can read the timing of pulse trains.  Pins `X`, `Y`, and `Z` cannot be used for reading.

### Durations and Other Numbers

Ticklish measures all times in seconds.  A _duration_ is given by eight characters: either eight decimal digits, or seven decimal digits and a single decimal point `.`.  A leading zero is required for times less than one second, and all values must be padded with zeros to reach eight total characters.  Thus, `0.000001` is the shortest non-zero time possible and `99999999` is the longest (about three years); `1.000000` and `00000001` are two different ways to specify one second.

Note also that the internal clock on the Teensy 3.2 is not accurate to one part in a hundred million (or even one in a million).  Synchronization should not be performed by dead reckoning alone, but by querying the internal clock of the Teensy.  There is also a synchronization-compensation feature.

### Specifying a Stimulus Train

Stimulus trains can be specified either as a single long command or piece by piece, as described below.

#### Specifying Stimulus Train Parameters

All commands to specify a stimulus train start with `~` followed by the output channel: `A` to `X` for digital channels or `Z` for the analog channel.

The third character, a lower case letter, specifies which parameter to set.

Then, any values are given.  Typically, this is an eight-character duration specification.

An example for each parameter is given below:

| Quantity | Char | Value | A/D? | Example | Explanation |
|----------|------|-------|------|---------|-------------|
| total duration | `t` | duration | either  | `~At00000120` | Output `A`'s stimulus train will end after 120 seconds |
| initial delay  | `d` | duration | either  | `~Ad00000030` | Output `A` will wait 30 seconds before doing anything |
| stimulus on    | `s` | duration | either  | `~Ay0000.300` | Each stimulus for `A` will be on for 300 ms |
| stimulus off   | `z` | duration | either  | `~An0005.700` | Then `A` will wait 5700 ms (for a 6 second interval total) |
| pulse on       | `p` | duration | digital | `~Ap0.004500` | Within `A`'s stimulus, turn on the signal for 4.5 ms... |
| pulse off      | `q` | duration | digital | `~Aq0.005500` | ...then off for 5.5 ms (10 ms total, for 30 pulses / stimulus) |
| wave period    | `w` | duration | analog  | `~Zw0.004170` | Analog output for 239.8 Hz (note: minimum period is 1 ms) |
| wave amplitude | `a` | 0000-2047 | analog | `~Za2000`    | Near-maximum amplitude wave.  (Values 0-2047.) |
| upright        | `u` | none     | either  | `~Au`        | Normal sign: digital goes low to high, sine starts up |
| inverted       | `i` | none     | either  | `~Ai`        | Inverted sign: digital goes high to low, sine starts down |
| sinusoidal     | `l` | none     | analog  | `~Zl`        | Analog output will be sinusoidal (overrides `r`) |
| triangular     | `r` | none     | analog  | `~Zr`        | Analog output will be triangular (overrides `l`) |

Note that the analog output only allows an integer number of wave half-periods to be executed within the on time of a stimulus.  If a half-wave would not complete by the time a stimulus was to turn off, that half-wave will be skipped.  This is done to avoid high-frequency artifacts as an output suddenly vanishes.  Note also that analog outputs have a maximum range of 0-3.3V, so "off" will be 1.65V.  If you connect a 10 uF capacitor in-line with the output pin, you should effectively remove the 1.65V offset.  Note also that the maximum current is very low; an amplifier is needed to run a stimulus device.

Note also that very short stimuli may fail to complete as expected.  The software is designed to have a timing accuracy of around 100 microseconds.  Setting a pulse on duration of 5 microseconds is possible, but unlikely to produce the desired output.

#### Complete Stimulus Train Specification

A basic digital stimulus train can be set using the `=` command followed by durations for `t`, `d`, `s`, `z`, `p`, and `q`.  Each duration must be separated by `;`.  Finally, a `u` or `i` is given for usual or inverted polarity.

An annotated example is given below.

```
  /------------------------------------------------------- Train specification command
  |            /------------------------------------------ Initial delay
  |            |                  /----------------------- Stimulus off duration
  |            |                  |                 /----- Pulse off duration
  v         vvvvvvvv          vvvvvvvv          vvvvvvvv
~A=00000120;00000030;000000.3;000005.7;0.004500;0.005500u
 ^ ^^^^^^^^          ^^^^^^^^          ^^^^^^^^         ^- Usual polarity
 |    |                 |                  \-------------- Pulse on duration
 |    |                 \--------------------------------- Stimulus on duration
 |    \--------------------------------------------------- Total stimulus train duration
 \-------------------------------------------------------- Output channel specifier
```

Analog stimuli must be set piece by piece.  Digital stimuli that are set all at once can be modified by further commands.

To immediately run the command on that single channel, discarding all other settings, use `:` in place of `=`.  (This only works when the system is not already running.)

#### Chained Stimulus Trains

Stimulus trains can be chained one after another.  To add a new train after the existing specified one, use `&`, e.g. `~A&`.  All commands regarding this output pin will apply only to the new train after this command executes.  The new train will be initialized with zero values (so you had better set them), and will begin executing as soon as the time on the previous train elapses.

A maximum of 254 trains can be stored across all pins.  Each of the 26 initial pins reserves one train to begin with, leaving 228 free for extensions.

### Error States

The Ticklish state machine contains a single error state.  The machine can enter this state in response to invalid input that is dangerous to ignore: placing an invalid request, trying to specify more states than are allowed, or setting parameters into an already-running protocol.  When in an error state, the system will accept commands but not parse any of them save for `~@` which will return `~!` if there is an error (that command will return `~.` when there is no error and is awaiting commands, `~*` when running, and `~/` when finished running but not reset); for `~#` which will report the error state (as `$error message here\n` where the message hopefully contains some information about what went wrong); and for `~.` which will reset and clear the error state (at which point it can no longer be read out).

### Executing and Querying a Stimulation Protocol

To run a stimulation protocol on all defined channels, send the command `~*`.  To run on only a single defined channel, send the command `~A*` where `A` is the channel letter.  This will also clear the programs on all other pins.

To terminate a stimulation protocol in progress, send the command `~/`.  To terminate a single channel while leaving any others still running, use `~A/`.  Once terminated, a channel cannot be restarted.

To ask the board to tell what time point it is at, send the command `~#`.  If there is no error it will respond with `~12345678.123456` where elapsed duration is specified in seconds plus microseconds; if the board has started and is running it will always report at least one elapsed microsecond.  If the stimulus protocol has not yet been started or has already finished, it will return `~00000000.000000`.  If the system has encountered an error, it will return `$an error message here\n`.

To query the state machine that runs an individual channel, send the command `~A@` for channel `A` (likewise for the others).  The board will respond with `~A`, followed by a number from `'0'` to `'3'` indicating its state (0 = not running, 1 = running but stimulus off, 2 = running and stimulus is on but not in a pulse, 3 = running and stimulus is on and in a pulse).  This is then followed by a `;` and the number of the stimulus train (in three digits), counting up from 0.  Analog stimuli will always report 1 or 3, not 2.

Ticklish will make a best effort to obey all the parameters set for it, but as the code does not form a hard real-time operating system, it may fail to switch at precisely the times requested.  To query an individual channel for error metrics, send the command `~A#` (for channel `A`).  It will respond with 8 numbers (after a `~`):

1. The number of stimuli that should have started (9 digits)
2. The number of stimuli that were missed (6 digits)
3. The number of pulses that should have started (9 digits)
4. The number of pulses that were missed (6 digits)
5. Max error for starting a pulse, in microseconds (5 digits)
6. Max error for ending a pulse, in microseconds (5 digits)
7. Cumulative number of microseconds of error for starting pulses (10 digits)
8. Cumulative number of microseconds of error for ending pulses (10 digits)

Overall, the reply is 61 bytes long.  There are no separators between the characters.

This data will be preserved after the run is complete.  Since this reporting is expensive and may itself induce timing errors, it is recommended to only call this between stimuli or during debugging.

### Resetting

After a run is complete, the previous program remains intact.  Before setting parameters or running again, the program needs to be cleared or reset.

To clear all stimuli, use `~.`.  To refresh stimuli (to run the same protocol again), use `~"`.  One cannot run a protocol or modify it after a run is complete unless it is either cleared or refreshed.  A refreshed protocol can be modified (but only the last stimulus train(s) are available).

### Drift

You can get timing information during a run with the `#` command.  This can be used to measure drift between the Teensy board clock and the clock of the communicating computer.  To request that the Teensy apply a drift to its own clock, calculate teensy_reported_interval/(locally_measured_interval - teensy_reported_interval), round it to an 8 digit number padded by zeros, and send it with the `^` command as follows:

```
~^+12345678.
```

Use `+` and `-` to indicate sign (symbol is mandatory).  All zeros indicates no drift correction. Use a trailing `!` instead of `.` to write to EEPROM so the same delay will be used next time.  Use `~^?` to query the existing drift.  Use `~^^` to read the previously-saved EEPROM value instead of using the one passed in.

The board replies with the previous delay value using the same format, except it uses `.` at the end if the new value was not written and `!` if it was.

Note that times will be reported as corrected by the drift factor, so you cannot directly compute a new drift correction when an old one is in place.

Drift can be set at any time except in an error state, and will take effect immediately.

### Analog and digital input

#### Instantaneous measurement

An analog-capable pin (`A` through `J`) will report its voltage if queried with `~A?`.  The return string will be `~D.DDD` (a leading decimal, a decimal point, and three more decimal digits) specifying the voltage.  If it is a digital pin, the values will be either `0.000` or `5.000`; analog pins will be graded.

Making an instantaneous measurement is supported at any time (except when in an error state), but cannot be made on a pin running a stimulus protocol.

#### Digital pulse input measurement

Digital pins `K` through `W` can be used--at most one at a time--to read digital pulse train input.  For now, only the DHT22 temperature/humidity sensor format is enabled.  Execute the command `~K|<` to turn on digital input on pin `K` for a particular stimulus protocol.  Then, instead of actually delivering a stimulus, the board will let the pin float as input most of the time; whenever the pin would normally be turned on, it will instead run a trigger and read stimulus, after which the digital pulse train will be stored in a buffer.

To retrieve the buffer, use `~K|?`.  Ticklish will respond with `$K|0123456789` where the numbers are hex-encoded bits, least signficant bit first.  If the measurement has not been made, the reply will be `$K|`.

## Visual feedback

The Teensy board contains an on-board LED which will blink to report on its status.

If the device is waiting to receive commands to set up stimuli and/or start running, the LED will flash briefly once a second.

If the device has encountered an error while running, the LED will flash quickly (five times a second, and brighter than when waiting).

While a stimulus protocol is running, the LED will be off by default.  If there is a protocol running on that output and there is no error, the LED will do whatever the protocol tells it to do.

When a stimulus protocol has finished, the LED will flash briefly once every three seconds.

## Loading the Ticklish program onto a Teensy 3.2 or later board

If you use the Arduino IDE with the standard loader, you should be able to simply run the IDE, compile with control-R, and press the button on the Teensy to load the program.  If the board is running at a rate other than 72 MHz, you will need to redefine the `MHZ` and `HTZ` defines.

## Implementation Details

The code running on the Teensy is a not-very-straightforward state machine to run the digital outputs plus interrupts as needed to run the analog output.  Presently, reading the source code (in the `ticklish` directory) is the best way to learn about the functioning of the state machine.

## Complete Ticklish Command Reference

### Handy Mnemonics

Controlling: `*` means run; `/` means abort.  `.` means clear, `"` means redo.

Feedback: `@` checks the run level; `#` gets details; `?` gets messages.

Channels: `=` sets everything.  `:` sets and runs everything.  `&` starts a new thing.

Sensors: `?` reads voltage (0.000 / 5.000 if digital pin, otherwise in 0-3.3 range)

### Setting Identity (do this first, but only once!)

You can give the board an identifying string of up to 48 characters in length.  This
should not be done more than necessary, as eventually the board's EEPROM will wear out.

The string is

```
$IDENTITYyour string here\n
```

Note that there is no space after `IDENTITY` and the string.  When returned, `IDENTITY` will be
replaced by `Ticklish1.0 ` (with a space); see the `~?` command.

### Channel-Independent Commands

*The command string starts with `~` and is followed by a single command char.*

#### No parameters

|  Command  | Char| Result?     | Additional Description |
|-----------|-----|-------------|------------------------|
| Run       | `*` | None        | Starts all protocols running.  (Error if already running.) |
| Abort     | `/` | None        | Stops any running protocol. |
| Clear     | `.` | None        | Clears errors & protocols. |
| Refresh   | `"` | None        | Restores protocols from prior run to use again. |
| State?    | `@` | 2 chars     | `~*` if running, `~/` if stopped, `~.` if ready, `~!` if error |
| Report    | `#` | 16 chars    | `$01234567.654321\n` or `$error message\n`; time == 0 if not running. |
| Identity  | `?` | 10-62 chars | `$Ticklish1.0 ` + message + `\n` |
| Ping      | `'` | 2 chars     | `$\n` (empty variable-length reply) |

#### With parameters or multiple command characters

| Command               |Chars | Parameter                   | Result?      | Additional Description |
|-----------------------|-----|-----------------------------|--------------|------------------------|
| Set drift             | `^+` | 8 digits + `.` or `!` | `^+NNNNNNNN` | Sets 1/n drift (too slow); replies with previous |
| Set drift             | `^-` | 8 digits + `.` or `!` | `^+NNNNNNNN` | Sets 1/n drift (too fast); replies with previous |
| Query drift setting   | `^?` | None                  | `^+NNNNNNNN` | Gives current drift value |
| Set drift from EEPROM | `^^` | None                  | `^+NNNNNNNN` | Also returns EEPROM drift value |

### Channel-dependent commands

*The command string starts with `~`, followed by channel number: `A` to `X` are digital channels; `Z` is analog.*

#### No parameters

*The command start character and channel number are followed by a single command character.*

| Command         |Char | Result?  | Additional Description |
|-----------------|-----|----------|------------------------|
| Run alone       | `*` | None     | Runs this output channel protocol alone.  (Must not be running.) |
| Abort run       | `/` | None     | Turns off this output channel.  Remaining protocol (if any) continues. |
| Check state     | `@` | 5 chars  | Run level digit, `;`, three digit train number. See text for details. |
| Check quality   | `#` | 61 chars | 8 decimal numbers. See text for details |
| Usual polarity  | `u` | None     | Stimuli are low-to-high (digital) or waveform is normal (analog). |
| Invert polarity | `i` | None     | Stimuli are high-to low (digital) or waveform is upside-down (analog). |
| Sinusoidal      | `s` | None     | Analog stimulus should be sinusoidal. |
| Triangular      | `r` | None     | Analog stimulus should be triangular. |
| Append train    | `&` | None     | Adds a new stimulus train to follow the existing one. |
| Query voltage   | `?` | 6 chars  | `~` followed by voltage in `D.DDD` format (5 digits) |

#### With parameters

*The command start character and channel number are followed by a command character and a parameter (specified below).*

| Command               |Char | Parameter         | Result? | Additional Description |
|-----------------------|-----|-------------------|---------|------------------------|
| Set total time        | `t` | 8 chars: duration | None    | |
| Set initial delay     | `d` | 8 chars: duration | None    | |
| Set stimulus on time  | `s` | 8 chars: duration | None    | |
| Set stimulus off time | `z` | 8 chars: duration | None    | |
| Set pulse on time     | `p` | 8 chars: duration | None    | Digital channels only. |
| Set pulse off time    | `q` | 8 chars: duration | None    | Digital channels only. |
| Set period            | `w` | 8 chars: duration | None    | Analog channels only.  Minimum period 1 ms. |
| Set amplitude         | `a` | 4 chars: ampl.    | None    | Analog channels only.  Values from 0 to 2047. |
| Set full protocol     | `=` | 54 chars: 6x8 +etc| None    | Digital channels only. |
| Set and run protocol  | `:` | 54 chars: as `=`  | None    | Clears all other protocols. |
| Read digital pulses   | `|` | `<` or `>`        | None    | `<` means actually read them (`>` is output like normal) |
| Query stored pulses   | `|` | `?`               | `$02468ace13` | Bits read as pulses |

### Allowed Commands by State

Possible states:
- `E`: error state
- `C`: completed run state (not reset)
- `P`: programmable state
- `R`: running state

| Command | States Allowed | Behavior when disallowed |
|---------|----------------|-------------------|
|`$IDENTITY...\n` | `P` | error |
| `~*`  | `P`    | error |
| `~/`  | `R`    | ignored |
| `~.`  | `ECPR` | N/A |
| `~"`  | `C`    | error |
| `~@`  | `ECPR` | N/A |
| `~#`  | `ECPR` | N/A |
| `~?`  | `ECPR` | N/A |
| `~'`  | `ECPR` | N/A |
| `~^`  | `CPR`  | N/A |
| `~A*` | `P`    | error |
| `~A/` | `R`    | ignored |
| `~A@` | `CPR`  | N/A |
| `~A#` | `CR`   | returns all zeros |
| `~Au` | `P`    | error |
| `~Ai` | `P`    | error |
| `~Zl` | `P`    | error (including if not `Z` or `Y`) |
| `~Zr` | `P`    | error (including if not `Z` or `Y`) |
| `~A&` | `P`    | error |
| `~At` | `P`    | error |
| `~Ad` | `P`    | error |
| `~As` | `P`    | error |
| `~Az` | `P`    | error |
| `~Ap` | `P`    | error (including if `Z` or `Y`) |
| `~Aq` | `P`    | error (including if `Z` or `Y`) |
| `~Zw` | `P`    | error (including if not `Z` or `Y`) |
| `~Za` | `P`    | error (including if not `Z` or `Y`) |
| `~A=` | `P`    | error |
| `~A:` | `P`    | error (including if `Z` or `Y`) |
| `~A?` | `CPR`  | error if running output on `A`; can't use on `X`, `Y`, `Z` |
| `~K|<`| `P`    | error (including if not `K` through `W`) |
| `~K|>`| `P`    | error (including if not `K` through `W`) |
| `~K|?`| `CR`   | returns empty string |


## Examples

#### Single event protocol

Suppose we wish to have the LED blink three times a second for ten seconds.  We can run the following protocol:

```
~X=10.00000;0.000001;0.033333;0.300000;0.050000;0.050000u
```

Suppose we wish to open a valve for 10s after 1500s.  We can set this protocol to run on the first channel as follows:

```
~A=00001510;00001500;00000010;00000001;00000010;00000001u
```

Note that the stimulus and pulse times are set to be the same for simplicity, and the off times for each do not really matter as the entire protocol will end then.

#### Multi-train protocol

Suppose we wish to have a digital stimulus that consists of a single pulse of 6 ms.  Suppose further than we wish to wait for 300 seconds, then deliver 50 of these stimuli at 20 second intervals, followed by a 120 second delay and a single test pulse, followed by another 180 second delay and another test pulse.  We can instruct the board to run this protocol on the first output (pin 14) as follows:

```
~A=00001290;00000300;00.00600;19.99400;0.006000;0.000001u
~A&
~A=00000120;00000110;00.00600;19.99400;0.006000;0.000001u
~A&
~A=0170.006;0170.000;00.00600;19.99400;0.006000;0.000001u
```

There are a few things to note about this protocol.  First, some dead time is included after the last stimulus in each case: 10 seconds for the first and second trains.  Second, the up/down time is kept at 6 ms / (20s - 6ms) for the single test pulses just to be consistent (it doesn't matter since the protocol ends before the downtime is complete).  Third, since only a single pulse is desired, the pulse up time is set at least as long as the stimulus on time, which gives just a single pulse.  It would also work to exaggerate the length of the stimulus on time, and have the pulse on/off cycle long enough so there wasn't a second pulse.  For example,

```
~A=00000120;00000110;01.00000;19.00000;0.006000;1.994000u
```

would work just as well to give a single 6 ms pulse (because the pulse up time of 1000s expires before pulse time of 6 ms + pulse down time of 1994 ms).

## Operational Details

### Digital Pulse Timing

In the absense of analog output, processing time (assuming the default clock rate of 72 MHz) is about 6 microseconds per channel.  This governs both minimum pulse width and maximum synchrony.  Two stimuli that are scheduled for identical times will be triggered in order of processing, which goes pin by pin from `A` to `X`.  If a pin is not used, it requires about 0.25 microseconds to skip it; if it is used, it requires about 6 microseconds to process.  Thus, outputs that should be tightly synchronized should be on adjacent pins, but if only two outputs are used, it doesn't matter very much.  (Adjacent pins: 6 us, maximally separated pins (A to X), 10 us.)

Beyond the pin-to-pin delay, jitter is possible.  A maximum of about 2 microseconds has been observed.

Non-synchronous inputs are have additional computational delays as the board checks for input and updates global counters.  Sequential events can be spaced no more closely than about 16 microseconds apart (across all pins); there may be jitter in this timing of up to about 7.5 microseconds.

Note that because the board is globally clocked, this jitter does not propagate.  Thus, a single channel 20 kHz square wave is maintained perfectly on average, but has about 10% jitter in the signal (18-22 kHz).

Timing of stimulus train switches has not yet been measured.

## Revision Notes

Ticklish currently version 2.0.

Version 1 had a slightly different control format (`\n` was not used after fixed-length commands) and somewhat fewer features.
