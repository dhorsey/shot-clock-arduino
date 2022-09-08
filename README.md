This is a program for a shot clock for sports, to run on an Arduino Nano.  The hardware connected
consists of (2) WS2818 LED strings used to display the front two digits on the clock, and a TM1637
4-digit display for the back.  It supports nRF240L01 radios to communicate with a second clock.
There are six physical buttons for the clock: start, stop, reset to 30 seconds, reset to a custom amount,
increase, decrease, and settings.  There is a horn output to activate when the clock runs out.

Listening on the serial port is a simplified Forth command processor, used for development and
testing of software and hardware.

This is mostly presented here as a code sample, although it is under the MIT license if anyone
finds bits of it useful.
