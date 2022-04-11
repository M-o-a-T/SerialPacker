# SerialPacker

This is an Arduino library for sending and receiving variable-sized,
CRC16-protected data packets.

It is optimized for daisy-chaining multiple
modules onto an ad-hoc bus, i.e. each member of the chain immediately
forwards the data to the next device without actually storing it. This
reduces RAM usage and speeds up processing.

Originally based on code by Stuart Pittaway.

## Usage

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

Include `<SerialPacker.h>`.

Create a SerialPacker instance, called SP in the rest of this document.

Set up your serial (or other) data stream.

Call `SP.begin(stream, header_handler, read_handler, packet_handler,
recv_buffer, recv_size, header_size)`, where

* `stream` is obviously your data stream,

* `header_handler` is a function that's called when `header_size` bytes
  have been read.

* `read_handler` is a function that's called after additional bytes
  have been read after your header handler calls `sendDefer`.

* `packet_handler` is a function that's called when a packet has been
  received (with correct CRC),

* `recv_buffer` is a buffer of size `recv_size`. Bytes beyond this size
  are not stored, but possibly forwarded.

Periodically call `SP.checkInputStream()`.

### Sending packet data

Call `sendStartFrame(SB_SIZE_T length)` to start transmitting a
packet.

Call `sendByte(uint8_t data)` to send a single byte, or `sendBuffer(const
uint8_t *buffer, SB_SIZE_T length)` to send multiple bytes.

Call `sendEndFrame(bool broken=false)` to transmit the CRC. If `broken` is
set, the CRC is intentionally mangled so that the next receiver will not
treat the packet as valid.

Sending more than the indicated number of bytes is not possible; they are
silently ignored.

### Receiving packet data

Periodically call `SP.checkInputStream()`.

Your `onPacket` handler is called when a message is complete. If it is
longer than `bufferSize`, data exceeding this buffer have been discarded.
If you called `sendCopy` earlier, you need to call `sendEndFrame()` here.

### Replying / Forwarding packet data

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

## Error handling

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

# Usage example

Check [this fork of the diyBMS code](https://github.com/M-o-a-T/diyBMS-code).
