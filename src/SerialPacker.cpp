// Copyright (c) 2020 Stuart Pittaway
// Copyright (c) 2022 Matthias Urlichs
// https://github.com/M-o-a-T/SerialPacker
//
// GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

#include "SerialPacker.h"

#ifndef SP_CRC
#define SP_CRC 0xBAAD
#endif

// generated by:
// git clone https://github.com/M-o-a-T/moat-bus
// cd moat-bus/python/moatbus
// python3 ./crc.py -b16 -p0xBAAD -d4
//
static const uint16_t crc16_4[] = {
#if SP_CRC==0xBAAD
0x0000, 0x64a8, 0xc950, 0xadf8,
0xe7fb, 0x8353, 0x2eab, 0x4a03,
0xbaad, 0xde05, 0x73fd, 0x1755,
0x5d56, 0x39fe, 0x9406, 0xf0ae,
#else
#error You need to generate a CRC table for this polynomial
#endif
};

uint16_t SerialPacker::crc16_update(uint16_t crc, uint8_t byte)
{
    crc = crc16_4[((byte>>4) ^ crc) & 0xF] ^ (crc>>4);
    crc = crc16_4[(byte ^ crc) & 0xF] ^ (crc>>4);
    return crc;
}

uint16_t SerialPacker::crc16_buffer(uint8_t data[], uint16_t length)      
{ 
    uint16_t crc = 0;
    while(length--)
        crc = crc16_update(crc, *(data++));
    return crc;
}

void SerialPacker::processByte(uint8_t data)
{
    //debugByte(data);

    uint16_t ts = millis(); // ignores the high word
    if(ts-last_ts > SP_MAX_FRAME_DELAY && receiveState != SP_IDLE)
    {
#ifdef SP_TRACE
        SP_TRACE.println(" R");
#endif
#ifdef SP_MARK
        receiveMark = false;
#endif
        receiveState = SP_IDLE;
#ifdef SP_ERRCOUNT
        errTimeout += 1;
#endif
    }
    last_ts = ts;
#ifdef SP_MARK
    if (receiveMark)
        receiveMark = false;
    else if (data == SP_MARK)
        receiveMark = true;
    else {
#ifdef SP_NONFRAME_STREAM
        SP_NONFRAME_STREAM.write(data);
#else
        stream->write(data);
#endif
    }
#endif

    switch(receiveState) {
    case SP_IDLE:
#if SP_FRAME_START >= 0
        if(data != SP_FRAME_START) {
#ifdef SP_NONFRAME_STREAM
#ifdef SP_TRACE
            SP_TRACE.write('?');
            SP_TRACE.print((int)data);
            SP_TRACE.write(' ');
#endif
            SP_NONFRAME_STREAM.write(data);
#endif
            break;
        }
        receiveState = SP_LEN1;
        break;
    case SP_LEN1:
#endif
        if (data == 0x00) {
            // A zero-length packet doesn't make sense; it's most likely a
            // BREAK.
            reset();
            break;
        }
#if SP_MAX_PACKET>255
        receiveLen = data&0x7F;
        if(data & 0x80)
            receiveState = SP_LEN2;
        else
#else
        receiveLen = data;
#endif
            // the next line is conditional on data & 0x80 if SP_MAX_PACKET>255
            receiveState = SP_DATA0;
        receivePos = 0;
        readLen = 0;
        break;
    case SP_LEN2:
        receiveLen |= data<<7;
        receiveState = SP_DATA0;
        break;
    case SP_DATA0:
        if(receiveLen > SP_MAX_PACKET) {
            receiveState = SP_ERROR;
            break;
        }
        receiveCRC = 0;
        receivePos = 0;
        if(receiveLen == 0) {
            receiveState = SP_CRC1;
            if(headerLen == 0 && onHeaderReceived)
                (*onHeaderReceived)();
            break;
        }
        receiveState = SP_DATA;
        // fall thru
    case SP_DATA:
        receiveCRC = crc16_update(receiveCRC, data);
        if(receivePos < receiveBufferLen)
            receiveBuffer[receivePos] = data;
        receivePos += 1;
        if (readLen) {
            readLen--;
            if (readLen == 0) {
                if (onReadReceived != nullptr)
                    (*onReadReceived)();
            }
        }
        else if(copyInput) {
            sendByte(data);
        }
        if(receivePos == receiveLen)
            receiveState = SP_CRC1;
        if(receivePos == headerLen) {
            if (onHeaderReceived != nullptr)
                (*onHeaderReceived)();
        }
        break;
    case SP_CRC1:
        crcHi = data;
        receiveState = SP_CRC2;
        break;
    case SP_CRC2:
        {
            uint16_t crc = ((crcHi<<8) | data);
#ifdef SP_TRACE
            SP_TRACE.print(crc,HEX);
            SP_TRACE.write(' ');
#endif
            if(crc ^ receiveCRC) {
#ifdef SP_TRACE
                SP_TRACE.print(F("-CRC:rem "));
                SP_TRACE.print(crc, HEX);
                SP_TRACE.print(F(" loc "));
                SP_TRACE.println(receiveCRC, HEX);
#endif
#ifdef SP_ERRCOUNT
                if (crc != 1)
                    errCRC += 1;
#endif
                receiveState = SP_IDLE;
                if(copyInput)
                    sendEndFrame(true);
            } else {
#ifdef SP_TRACE
                SP_TRACE.println("+CRC");
#endif
                receiveLen = receivePos;
                if(onPacketReceived != nullptr) {
                    (*onPacketReceived)();
                    if(copyInput)
                        sendEndFrame(false);
                }
                receiveState = SP_IDLE;
            }
        }
        break;
    case SP_DONE:
        break;
    case SP_ERROR:
        break;
    }
}

