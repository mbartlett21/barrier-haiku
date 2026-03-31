/*
uBarrier client -- Implementation for the embedded Barrier client library
    version 1.0.0, July 7th, 2012

Copyright (c) 2012 Alex Evans

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software
    in a product, an acknowledgment in the product documentation would be
    appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
*/
#include "uBarrier.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>


//---------------------------------------------------------------------------------------------------------------------
//	Internal helpers
//---------------------------------------------------------------------------------------------------------------------



/**
@brief Read 16 bit integer in network byte order and convert to native byte order
**/
static int16_t sNetToNative16(const unsigned char *value)
{
#ifdef UBARRIER_LITTLE_ENDIAN
	return value[1] | (value[0] << 8);
#else
	return value[0] | (value[1] << 8);
#endif
}



/**
@brief Read 32 bit integer in network byte order and convert to native byte order
**/
static int32_t sNetToNative32(const unsigned char *value)
{
#ifdef UBARRIER_LITTLE_ENDIAN
	return value[3] | (value[2] << 8) | (value[1] << 16) | (value[0] << 24);
#else
	return value[0] | (value[1] << 8) | (value[2] << 16) | (value[3] << 24);
#endif
}



/**
@brief Trace text to client
**/
static void sTrace(uBarrierContext *context, const char* text)
{
	// Don't trace if we don't have a trace function
	if (context->m_traceFunc != 0L)
		context->m_traceFunc(context->m_cookie, text);
}



/**
@brief Add string to reply packet
**/
static void sAddString(uBarrierContext *context, const char *string)
{
	size_t len = strlen(string);
	memcpy(context->m_replyCur, string, len);
	context->m_replyCur += len;
}



/**
@brief Add data to reply packet
**/
static void sAddData(uBarrierContext *context, const char *string, size_t len)
{
	memcpy(context->m_replyCur, string, len);
	context->m_replyCur += len;
}



/**
@brief Add uint8 to reply packet
**/
static void sAddUInt8(uBarrierContext *context, uint8_t value)
{
	*context->m_replyCur++ = value;
}



/**
@brief Add uint16 to reply packet
**/
static void sAddUInt16(uBarrierContext *context, uint16_t value)
{
	uint8_t *reply = context->m_replyCur;
	*reply++ = (uint8_t)(value >> 8);
	*reply++ = (uint8_t)value;
	context->m_replyCur = reply;
}



/**
@brief Add uint32 to reply packet
**/
static void sAddUInt32(uBarrierContext *context, uint32_t value)
{
	uint8_t *reply = context->m_replyCur;
	*reply++ = (uint8_t)(value >> 24);
	*reply++ = (uint8_t)(value >> 16);
	*reply++ = (uint8_t)(value >> 8);
	*reply++ = (uint8_t)value;
	context->m_replyCur = reply;
}



/**
@brief Send reply packet
**/
static uBarrierBool sSendReply(uBarrierContext *context)
{
	// Set header size
	uint8_t		*reply_buf	= context->m_replyBuffer;
	uint32_t	reply_len	= (uint32_t)(context->m_replyCur - reply_buf);				/* Total size of reply */
	uint32_t	body_len	= reply_len - 4;											/* Size of body */
	uBarrierBool ret;
	reply_buf[0] = (uint8_t)(body_len >> 24);
	reply_buf[1] = (uint8_t)(body_len >> 16);
	reply_buf[2] = (uint8_t)(body_len >> 8);
	reply_buf[3] = (uint8_t)body_len;

	// Send reply
	ret = context->m_sendFunc(context->m_cookie, context->m_replyBuffer, reply_len);

	// Reset reply buffer write pointer
	context->m_replyCur = context->m_replyBuffer+4;
	return ret;
}



