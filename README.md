# teensy-stimulus

Teensy Stimulus allows Teensy 3.1 boards to provide stimulus trains suitable for animal behavior experiments.

## Overview

Teensy Stimulus allows a Teensy 3.1 or 3.2 board to function as a simple stimulus generator.  It listens for commands via serial-over-USB, then executes them.  It has a minimum time resolution of 0.1 milliseconds, a maximum protocol length of 100,000 seconds, and can run up to 25 digital output channels and one analog output channel (for sinewaves or triangle waves of frequencies up to 1 KHz).

## Constructing Stimuli

A stimulus protocol consists of one or more **stimulus trains** which execute sequentially.  Each train consists of an **initial delay** followed by one or more repeats of a timed **stimulus on** phase followed by a timed **stimulus off**.  The pattern repeats until the total **duration** of the stimulus train has elapsed (counting the initial delay).

Digital stimuli consist of repeated **pulse on** and **pulse off** blocks.  Analog stimuli consist of a **frequency** and a **shape**.

Due to the limitations of the Teensy 3.1 analog/digital outputs, one cannot mix analog and digital outputs on the same channel.  Furthermore, a maximum of 254 stimulus trains may be used across all output channels.

TODO: example picture goes here.

## Communicating with a Teensy Stimulus device

Teensy Stimulus has a simple serial protocol for communications.

All commands start with the symbol `~`.  Variable-length commands end with the symbol `$`, but most commands are fixed-length.

All returned values start with the symbol `~`.  Variable-length return values end with the symbol `$`, but most return values are fixed-length.

No command or return value may be longer than 60 bytes.

### Output channels

Digital output channels are specified by letter, starting with `A`.  Analog output channels are specified by number, starting with `0`.

### Numbers and Durations

Teensy Stimulus understands durations given in either decimal numbers or numbers packed into 6-bit chunks (base 64).  If the digits `0` through `9` are used, this is interpreted as a decimal number of milliseconds, up to 8 digits long and terminated with `$`.  If the ASCII digits `;` (number 59, hex 0x3B) through `z` (number 122, hex 0x7A) are used, this must be 5 characters long and is interpreted as a base 64 number with the value of the digit equal to the ASCII number minus 59 (i.e. `c - ';'` where `c` is the character), expressed in tenths of milliseconds (hundreds of microseconds).  Values over 1,000,000,000 (i.e. `vagc;`) are not valid, even though `zzzzz` = 1,073,741,823 can formally be represented.

One exception is the specifying the period of analog waves.  Here, the period is given in tens of microseconds (decimal format) or microseconds (base 64 format).  The minimum period is `100$` or `;;;Jc`.

Other numbers use either a decimal text format or the same base 64 encoding.

### Specifying a Stimulus Train

Stimulus trains can be specified either as a single long command or piece by piece.

#### Piecewise Stimulus Train Specification

The various parameters that describe a stimulus train can be set by the commands below.  No reply is given in response.  In the descriptions below, `@` stands in for the channel (`A` to `Y` for digital, `0` for analog).

In every case the format is `~@%<duration>`, where `%` stands in for a command character given in the table below, and `<duration>` is a number in either decimal or base64 format as described above.

| Quantity | Command Char | Analog/Digital? | Example | Explanation |
|----------|--------------|---| ---------|-------------|
| total duration | `D` | A,D |`~AD60000$` | Output `A`'s stimulus train will end after 60 seconds |
| initial delay | `i` | A,D | `~Ai30000$` | Output `A` will wait 30 seconds before doing anything |
| stimulus on | `+` | A,D | `~A+300$` | Each stimulus for `A` will be on for 300 ms |
| stimulus off | `-` | A,D | `~A-5700$` | Then `A` will wait 5700 ms (for a 6 second interval total) |
| pulse on | `^` | D only | `~A^5$` | Within `A`'s stimulus, turn on the signal for 5 ms... |
| pulse off | `_` | D only | `~A_5$` | ...then off for 5 ms (10 ms total, for 30 pulses / stimulus) |
| shape? | `%` | D only | `~A%0` | Stimulus is low until set high.  (Use `1` for the opposite.) |
| wave period | `*` | A only | `!0*417$` | Analog output `0` set for 239.8 Hz (note: period in 10s of us) |
| shape? | `%` | A only | `~0%0` | Sine wave.  (Use `1` for triangle.) |

Note that in analog outputs, only an integer number of wave half-periods are executed within a stimulus on time.  If a half-wave would not complete by the time a stimulus was to turn off, that half-wave will be skipped.  This is done to avoid high-frequency artifacts as an output suddenly vanishes.

