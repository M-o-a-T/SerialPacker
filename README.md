# SerialPacker

This is an Arduino and Python library for sending and receiving
variable-sized, CRC16-protected data packets.

It is optimized for daisy-chaining multiple modules onto an ad-hoc bus,
i.e. each member of the chain forwards the data it receives to the next
device as quickly as possible, i.e. without waiting for the end of the
packet and without requiring RAM for the whole thing. This reduces RAM
usage and speeds up processing, particularly if your string is long.

Originally based on code by Stuart Pittaway.

## Usage

### Python

As the Python code is intended for the "master" node, it does not use
callbacks and does not support assembling or forwarding messages on the
fly.

The module exports a `SerialPacker` class which you can instantiate with
the following arguments:

* max\_idle: Milliseconds between intra-packet bytes. Default: 10 ms.

* crc: CRC generator class. Default: CRC16/0xBAAD.

* max\_packet: maximum packet length. Default: 127.

* frame\_start: frame start byte. The default is 0x85. `None` skips
  framing entirely.

#### CRC

The CRC class must be callable with an empty initializer.

Objects have the following attributes:

* feed(byte): add a byte to the CRC.

* crc: the accumulated CRC.

* len: the number of bytes to transmit.

#### Sending

The method `frame(data)` returns a head/tail tuple. Transmit the head
bytes, the data, and the tail bytes.

#### Receiving

Send all incoming bytes to `feed(byte)`. This method returns a complete
message when it is complete and its CRC matches. Otherwise, if the byte is
not part of a message, it is returned as-is.

#### Console data

If you set `frame_start` to a non-`None` value, incoming non-frame data
are accumulated in an internal buffer. You should periodically call
`read()` to zero and return it.

If the buffer ends with an incomplete UTF-8 character, it will not be
returned until `max_idle` milliseconds have passed since the last call to
`feed`.

### Arduino / C++

Define `SP_FRAME_START` to a byte that shall introduce a new packet.
Until that byte is seen, any other data will be ignored.
The default value is 0x85 (contains some alternating bits, is not a valid
ASCII or UTF-8 character). If `SP_FRAME_START` is defined to `-1`, any
character may start a message.

Define `SP_MAX_PACKET` as the maximum possible packet size. You do not need to
reserve space for the whole packet. If this value is >255, packet lengths
greater than 127 are encoded in two bytes. The default is 127 for maximum
compatibility.

Define `SP_MAX_FRAME_DELAY` as the number of milliseconds between successive
serial characters. A delay longer than this indicates the start of a new
frame. The default is 100.

Define `SP_SENDLEN` if you want to track the length of the transmitted
data. This adds code to pad a transmitted message automatically when you
abort due to an error, and prevents you from adding more data than you
should.

Define `SP_MARK` to a byte that's prefixed to every transmitted byte.
Also, received bytes are only considered part of a packet when they're
prefixed with that byte. This is useful if some embedded thing needs to
print debug information *while* sending a packet. We recommend `0x10`
("DLE").

Define `SP_NONFRAME_STREAM` if you want incoming characters that are not
part of a frame to be forwarded to another port. Obviously, this does not
work if `SP_FRAME_START` is not defined.

Define `SP_TRACE` to a serial stream (like `Serial1`) if you want to log a
mostly-readable hex copy of the data you're receiving.

Define `SP_CRC` to some suitable 16-bit polynomial. The default is 0xBAAD.
If you do use a different polynomial you need to patch the source to
include the appropriate lookup table.

Define `SP_ERRCOUNT` if you want to count receiver errors.

Include `<SerialPacker.h>`.

Create a SerialPacker instance, called SP in the rest of this document.

Set up your serial (or other) data stream.

Call `SP.begin(stream, header_handler, read_handler, packet_handler,
recv_buffer, recv_size, header_size)`, where

* `stream` is your data stream (obviously),

* `header_handler` is a function that's called when `header_size` bytes
  have been read.

* `read_handler` is a function that's called after additional bytes
  have been read after your header handler calls `sendDefer`.

* `packet_handler` is a function that's called when a packet has been
  received (with correct CRC),

* `recv_buffer` is a buffer of size `recv_size`. Bytes beyond this size
  are not stored, but possibly forwarded.

Periodically call `SP.checkInputStream()`.

#### Sending packet data

Call `sendStartFrame(SB_SIZE_T length)` to start transmitting a
packet.

Call `sendByte(uint8_t data)` to send a single byte, or `sendBuffer(const
uint8_t *buffer, SB_SIZE_T length)` to send multiple bytes.

Call `sendEndFrame(bool broken=false)` to transmit the CRC. If `broken` is
set, the CRC is intentionally mangled so that the next receiver will not
treat the packet as valid.

Sending more than the indicated number of bytes is not possible; they are
silently ignored.

#### Receiving packet data

Periodically call `SP.checkInputStream()`.

Your `onPacket` handler is called when a message is complete. If it is
longer than `bufferSize`, data exceeding this buffer have been discarded.
If you called `sendCopy` earlier, you need to call `sendEndFrame()` here.

#### Replying / Forwarding packet data

From within your `onHeader` handler, call `sendCopy(addLength)`. This sends
the header and the rest of the message onwards.

Alternately, call `sendDefer(readLength)`. This reads an additional
`readLength` bytes into the buffer without forwarding them, then calls your
`onRead` hook. You then call `sendCopy` (or another `sendDefer` if it's a
variable-length message) from there.

When the packet is complete, your `onPacket` handler *must* send exactly
`addLength` bytes.

If you decide that the frame should be invalidated, send filler bytes to
fulfill your `addLength` promise, then call `sendEndFrame(true)`. This
transmits an invalid CRC. If you do not do this 

### Error handling

You should defer acting on the message's data until your `onPacket` handler
runs. It will only be called when the message's CRC is correct.

Other than that, this library includes no error handling.

This is intentional, as it is optimized for forwarding messages in
resource-constrained environment where most error handling takes too much
time or space.

If you need to discover where in your chain of modules a packet got lost,
the usual process is to include a sequence counter in your packet header.
Increment your packet-loss counter by the number of missed entries in the
sequence, and add a way to retrieve that counter.

Checking whether `onPacket` is *not* called after `onHeader` indicates a
CRC error. That error however can result from various problems (an actual
transmission error, wrong number of bytes written by *some* module before
the current one, etc.). This library doesn't try to determine which is
which.

The master should wait `SP_MAX_FRAME_DELAY` milliseconds between messages,
plus the time required for transmitting the data added by modules.

### Deep Sleep

In some applications, microcontrollers go to "deep sleep" with disabled
oscillators. However, serial communication is difficult when you don't have
a clock.

In this situation it's helpful to set the frame start to `0xFF` and to
hook an interrupt to the rising edge of the serial line (i.e. the end of
the start bit). The interrupt shall disable itself, re-enable the serial
receiver, and call the `wokeUp()` method.

# Usage example

Check [this fork of the diyBMS code](https://github.com/M-o-a-T/diyBMS-code).