/**
@brief Call mouse callback after a mouse event
**/
static void sSendMouseCallback(uBarrierContext *context)
{
	// Skip if no callback is installed
	if (context->m_mouseCallback == 0L)
		return;

	// Send callback
	context->m_mouseCallback(context->m_cookie, context->m_mouseX, context->m_mouseY, context->m_mouseWheelX,
		context->m_mouseWheelY, context->m_mouseButtonLeft, context->m_mouseButtonRight, context->m_mouseButtonMiddle);
}



/**
@brief Send keyboard callback when a key has been pressed or released
**/
static void sSendKeyboardCallback(uBarrierContext *context, uint16_t key, uint16_t modifiers, uBarrierBool down, uBarrierBool repeat)
{
	// Skip if no callback is installed
	if (context->m_keyboardCallback == 0L)
		return;

	// Send callback
	context->m_keyboardCallback(context->m_cookie, key, modifiers, down, repeat);
}



/**
@brief Send joystick callback
**/
static void sSendJoystickCallback(uBarrierContext *context, uint8_t joyNum)
{
	int8_t *sticks;

	// Skip if no callback is installed
	if (context->m_joystickCallback == 0L)
		return;

	// Send callback
	sticks = context->m_joystickSticks[joyNum];
	context->m_joystickCallback(context->m_cookie, joyNum, context->m_joystickButtons[joyNum], sticks[0], sticks[1], sticks[2], sticks[3]);
}

static int natoi(const char *s, int n)
{
    int x = 0;
    while(isdigit(s[0]) && n--)
    {
        x = x * 10 + (s[0] - '0');
        s++;
    }
    return x;
}

