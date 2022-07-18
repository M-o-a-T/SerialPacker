try:
    import utime as time
except ImportError:
    import time

    def ticks_ms():
        return time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW) / 1000000
    def ticks_diff(a,b):
        return a-b
else:
    ticks_ms = time.ticks_ms
    ticks_diff = time.ticks_diff

class CRC16:
    # polynomial: 0xBAAD
    # 
    # cd moat-bus/python/moatbus
    # python3 ./crc.py -b16 -p0xBAAD -d4
    len = 2

    table = [
        0x0000, 0x64a8, 0xc950, 0xadf8,
        0xe7fb, 0x8353, 0x2eab, 0x4a03,
        0xbaad, 0xde05, 0x73fd, 0x1755,
        0x5d56, 0x39fe, 0x9406, 0xf0ae,
    ]
    def __init__(self):
        self.crc = 0

    def feed(self, byte):
        crc = self.crc
        crc = self.table[((byte>>4) ^ crc) & 0xF] ^ (crc>>4);
        crc = self.table[(byte ^ crc) & 0xF] ^ (crc>>4);
        self.crc = crc

class SerialPacker:
    err_crc = 0  # CRC wrong
    err_frame = 0  # length wrong or timeout

    def __init__(self, max_idle=100, max_packet=127, frame_start=0x85, crc=CRC16):
        self.max_packet = max_packet
        self.max_idle = max_idle
        self.frame_start = frame_start
        self._crc = crc
        self.reset()

    def reset(self):
        self.crc = self._crc()
        self.last = ticks_ms()
        self.buf = None
        self.state = 1 if self.frame_start is None else 0
        self.pos = 0

    def is_idle(self):
        if self.s == 0:
            return True
        t=ticks_ms()
        if ticks_diff(t,self.last) < self.max_idle:
            return False
        self.err_frame += 1
        self.reset()
        return True

    def wakeup(self):
        self.reset()
        self.s = 1

    def feed(self,byte):
        # returns a packet if one has been completed
        # returns a byte if it's not part of a packet
        t=ticks_ms()
        if ticks_diff(t,self.last) >= self.max_idle:
            self.err_frame += 1
            self.reset()
        self.last = t

        s=self.state
        if s==0: # idle
            if self.pos > 0:
                self.pos -= 1
                return byte
            elif byte == self.frame_start:
                s=1
            else:
                if byte >= 0xC0:
                    # UTF-8 lead-in character. Skip 0x10xxxxxx bytes so
                    # an accidental start byte is not misrecognized.
                    c ^= 0xff
                    n = 0
                    while c:
                        n += 1
                        c >>= 1
                    self.pos = 6-n
                return byte

        elif s == 1: # len1
            if byte == 0: # zero length = error (break?)
                self.err_frame += 1
                self.reset()
                return;
            self.len = byte
            if self.max_packet>255 and (byte&0x80): # possibly a two byte packet
                s = 2
            elif byte > self.max_packet:
                self.err_frame += 1
                self.reset()
                return
            else:
                s = 3
                self.buf=bytearray(self.len)
                self.pos=0

        elif s == 2: # len2
            self.len = (self.len&0x7f) | (byte<<7)
            if self.len > self.max_packet:
                self.err_frame += 1
                self.reset()
                return

            s = 3
            self.buf=bytearray(self.len)
            self.pos=0

        elif s == 3:
            self.crc.feed(byte)
            self.buf[self.pos] = byte
            self.pos += 1
            if self.pos == self.len:
                s = 4
                self.pos = self.crc.len
                self.crc_in = 0

        elif s == 4: # CRC
            self.crc_in = (self.crc_in<<8) | byte
            self.pos -= 1
            if self.pos == 0:
                if self.crc.crc != self.crc_in: # error
                    self.err_crc += 1
                    rbuf = None
                else:
                    rbuf = self.buf
                self.reset()
                return rbuf

        self.state=s

    def frame(self,data):
        # return head and tail data for the packet
        ld = len(data)
        if ld > self.max_packet:
            raise ValueError(f"max len {self.max_packet}")
        crc = self._crc()
        for b in data:
            crc.feed(b)
        h = b"" if self.frame_start is None else bytes((self.frame_start,))
        if self.max_packet>255 and ld>127:
            h += bytes(((ld&0x7f)|0x80,ld>>7))
        else:
            h += bytes((ld,))
        t = bytes((crc.crc>>8, crc.crc&0xFF))
        return h,t

