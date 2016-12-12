# C interface to Ticklish

The files here contain a minimal C interface to the Ticklish stimulus delivery
software for the Teensy 3 series of boards.  You can also use this interface
from languages with a C FFI (foreign function interface).

## Requirements

Serial port communication is performed with `libserialport`.  You are responsible for
downloading it yourself one way or another; be aware that libserialport is under the
LGPL license so if you modify it, those modifications must be shared with any software
you distribute.  libserialport is not distributed with this package to ensure that it
remains unmodified and called only as a library.  Version 0.1.1+ is
required.

To compile under Windows...actually, I haven't figured out how to do this yet. 

You'll need the `pthread` library available.

## Usage

You'll have to read the example.  Feel free to modify it!

## Timing and Threading

A best effort has been made to keep the interface efficient.  However, no attempt
has been made to accommodate threading aside from guarding internal state
changes with a pthread mutex.  The code can easily be altered to work in a threaded context
(by creating threads, calling a thread-sleep at key points, and perhaps
dropping in a few more synchronization primitives), but this is left to you.