/**
@brief Parse a single client message, update state, send callbacks and send replies
**/
#define UBARRIER_IS_PACKET(pkt_id)	memcmp(message+4, pkt_id, 4)==0
static void sProcessMessage(uBarrierContext *context, const uint8_t *message)
{
	// We have a packet!
	if (memcmp(message+4, "Barrier", 7)==0 || memcmp(message+4, "Synergy", 7)==0)
	{
		// Welcome message
		//		kMsgHello			= "Barrier%2i%2i"
		//		kMsgHelloBack		= "Barrier%2i%2i%s"
		sAddData(context, message + 4, 7);
		sAddUInt16(context, UBARRIER_PROTOCOL_MAJOR);
		sAddUInt16(context, UBARRIER_PROTOCOL_MINOR);
		sAddUInt32(context, (uint32_t)strlen(context->m_clientName));
		sAddString(context, context->m_clientName);
		if (!sSendReply(context))
		{
			// Send reply failed, let's try to reconnect
			sTrace(context, "SendReply failed, trying to reconnect in a second");
			context->m_connected = UBARRIER_FALSE;
			context->m_sleepFunc(context->m_cookie, 1000);
		}
		else
		{
			// Let's assume we're connected
			char buffer[256+1];
			sprintf(buffer, "Connected as client \"%s\"", context->m_clientName);
			sTrace(context, buffer);
			context->m_hasReceivedHello = UBARRIER_TRUE;
		}
		return;
	}
	else if (UBARRIER_IS_PACKET("QINF"))
	{
		// Screen info. Reply with DINF
		//		kMsgQInfo			= "QINF"
		//		kMsgDInfo			= "DINF%2i%2i%2i%2i%2i%2i%2i"
		uint16_t x = 0, y = 0, warp = 0;
		sAddString(context, "DINF");
		sAddUInt16(context, x);
		sAddUInt16(context, y);
		sAddUInt16(context, context->m_clientWidth);
		sAddUInt16(context, context->m_clientHeight);
		sAddUInt16(context, warp);
		sAddUInt16(context, 0);		// current mouse x
		sAddUInt16(context, 0);		// current mouse y
		sSendReply(context);
		return;
	}
	else if (UBARRIER_IS_PACKET("CIAK"))
	{
		// Do nothing?
		//		kMsgCInfoAck		= "CIAK"
		return;
	}
	else if (UBARRIER_IS_PACKET("CROP"))
	{
		// Do nothing?
		//		kMsgCResetOptions	= "CROP"
		return;
	}
	else if (UBARRIER_IS_PACKET("CINN"))
	{
		// Screen enter. Reply with CNOP
		//		kMsgCEnter 			= "CINN%2i%2i%4i%2i"

		// Obtain the Barrier sequence number
		context->m_sequenceNumber = sNetToNative32(message + 12);
		context->m_isCaptured = UBARRIER_TRUE;

		// Call callback
		if (context->m_screenActiveCallback != 0L)
			context->m_screenActiveCallback(context->m_cookie, UBARRIER_TRUE);

		/* grab the clipboard ownership */
		sAddString(context, "CCLP");
		sAddUInt8(context, 0);
		sAddUInt32(context, context->m_sequenceNumber);
		sSendReply(context);
		context->m_clipboardOwned = UBARRIER_TRUE;
	}
	else if (UBARRIER_IS_PACKET("COUT"))
	{
		// Screen leave
		//		kMsgCLeave 			= "COUT"
		context->m_isCaptured = UBARRIER_FALSE;

		// Call callback
		if (context->m_screenActiveCallback != 0L)
			context->m_screenActiveCallback(context->m_cookie, UBARRIER_FALSE);
	}
	else if (UBARRIER_IS_PACKET("DMDN"))
	{
		// Mouse down
		//		kMsgDMouseDown		= "DMDN%1i"
		char btn = message[8]-1;
		if (btn==2)
			context->m_mouseButtonRight		= UBARRIER_TRUE;
		else if (btn==1)
			context->m_mouseButtonMiddle	= UBARRIER_TRUE;
		else
			context->m_mouseButtonLeft		= UBARRIER_TRUE;
		sSendMouseCallback(context);
	}
	else if (UBARRIER_IS_PACKET("DMUP"))
	{
		// Mouse up
		//		kMsgDMouseUp		= "DMUP%1i"
		char btn = message[8]-1;
		if (btn==2)
			context->m_mouseButtonRight		= UBARRIER_FALSE;
		else if (btn==1)
			context->m_mouseButtonMiddle	= UBARRIER_FALSE;
		else
			context->m_mouseButtonLeft		= UBARRIER_FALSE;
		sSendMouseCallback(context);
	}
	else if (UBARRIER_IS_PACKET("DMMV"))
	{
		// Mouse move. Reply with CNOP
		//		kMsgDMouseMove		= "DMMV%2i%2i"
		context->m_mouseX = sNetToNative16(message+8);
		context->m_mouseY = sNetToNative16(message+10);
		sSendMouseCallback(context);
	}
	else if (UBARRIER_IS_PACKET("DMWM"))
	{
		// Mouse wheel
		//		kMsgDMouseWheel		= "DMWM%2i%2i"
		//		kMsgDMouseWheel1_0	= "DMWM%2i"
		context->m_mouseWheelX += sNetToNative16(message+8);
		context->m_mouseWheelY += sNetToNative16(message+10);
		sSendMouseCallback(context);
	}
	else if (UBARRIER_IS_PACKET("DKDN"))
	{
		// Key down
		//		kMsgDKeyDown		= "DKDN%2i%2i%2i"
		//		kMsgDKeyDown1_0		= "DKDN%2i%2i"
		//uint16_t id = sNetToNative16(message+8);
		uint16_t mod = sNetToNative16(message+10);
		uint16_t key = sNetToNative16(message+12);
		sSendKeyboardCallback(context, key, mod, UBARRIER_TRUE, UBARRIER_FALSE);
	}
	else if (UBARRIER_IS_PACKET("DKRP"))
	{
		// Key repeat
		//		kMsgDKeyRepeat		= "DKRP%2i%2i%2i%2i"
		//		kMsgDKeyRepeat1_0	= "DKRP%2i%2i%2i"
		uint16_t mod = sNetToNative16(message+10);
//		uint16_t count = sNetToNative16(message+12);
		uint16_t key = sNetToNative16(message+14);
		sSendKeyboardCallback(context, key, mod, UBARRIER_TRUE, UBARRIER_TRUE);
	}
	else if (UBARRIER_IS_PACKET("DKUP"))
	{
		// Key up
		//		kMsgDKeyUp			= "DKUP%2i%2i%2i"
		//		kMsgDKeyUp1_0		= "DKUP%2i%2i"
		//uint16 id=Endian::sNetToNative(sbuf[4]);
		uint16_t mod = sNetToNative16(message+10);
		uint16_t key = sNetToNative16(message+12);
		sSendKeyboardCallback(context, key, mod, UBARRIER_FALSE, UBARRIER_FALSE);
	}
	else if (UBARRIER_IS_PACKET("DGBT"))
	{
		// Joystick buttons
		//		kMsgDGameButtons	= "DGBT%1i%2i";
		uint8_t	joy_num = message[8];
		if (joy_num<UBARRIER_NUM_JOYSTICKS)
		{
			// Copy button state, then send callback
			context->m_joystickButtons[joy_num] = (message[9] << 8) | message[10];
			sSendJoystickCallback(context, joy_num);
		}
	}
	else if (UBARRIER_IS_PACKET("DGST"))
	{
		// Joystick sticks
		//		kMsgDGameSticks		= "DGST%1i%1i%1i%1i%1i";
		uint8_t	joy_num = message[8];
		if (joy_num<UBARRIER_NUM_JOYSTICKS)
		{
			// Copy stick state, then send callback
			memcpy(context->m_joystickSticks[joy_num], message+9, 4);
			sSendJoystickCallback(context, joy_num);
		}
	}
	else if (UBARRIER_IS_PACKET("DSOP"))
	{
		// Set options
		//		kMsgDSetOptions		= "DSOP%4I"
	}
	else if (UBARRIER_IS_PACKET("CALV"))
	{
		// Keepalive, reply with CALV and then CNOP
		//		kMsgCKeepAlive		= "CALV"
		sAddString(context, "CALV");
		sSendReply(context);
		// now reply with CNOP
	}
	else if (UBARRIER_IS_PACKET("CCLP"))
	{
		/* clipboard has been grabbed */
		context->m_clipboardOwned = UBARRIER_FALSE;
		return;
	}
	else if (UBARRIER_IS_PACKET("DCLP"))
	{
		// Clipboard message
		//		kMsgDClipboard		= "DCLP%1i%4i%s"
		//
		// The clipboard message contains:
		//		1 uint32:	The size of the message
		//		4 chars: 	The identifier ("DCLP")
		//		1 uint8: 	The clipboard index
		//		1 uint32:	The sequence number. It's zero, because this message is always coming from the server
		//		1 uint8:	The mark (DataStart / DataChunk / DataEnd = 1/2/3)
		//		1 uint32:	The current data length
		//		* chars:	The current data
		//	If mark == DataStart (1)
		//		Current data is a string representation of the length of the full clipboard data. Equivalent to the output of printf("%d", encodedclipboardlength)
		//	ElseIf mark == DataChunk (2)
		//		Current data is appended to the current buffer
		//	ElseIf mark == DataEnd (3)
		//		Indicates the end of data. Length is always 0
		//		The program should verify that the clipboard length is correct and then set its clipboard.

		//	The encoded clipboard data contains:
		//		1 uint32:	The number of formats present in the clipboard
		//	For each format:
		//		1 uint32:	The format of the clipboard data (0 = text)
		//		1 uint32:	The size of this set of clipboard data
		//		n uint8:	The data

		uint8_t clipindex = message[8];
		uint8_t mark = message[13];
		uint32_t datalen = sNetToNative32(message + 14);
		if (datalen > UBARRIER_RECEIVE_BUFFER_SIZE) {
			sTrace(context, "DCLP data length too long. ignoring");
			return;
		}
		const uint8_t *datastart = message + 18;

		/* Only receive clipboard 0 */
		if (clipindex == 0) {
			if (mark == 1) {
				context->m_clipboardRecvState = 1; // Receiving
				context->m_clipboardRecvLength = natoi(datastart, datalen);

				context->m_clipboardRecvOffset = 0;
				if (context->m_clipboardRecvLength >= UBARRIER_RECEIVE_CLIPBOARD_SIZE) {
					sTrace(context, "Clipboard receive length too long. ignoring");
					context->m_clipboardRecvState = 0; // off
				}
			} else if (mark == 2) { // DataChunk
				if (context->m_clipboardRecvState != 1)
					sTrace(context, "Clipboard not in receiving state. ignoring data.");
				else {
					uint8_t *datatostart = context->m_clipboardRecvBuffer + context->m_clipboardRecvOffset;
					if (context->m_clipboardRecvOffset + datalen >= UBARRIER_RECEIVE_CLIPBOARD_SIZE)
						sTrace(context, "Clipboard receive length too long. ignoring");
					else {
						memcpy(datatostart, datastart, datalen);
						context->m_clipboardRecvOffset += datalen;
					}
				}
			} else if (mark == 3) { // DataEnd
				if (context->m_clipboardRecvState != 1)
					sTrace(context, "Clipboard not in receiving state. ignoring data.");
				else if (context->m_clipboardRecvOffset != context->m_clipboardRecvLength)
					sTrace(context, "Clipboard length invalid. ignoring data.");
				else {
					const uint8_t *	parse_msg	= context->m_clipboardRecvBuffer;
					const uint8_t *bufEnd = context->m_clipboardRecvBuffer + context->m_clipboardRecvOffset;
					// now parse the output
					context->m_clipboardRecvState = 0; // off
					uint32_t num_formats = sNetToNative32(parse_msg);
					parse_msg += 4;

					for (; num_formats; num_formats--)
					{
						if (parse_msg > bufEnd - 8) {
							sTrace(context, "clipboard overrun while parsing");
							return;
						}
						// Parse clipboard format header
						uint32_t format	= sNetToNative32(parse_msg);
						uint32_t size	= sNetToNative32(parse_msg+4);
						parse_msg += 8;
						if (parse_msg > bufEnd - size) {
							sTrace(context, "clipboard overrun while parsing (size)");
							return;
						}
						// Call callback
						if (context->m_clipboardCallback)
							context->m_clipboardCallback(context->m_cookie, format, parse_msg, size);
						parse_msg += size;
					}
				}
			}
		}
		return;
	}
	else if (UBARRIER_IS_PACKET("CBYE")) 
	{
		// Server shutting down
		//		kMsgCClose 			= "CBYE"
		sTrace(context, "Server disconnecting");
	}
	else if (UBARRIER_IS_PACKET("EUNK")) 
	{
		// Client is Unknown
		//		kMsgEUnknown		= "EUNK"
		sTrace(context, "Client is unknown to server");
	}
	else
	{
		// Unknown packet, could be any of these
		//		kMsgCNoop 			= "CNOP"
		//		kMsgCScreenSaver 	= "CSEC%1i"
		//		kMsgDKeyRepeat		= "DKRP%2i%2i%2i%2i"
		//		kMsgDKeyRepeat1_0	= "DKRP%2i%2i%2i"
		//		kMsgDMouseRelMove	= "DMRM%2i%2i"
		//		kMsgEIncompatible	= "EICV%2i%2i"
		//		kMsgEBusy 			= "EBSY"
		//		kMsgEBad			= "EBAD"
		char buffer[64];
		sprintf(buffer, "Unknown packet '%c%c%c%c'", message[4], message[5], message[6], message[7]);
		sTrace(context, buffer);
		return;
	}

	// Reply with CNOP maybe?
	sAddString(context, "CNOP");
	sSendReply(context);
}
#undef UBARRIER_IS_PACKET



