// Copyright (c) 2020 Stuart Pittaway
// Copyright (c) 2022 Matthias Urlichs
// https://github.com/M-o-a-T/SerialPacker
//
// GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

#include "SerialPacker.h"

void SerialPacker::processByte(uint8_t data)
{
    //debugByte(data);

    uint16_t ts = millis(); // ignores the high word
    if(ts-last_ts > SP_MAX_FRAME_DELAY && receiveState != SP_IDLE)
    {
#ifdef SP_TRACE
        SP_TRACE.println(" R");
#endif
        receiveState = SP_IDLE;
        errTimeout += 1;
    }
    last_ts = ts;
    switch(receiveState) {
    case SP_IDLE:
#if SP_FRAME_START >= 0
        if(data != SP_FRAME_START) {
#ifdef SP_NONFRAME_STREAM
            SP_NONFRAME_STREAM.write(data);
#endif
            break;
        }
        receiveState = SP_LEN1;
#ifdef SP_TRACE
        SP_TRACE.print("\nS");
#endif
        break;
    case SP_LEN1:
#endif
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
#ifdef SP_TRACE
        SP_TRACE.write('L');
        SP_TRACE.print(receiveLen);
        SP_TRACE.write(' ');
#endif
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
        receiveCRC.restart();
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
#ifdef SP_TRACE
        SP_TRACE.print(data,HEX);
        SP_TRACE.write(' ');
#endif
        receiveCRC.add(data);
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
            uint16_t crc = ((crcHi<<8) | data) ^ receiveCRC.getCRC();
            if(crc != 0) {
    #ifdef SP_TRACE
                SP_TRACE.print("-");
                SP_TRACE.println(receiveCRC.getCRC(), HEX);
    #endif
                if (crc != 1)
                    errCRC += 1;
                receiveState = SP_IDLE;
                if(copyInput)
                    sendEndFrame(true);
            } else {
    #ifdef SP_TRACE
                SP_TRACE.println("+");
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
        processByte((uint8_t)stream->read());

    }
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
    stream->write(SP_FRAME_START);
#endif
#if SP_MAX_PACKET>255
    if(length > 0x7F) {
        stream->write(length | 0x80);
        stream->write(length >> 7);
    } else
#endif
    stream->write(length);
    sendCRC.restart();
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

    uint16_t crc = sendCRC.getCRC();
    if(br)
        crc ^= 1;
    stream->write(crc >> 8);
    stream->write(crc & 0xFF);

#ifdef SP_TRACE
    SP_TRACE.print("\nX");
#ifdef SP_SENDLEN
    SP_TRACE.print(sendLen);
    if(br>1) {
        SP_TRACE.write('-');
        SP_TRACE.print(br-1);
    }
    if(sendPos>sendLen) {
        SP_TRACE.write('-');
        SP_TRACE.print(sendLen-sendPos);
    }
#endif // SENDLEN
    if(br) {
        SP_TRACE.print("_ERR:");
        SP_TRACE.println(br);
    } else
        SP_TRACE.println("+");
#endif // TRACE
}

