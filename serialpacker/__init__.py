"""
This module implements a simple framing scheme for serially-transmitted
data. Frames use an optional fixed start byte, a intra-frame time maximum
to protect against data loss, and a CRC16 (polynomial 0xBAAD, optimal for
small-ish frames with a large number of bit errors).

The length is transmitted as one or two characters, depending on its
maximum, in order to keep packets small yet not restrict size too much.

A C implementation of this scheme is also available. The C version supports
streaming; this Python implementation does not.
"""
from __future__ import annotations

__all__ = ["FRAME_START", "SerialPacker", "CRC16"]

try:
    import utime as time
except ImportError:
    # CPython
    import time

    def ticks_ms():
        """System ticks, in msec"""
        return time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW) / 1000000

    def ticks_diff(a, b):
        """Difference between ticks, non-wrapping"""
        return a - b

else:
    # MicroPython
    ticks_ms = time.ticks_ms
    ticks_diff = time.ticks_diff

FRAME_START = 0x85


class CRC16:
    """
    A simple CRC routine.

    Polynomial: 0xBAAD, no inversion or similar tricks
    """

    # cd moat-bus/python/moatbus
    # python3 ./crc.py -b16 -p0xBAAD -d4
    len = 2

    # Use a nibble-based table; it's fast enough, but doesn't need a
    # kbyte of memory (MicroPython doesn't have arrays).
    table = (
        0x0000,
        0x64A8,
        0xC950,
        0xADF8,
        0xE7FB,
        0x8353,
        0x2EAB,
        0x4A03,
        0xBAAD,
        0xDE05,
        0x73FD,
        0x1755,
        0x5D56,
        0x39FE,
        0x9406,
        0xF0AE,
    )

    def __init__(self):
        self.crc = 0

    def feed(self, byte):
        """add a byte to the CRC"""
        crc = self.crc
        crc = self.table[((byte >> 4) ^ crc) & 0xF] ^ (crc >> 4)
        crc = self.table[(byte ^ crc) & 0xF] ^ (crc >> 4)
        self.crc = crc


class SerialPacker:
    """
    Frame sender and receiver.

    Args:
      max_idle:
        max milliseconds between bytes within a frame.
      max_packet:
        maximum packet size. Size is always transmitted as a single byte if
        max_packet is <256, otherwise two bytes are used whenever the actual
        size is >127.
      frame_start: start byte, or None for solely relying on inter-frame timeout.
      crc:
        CRC method to use. The default is our CRC16 with 0xBAAD polynomial.
    """

    # error counters
    err_crc = 0  # CRC wrong
    err_frame = 0  # length wrong or timeout

    def __init__(
        self,
        max_idle=100,
        max_packet=127,
        frame_start=FRAME_START,
        crc=CRC16,
        mark=None,
    ):
        self.max_packet = max_packet
        self.max_idle = max_idle
        self.frame_start = frame_start
        self.mark = mark
        self.mark_seen = False
        self._crc = crc
        self.reset()

    def reset(self):
        """Reset the packet receiver."""
        self.crc = self._crc()
        self.last = ticks_ms()
        self.buf = None
        self.state = 1 if self.frame_start is None else 0
        self.pos = 0
        # intentionally does not clear nbuf

    def is_idle(self):
        """
        Test whether we're currently receiving a frame.
        """
        if self.s == 0:
            return True
        t = ticks_ms()
        if ticks_diff(t, self.last) < self.max_idle:
            return False
        self.err_frame += 1
        self.reset()
        return True

    def wakeup(self):
        """
        Utility method to start a frame without a start byte. Useful if the
        first byte is used as a wake-from-sleep signal.
        """
        self.reset()
        self.s = 1

    def feed(self, byte):
        """
        Receiver for a single byte.

        Returns: a packet, if this byte completed its reception.
        """
        t = ticks_ms()
        if ticks_diff(t, self.last) >= self.max_idle:
            self.err_frame += 1
            self.reset()
        self.last = t

        s = self.state

        if self.mark is None:
            pass
        elif self.mark_seen:
            self.mark_seen = False
        elif byte == self.mark:
            self.mark_seen = True
            return None
        else:
            return byte

        if s == 0:  # idle
            if self.pos > 0:
                self.pos -= 1
                return byte
            elif byte == self.frame_start:
                s = 1
            else:
                if byte >= 0xC0:
                    # UTF-8 lead-in character. Skip 0x10xxxxxx bytes so
                    # an accidental start byte is not misrecognized.
                    c = byte ^ 0xFF
                    n = 0
                    while c:
                        n += 1
                        c >>= 1
                    self.pos = 6 - n
                return byte

        elif s == 1:  # len1
            if byte == 0:  # zero length = error (break?)
                self.err_frame += 1
                self.reset()
                return None
            self.len = byte
            if self.max_packet > 255 and (byte & 0x80):  # possibly a two byte packet
                s = 2
            elif byte > self.max_packet:
                self.err_frame += 1
                self.reset()
                return None
            else:
                s = 3
                self.buf = bytearray(self.len)
                self.pos = 0

        elif s == 2:  # len2
            self.len = (self.len & 0x7F) | (byte << 7)
            if self.len > self.max_packet:
                self.err_frame += 1
                self.reset()
                return None

            s = 3
            self.buf = bytearray(self.len)
            self.pos = 0

        elif s == 3:
            self.crc.feed(byte)
            self.buf[self.pos] = byte
            self.pos += 1
            if self.pos == self.len:
                s = 4
                self.pos = self.crc.len
                self.crc_in = 0

        elif s == 4:  # CRC
            self.crc_in = (self.crc_in << 8) | byte
            self.pos -= 1
            if self.pos == 0:
                if self.crc.crc != self.crc_in:  # error
                    self.err_crc += 1
                    rbuf = None
                else:
                    rbuf = self.buf
                self.reset()
                return rbuf

        self.state = s
        return None

    def frame(self, data):
        """
        Build framing for a message.

        Returns:
            a triple with head (start byte, length), bytes (possibly
            mark-ed), and tail (CRC) data.
        """
        ld = len(data)
        if ld > self.max_packet:
            raise ValueError(f"max len {self.max_packet}")
        crc = self._crc()
        for b in data:
            crc.feed(b)
        h = bytearray()
        if self.frame_start is not None:
            h.append(self.frame_start)
        if self.max_packet > 255 and ld > 127:
            h.extend(((ld & 0x7F) | 0x80, ld >> 7))
        else:
            h.append(ld)
        t = bytes((crc.crc >> 8, crc.crc & 0xFF))

        if self.mark is not None:

            def itermark(data):
                for b in data:
                    yield self.mark
                    yield b

            h = bytes(itermark(h))
            data = bytes(itermark(data))
            t = bytes(itermark(t))
        return h, data, t
