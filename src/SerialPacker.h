// Copyright (c) 2020 Stuart Pittaway
// Copyright (c) 2022 Matthias Urlichs
// https://github.com/M-o-a-T/SerialPacker
//
// GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

#pragma once

#include <Arduino.h>
#include <CRC16.h>

//
// Definitions: see README

#ifndef SP_MAX_FRAME_DELAY
#define SP_MAX_FRAME_DELAY 100
#endif

#ifndef SP_MAX_PACKET
#define SP_MAX_PACKET 127
#endif

#ifndef SP_FRAME_START
#define SP_FRAME_START 0x85
#endif

#if SP_MAX_PACKET>255
typedef uint16_t SB_SIZE_T;
#else
typedef uint8_t SB_SIZE_T;
#endif

enum SerialPackerState : uint8_t {
    SP_IDLE=0,
#if SP_FRAME_START >= 0
    SP_LEN1,
#endif
    SP_LEN2,
    SP_DATA0,
    SP_DATA,
    SP_CRC1,
    SP_CRC2,
    SP_DONE, // wait for getting data
    SP_ERROR, // wait for idle
};

class SerialPacker
{
public:
    SerialPacker() : receiveCRC(),sendCRC() {}
    typedef void (*PacketHandlerFunction)();

    void begin(Stream *stream, PacketHandlerFunction onHeader, PacketHandlerFunction onReader, PacketHandlerFunction onPacket, uint8_t *receiveBuf, SB_SIZE_T bufSize, uint8_t headerSize=0)
    {
        stream = stream;
        onHeaderReceived = onHeader;
        onReadReceived = onReader;
        onPacketReceived = onPacket;
        receiveBuffer = receiveBuf;
        receiveBufferLen = bufSize;
        headerLen = headerSize;
    }

    void checkInputStream();

    // start sending
    void sendStartFrame(SB_SIZE_T length);
    void sendStartCopy(SB_SIZE_T addLength);
    void sendDefer(SB_SIZE_T readLength)
    {
        readLen += readLength;
    }

    void sendBuffer(const void *buffer, SB_SIZE_T length);

    void sendByte(uint8_t data)
    {
        //debugByte(data);
#ifdef SP_SENDLEN
        if(!sendLen)
            return; // oops
#endif
        sendCRC.add(data);
        stream->write(data);
#ifdef SP_SENDLEN
        sendLen -= 1;
#endif
    }

    // stop sending
    void sendEndFrame(bool broken=false);

/*    
    void debugByte(uint8_t data)
    {
        if (data <= 0x0F)
        {
            Serial1.write('0');
        }
        Serial1.print(data, HEX);
        Serial1.write(' ');
    }
*/

    bool isCopying() {
        return copyInput;
    }
private:

    // receiver *****

    SerialPackerState receiveState = SP_IDLE;
    CRC16 receiveCRC;

    uint16_t last_ts = 0;

    SB_SIZE_T headerLen = 0;
    SB_SIZE_T readLen = 0;
    bool copyInput = false;

    //Pointer to start of receive buffer (byte array)
    uint8_t *receiveBuffer = nullptr;
    //Index into receive buffer of current position
    SB_SIZE_T receivePos = 0;
    SB_SIZE_T receiveLen = 0;
    SB_SIZE_T receiveBufferLen = 0;

    //Send/Receive stream
    Stream *stream = nullptr;

    // process an incoming byte
    void processByte(uint8_t data);

    //Call back: headerSize bytes have been received
    PacketHandlerFunction onHeaderReceived = nullptr;
    //Call back: readLen bytes have been received
    PacketHandlerFunction onReadReceived = nullptr;
    //Call back: a complete message has been received
    PacketHandlerFunction onPacketReceived = nullptr;

    // sender *****

    CRC16 sendCRC;
#ifdef SP_SENDLEN
    SB_SIZE_T sendLen = 0;
#endif

    void reset()
    {
        receiveState = SP_IDLE;
    }
};