/**
@brief Mark context as being disconnected
**/
static void sSetDisconnected(uBarrierContext *context)
{
	context->m_connected		= UBARRIER_FALSE;
	context->m_hasReceivedHello = UBARRIER_FALSE;
	context->m_isCaptured		= UBARRIER_FALSE;
	context->m_replyCur			= context->m_replyBuffer + 4;
	context->m_sequenceNumber	= 0;
	context->m_clipboardOwned	= UBARRIER_FALSE;
	context->m_clipboardRecvOffset	= 0;
	context->m_clipboardRecvLength	= 0;
	context->m_clipboardRecvState	= 0;
}



/**
@brief Update a connected context
**/
static void sUpdateContext(uBarrierContext *context)
{
	/* Receive data (blocking) */
	int receive_size = UBARRIER_RECEIVE_BUFFER_SIZE - context->m_receiveOfs;
	int num_received = 0;
	int packlen = 0;
	if (context->m_receiveFunc(context->m_cookie, context->m_receiveBuffer + context->m_receiveOfs, receive_size, &num_received) == UBARRIER_FALSE)
	{
		/* Receive failed, let's try to reconnect */
		char buffer[128];
		sprintf(buffer, "Receive failed (%d bytes asked, %d bytes received), trying to reconnect in a second", receive_size, num_received);
		sTrace(context, buffer);
		sSetDisconnected(context);
		context->m_sleepFunc(context->m_cookie, 1000);
		return;
	}
	context->m_receiveOfs += num_received;

	/*	If we didn't receive any data then we're probably still polling to get connected and
		therefore not getting any data back. To avoid overloading the system with a Barrier
		thread that would hammer on polling, we let it rest for a bit if there's no data. */
	if (num_received == 0)
		context->m_sleepFunc(context->m_cookie, 500);

	/* Check for timeouts */
	if (context->m_hasReceivedHello)
	{
		uint32_t cur_time = context->m_getTimeFunc();
		if (num_received == 0)
		{
			/* Timeout after 2 secs of inactivity (we received no CALV) */
			if ((cur_time - context->m_lastMessageTime) > UBARRIER_IDLE_TIMEOUT)
				sSetDisconnected(context);
		}
		else
			context->m_lastMessageTime = cur_time;
	}

	/* Eat packets */
	for (;;)
	{
		/* Grab packet length and bail out if the packet goes beyond the end of the buffer */
		packlen = sNetToNative32(context->m_receiveBuffer);
		if (packlen+4 > context->m_receiveOfs)
			break;

		/* Process message */
		sProcessMessage(context, context->m_receiveBuffer);

		/* Move packet to front of buffer */
		memmove(context->m_receiveBuffer, context->m_receiveBuffer+packlen+4, context->m_receiveOfs-packlen-4);
		context->m_receiveOfs -= packlen+4;
	}

	/* Throw away over-sized packets */
	if (packlen > UBARRIER_RECEIVE_BUFFER_SIZE)
	{
		/* Oversized packet, ditch tail end */
		char buffer[128];
		sprintf(buffer, "Oversized packet: '%c%c%c%c' (length %d)", context->m_receiveBuffer[4], context->m_receiveBuffer[5], context->m_receiveBuffer[6], context->m_receiveBuffer[7], packlen);
		sTrace(context, buffer);
		num_received = context->m_receiveOfs-4; // 4 bytes for the size field
		while (num_received != packlen)
		{
			int buffer_left = packlen - num_received;
			int to_receive = buffer_left < UBARRIER_RECEIVE_BUFFER_SIZE ? buffer_left : UBARRIER_RECEIVE_BUFFER_SIZE;
			int ditch_received = 0;
			if (context->m_receiveFunc(context->m_cookie, context->m_receiveBuffer, to_receive, &ditch_received) == UBARRIER_FALSE)
			{
				/* Receive failed, let's try to reconnect */
				sTrace(context, "Receive failed, trying to reconnect in a second");
				sSetDisconnected(context);
				context->m_sleepFunc(context->m_cookie, 1000);
				break;
			}
			else
			{
				num_received += ditch_received;
			}
		}
		context->m_receiveOfs = 0;
	}
}


