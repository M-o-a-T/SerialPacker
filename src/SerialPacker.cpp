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
    if(ts-last_ts > SP_MAX_FRAME_DELAY && receiveState != SP_IDLE) {
        receiveState = SP_IDLE;
    }
    switch(receiveState) {
    case SP_IDLE:
#if SP_FRAME_START >= 0
        if(data != SP_FRAME_START) {
            break;
        }
        receiveState = SP_LEN1;
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
        // fall thru
    case SP_DATA:
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
        receiveCRC.add(data);
        receiveState = SP_CRC2;
        break;
    case SP_CRC2:
        receiveCRC.add(data);
        if(receiveCRC.getCRC() != 0) {
            receiveState = SP_IDLE;
            if(copyInput)
                sendEndFrame(true);
        } else {
            receiveLen = receivePos;
            if(onPacketReceived != nullptr) {
                (*onPacketReceived)();
                if(copyInput)
                    sendEndFrame(false);
            }
            receiveState = SP_IDLE;
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
void SerialPacker::sendBuffer(const uint8_t *buffer, SB_SIZE_T len)
{
    if (stream == nullptr || buffer == nullptr)
        return;

    for (SB_SIZE_T i = 0; i < len; i++)
        sendByte(receiveBuffer[i]);
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
    sendLen = length;
#endif
    copyInput = false;
}

void SerialPacker::sendStartCopy(SB_SIZE_T addLength)
{
    // ASSERT(!copyInput);
    // Subtract any skipped bytes from the resulting header
    SerialPacker::sendStartFrame(receiveLen + addLength - (receivePos-headerLen));

    // the receive buffer is guaranteed to contain exactly @headerLen bytes
    // at this point.
    for(uint8_t i = 0; i < headerLen; i++)
        sendByte(receiveBuffer[i]);
    copyInput = true;
}

void SerialPacker::sendEndFrame(bool broken)
{
#ifdef SP_SENDLEN
    SB_SIZE_T length = sendLen;
#endif
    copyInput = false;
#ifdef SP_SENDLEN
    if(length) {
        // oops
        while(length)
            sendByte(0);
        sendCRC.add(0x01); // breaks CRC
        sendLen = 0;
    }
    else
#endif
    if(broken)
        sendCRC.add(0x02);

    uint16_t crc = sendCRC.getCRC();
    stream->write(crc >> 8);
    stream->write(crc & 0xFF);
}

