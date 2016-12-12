# LabView interface to Ticklish

The Virtual Instrument here contains a minimal interface to the Ticklish
system running on a Teensy board.  Read the Test VI to get some idea how
to use it.  All the other VIs can be used as sub-VIs.

## Requirements

You must have LabView 2016.

## Usage

The source here is intended to be used as a template as much as a
module to be called.  The VIs have a number of design patterns that
are poor and are used either from inexperience or to safeguard against
potential problems that likely don't exist (but it was faster to
code against them than to check whether or not they're a problem).