#### Complete Stimulus Train Specification

All aspects of a stimulus train can be set with a single command.  In this case, only the base 64 number format can be used.  The format is explained below.

```
  /-------------------------------- Complete train specification command
  |        /----------------------- Initial delay
  |       |            /----------- Stimulus off duration
  v     vvvvv     vvvvv     vvvvv-- Pulse off duration (ignored for analog)
~A=;=MZ;;<DJ[;;;is;;Huc;;;;m;;;;m0
 ^ ^^^^^     ^^^^^     ^^^^^     ^- Shape (stimulus low by default)
 |   |         |            \------ Pulse on duration (wave period for analog)
 |   |          \------------------ Stimulus on duration
 |   \----------------------------- Total stimulus train duration
 \--------------------------------- Output channel specifier (first digital) 
```

If you wish to have a channel run multiple stimulus trains in succession, you can use the `&` command instead to add additional stimulus trains after the first.  If no initial train has been specified, this will succeed anyway and set the first stimulus train.

```
~A&;=MZ;;<u?[;;;is;;Huc;;;;m;;;;m0
```

For digital outputs, the shape must agree in every case.  If it does not, the first shape will be used. For analog outputs, the shape can change each time.

Finally, if you wish to set a single channel and run it at once, use the `!` command:

```
~A!;=MZ;;<DJ[;;;is;;Huc;;;;m;;;;m0
```

If a protocol is already running, it will be terminated and this one will start running instead.

### Error States

The Teensy Stimulus state machine contains a single error state.  The machine can enter this state in response to invalid input that is dangerous to ignore: requesting a channel that is not there, trying to specify more states than are allowed, or setting parameters one by one into an already-running protocol.  When in an error state, the system will accept commands but not parse any of them save for `~?` which will report the error state (as `~!abcd` where `abcd` contains some information about what went wrong), and for `~.` which will reset the state.  The error can still be read out until some other command is executed, at which point the error information will be cleared.

### Executing and Querying a Stimulation Protocol

To run a stimulation protocol on all defined channels, send the command `~!`.  To run on only a single defined channel, send the command `~A!`.  More channels may not be started until the protocol on that channel has ended.

To terminate a stimulation protocol in progress, send the command `~.`.  To terminate a single channel while leaving any others still running, use `~A.`.

When a stimulation protocol ends, either because of termination or running out of time, it is marked for deletion but isn't actually deleted until new commands are sent to define stimulus trains.  To recover the previous stimulation protocol, send the command `."`.  This must be sent before any new stimuli are defined; otherwise, all will be cleared.  This must also be done after running with `~A!`.

To ask the board to tell what time point it is at, send the command `~?`.  It will respond with `~<duration>` where duration is specified in tenths of microseconds in base-64 format.  If it has not yet been started or has already finished, it will return `~zzzzz`.  If the system has encountered an error,it will return `~!` followed by four characters that may give some clue as to what went wrong.

To query the state machine that runs an individual channel, send the command `~A?` for channel `A` (likewise for the others).  The board will respond with the protocol it is running, in the same format used to set the channels, followed by four `+` or `-` characters.

The first character indicates whether that channel is running at all (`+` = yes, `-` = no).  The second indicates whether it a stimulus is active or not (`+` = yes, `-` = no).  The third indicates whether a pulse is active within a stimulus (`+` = yes, `-` = no; this is always `+` during a waveform unless part of a final half-wave is being skipped).  The fourth indicates whether there is another stimulus train after this one completes (`+` = yes, `-` = no).  The response is thus 36 characters long: the `~` message start symbol, 6 5-character durations, one character for shape, and 4 `+` or `-` flags.

The Teensy Stimulus board will make a best effort to obey all the parameters set for it, but as the code does not form a hard real-time operating system, it may fail to switch at precisely the times requested.  To query an individual channel for error metrics, send the command `~A#` (for channel `A`).  It will respond with 8 30-bit numbers: number of stimuli that should have started, number that were missed, number of pulses that should have started, number that were missed, max error (in microseconds) for starting a pulse, max error (in microseconds) for ending a pulse, total number of microseconds off for starting pulses, total number off for ending pulses.  For analog outputs, the difference between the target time for changing voltage and the actual time is reported instead of starting edge values, and there are no stats for ending edges.  This data will be preserved after the run is complete.  Since this reporting is expensive and may itself induce timing errors, it is recommended to only call this afterwards or during debugging.

## Loading the Teensy Stimulus program onto a Teensy 3.1 or later board

TODO: write this.

## Implementation Details

TODO: write this.
