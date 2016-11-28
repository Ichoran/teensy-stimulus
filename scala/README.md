# Scala interface to Teensy-Stimulus

The files here contain a minimal interface to the Teensy Stimulus board
usable from Scala.  Interfacing with Java is similar, if more wordy.

## Requirements

You must have Java 1.8 and SBT installed.  SBT will take care of handling
dependencies (which are Scala 2.12 and JSSC 2.8).

## Usage

The source here is intended to be used as a template, not as a module to
be called.

There are a few utility methods provided in the `TeensyUtil` object for
encoding and decoding requests and responses.

## Timing and Threading

Applications that run on the JVM have difficulty maintining tight timing
guarantees due to garbage collection.  Also, external resources like serial
ports are not always cleaned up in cases of exceptional failure.  Thus,
it is generally unwise to use this directly in a large heavily
memory-impacted application (run it as a separate process!), or when extreme
reliability is essential.

Nonetheless, for lightweight use, the Scala interface is very easy.
