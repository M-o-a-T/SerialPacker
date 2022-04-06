# SerialPackets

This is an Arduino library for sending and receiving fixed-size,
CRC16-protected data packets.

Crucially for microcontroller applications, this library supports
forwarding and extending a packet without actually storing it.

It's a good idea to extend the packet at the end (this way requires
smaller serial buffers), but adding data to its start is possible.

Originally based on code by Stuart Pittaway.

## Usage

Define FRAMESTART to a byte that shall introduce a new packet.
Until that byte is seen, any other bytes will be ignored.
A good start value is 0x85 (some alternating bits, not an ASCII or UTF-8
character). If FRAMESTART is not defined, any leading character is
interpreted as a length byte.

Define MAX_PACKET as the maximum possible packet size. You do not need to
reserve space for the whole packet. If this value is >255, packet lengths
greater than 127 are encoded in two bytes. The default is 127 for maximum
compatibility.

Define MAX_FRAME_DELAY as the number of milliseconds between successive
serial characters. A delay longer than this indicates the start of a new
frame. The default is 100.

Include `<SerialPacker.h>`.

Create a SerialPacker instance, called SP in the rest of this document.

Set up your serial (or other) data stream.

Call `SP.begin(stream, header_handler, packet_handler, recv_buffer,
recv_size, header_size)`, where

* `stream` is obviously your data stream,
* `header_handler` is a function that's called when `header_size` bytes
  have been read,
* `packet_handler` is a function that's called when a packet has been
  received (with correct CRC),
* `recv_buffer` is a buffer of size `recv_size`. Bytes beyond this size
  are not stored, but possibly forwarded.

Periodically call `SP.checkInputStream()`.

### Sending packet data

Call `sendHeader(SB_SIZE_T length)` to start transmitting a
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

From within your `onHeader` handler, you may call `sendCopy(SB_SIZE_T
addLength)`. This copies the header bytes (feel free to modify them). You
need to send exactly `addLength` bytes, either immediately or in your
`onPacket` handler.

Your `onPacket` handler is called when a message is complete. If it is
longer than `bufferSize`, data exceeding this buffer have been discarded.
If you called `sendCopy` earlier, you need to call `sendEndFrame()` here.