// Checks stream for new bytes to arrive and processes them as needed
void SerialPacker::checkInputStream()
{
    if (stream == nullptr || receiveBuffer == nullptr)
        return;

    while (stream->available() > 0) {
        uint16_t data = stream->read();
#ifdef SP_TRACE
        SP_TRACE.print(data,HEX);
        SP_TRACE.write(' ');
#endif
        processByte((uint8_t)data);
    }
}

void SerialPacker::sendByte(uint8_t data)
{
    //debugByte(data);
#ifdef SP_SENDLEN
    if(sendPos++ > sendLen)
        return; // oops
#endif
    sendCRC = crc16_update(sendCRC, data);
#ifdef SP_MARK
    stream->write(SP_MARK);
#endif
    stream->write(data);
}

// Sends a buffer (fixed length)
void SerialPacker::sendBuffer(const void *buffer, SB_SIZE_T len)
{
    const uint8_t *buf = (const uint8_t *)buffer;
    for (SB_SIZE_T i = 0; i < len; i++,buf++)
        sendByte(*buf);
}

void SerialPacker::sendStartFrame(SB_SIZE_T length)
{
#if SP_FRAME_START >= 0
#ifdef SP_MARK
    stream->write(SP_MARK);
#endif
    stream->write(SP_FRAME_START);
#endif
#if SP_MAX_PACKET>255
    if(length > 0x7F) {
#ifdef SP_MARK
        stream->write(SP_MARK);
#endif
        stream->write(length | 0x80);
#ifdef SP_MARK
        stream->write(SP_MARK);
#endif
        stream->write(length >> 7);
    } else
#endif
#ifdef SP_MARK
    stream->write(SP_MARK);
#endif
    stream->write(length);
    sendCRC = 0;
#ifdef SP_SENDLEN
    sendPos = 0;
    sendLen = length;
#endif
    copyInput = false;
}

void SerialPacker::sendStartCopy(SB_SIZE_T addLength)
{
    // ASSERT(!copyInput);

    // Subtract any skipped bytes (i.e.. those read beyond @headerlen) from
    // the resulting header. The receive buffer is guaranteed to contain
    // @headerLen bytes at this point.
    SerialPacker::sendStartFrame(receiveLen + addLength - (receivePos-headerLen));

    for(uint8_t i = 0; i < headerLen; i++)
        sendByte(receiveBuffer[i]);
    copyInput = true;
}

void SerialPacker::sendEndFrame(bool broken)
{
#ifdef SP_TRACE
    SP_TRACE.write(broken ? '-' : '+'),
    SP_TRACE.println(F("SEND"));
#endif
    uint8_t br = broken;
    copyInput = false;
#ifdef SP_SENDLEN
    if(sendPos != sendLen) {
        // oops
        br = 1;
        while(sendPos < sendLen)
            sendByte(0);
    }
#endif

    uint16_t crc = sendCRC;
    if(br)
        crc ^= 1;
#ifdef SP_MARK
    stream->write(SP_MARK);
#endif
    stream->write(crc >> 8);
#ifdef SP_MARK
    stream->write(SP_MARK);
#endif
    stream->write(crc & 0xFF);

#ifdef SP_TRACE
    SP_TRACE.print(F("\nX"));
#ifdef SP_SENDLEN
    SP_TRACE.print(sendLen);
    if(br>1) {
        SP_TRACE.print(F("-BR:"));
        SP_TRACE.println(br-1);
    }
    if(sendPos>sendLen) {
        SP_TRACE.print(F("-POS:"));
        SP_TRACE.print(sendLen);
        SP_TRACE.write(':');
        SP_TRACE.println(sendPos);
    }
#endif // SENDLEN
    if(br) {
        SP_TRACE.print(F("_ERR:"));
        SP_TRACE.println(br);
    } else
        SP_TRACE.println("+");
#endif // TRACE
}

