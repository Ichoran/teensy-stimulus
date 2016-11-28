# C interface to Ticklish

The files here contain a minimal C interface to the Ticklish stimulus delivery
software for the Teensy 3 series of boards.  You can also use this interface
from languages with a C FFI (foreign function interface).

## Requirements

Serial port communication is performed with libserialport.  You are responsible for
downloading it yourself one way or another; be aware that libserialport is under the
LGPL license so if you modify it, those modifications must be shared with any software
you distribute.  libserialport is not distributed with this package to ensure that it
remains unmodified and called only as a library.

To compile under Windows, MinGW must be installed. 

## Usage

The source here is intended to be used as a template, not as a module to
be called.

## Timing and Threading

A best effort has been made to keep the interface efficient.  However, no attempt
has been made to accommodate threading.  This keeps communications fast, but they
may be inefficient.  The code can easily be altered to work in a threaded context
(by dropping in a few synchronization primitives and calling a thread-sleep at key
points), but this is left to you.