//---------------------------------------------------------------------------------------------------------------------
//	Public interface
//---------------------------------------------------------------------------------------------------------------------



/**
@brief Initialize uBarrier context
**/
void uBarrierInit(uBarrierContext *context)
{
	/* Zero memory */
	memset(context, 0, sizeof(uBarrierContext));

	/* Initialize to default state */
	sSetDisconnected(context);
}


/**
@brief Update uBarrier
**/
void uBarrierUpdate(uBarrierContext *context)
{
	if (context->m_connected)
	{
		/* Update context, receive data, call callbacks */
		sUpdateContext(context);
	}
	else
	{
		/* Try to connect */
		if (context->m_connectFunc(context->m_cookie))
			context->m_connected = UBARRIER_TRUE;
	}
}



/**
@brief Send clipboard data
**/
void uBarrierSendClipboard(uBarrierContext *context, const char *text, uint32_t text_length)
{
	if (!context->m_clipboardOwned) {
		sTrace(context, "clipboard not currently owned");
		return;
	}

	char intbuffer[15] = {0};
	// Calculate maximum size that will fit in a reply packet
	uint32_t overhead_size =	4 +					/* Message size */
								4 +					/* Message ID */
								1 +					/* Clipboard index */
								4 +					/* Sequence number */
								4 +					/* Rest of message size (because it's a Barrier string from here on) */
								4 +					/* Number of clipboard formats */
								4 +					/* Clipboard format */
								4;					/* Clipboard data length */
	uint32_t max_length_packet = UBARRIER_REPLY_BUFFER_SIZE - overhead_size;

	/* packet has to be done in segments */

	/* file size has to be done as a literal integer */
	uint32_t size_len = sprintf(intbuffer, "%d", text_length + 12);

	// Initial packet
	sAddString(context, "DCLP");
	sAddUInt8(context, 0);							/* Clipboard index */
	sAddUInt32(context, context->m_sequenceNumber);
	sAddUInt8(context, 1);
	sAddUInt32(context, size_len);
	sAddString(context, intbuffer);
	sSendReply(context);

	/* send initial packet with header */
	sAddString(context, "DCLP");
	sAddUInt8(context, 0);							/* Clipboard index */
	sAddUInt32(context, context->m_sequenceNumber);
	sAddUInt8(context, 2); /* dataChunk */
	sAddUInt32(context, 12);
	sAddUInt32(context, 1); /* number of formats */
	sAddUInt32(context, UBARRIER_CLIPBOARD_FORMAT_TEXT);
	sAddUInt32(context, text_length);
	sSendReply(context);
	// sAddData(context, &text[curr], dataLen);

	for (uint32_t curr = 0; curr < text_length; curr += max_length_packet) {
		// Intermediate packet
		uint32_t dataLen = max_length_packet;
		if (text_length < curr + dataLen)
			dataLen = text_length - curr;

		sAddString(context, "DCLP");
		sAddUInt8(context, 0);							/* Clipboard index */
		sAddUInt32(context, context->m_sequenceNumber);
		sAddUInt8(context, 2); /* dataChunk */
		sAddUInt32(context, dataLen);
		sAddData(context, &text[curr], dataLen);
		sSendReply(context);
	}

	sAddString(context, "DCLP");
	sAddUInt8(context, 0);							/* Clipboard index */
	sAddUInt32(context, context->m_sequenceNumber);
	sAddUInt8(context, 3);
	sAddUInt32(context, 0);
	sSendReply(context);
}
