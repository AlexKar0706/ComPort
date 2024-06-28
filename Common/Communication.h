#ifndef _COMMUNICATION_H
#define _COMMUNICATION_H

#include <windows.h>

#define PORT_BUFFER_METADATA_SIZE			 (128)  // Maximum size of buffer metadata
#define PORT_BUFFER_DATA_SIZE				(1024)  // Maximum size of buffer data

#define PORT_BUFFER_SIZE		  (PORT_BUFFER_METADATA_SIZE + PORT_BUFFER_DATA_SIZE)

typedef struct {
	uint8_t buffer[PORT_BUFFER_SIZE];
	size_t length;
} Message_t;

#define GET_TX_BUFFER_TRANSMIT		(0)
#define GET_TX_BUFFER_NO_MESSAGE	(1)
#define GET_TX_BUFFER_TERMINATE		(2)

typedef struct {
	/* 
		Important handler, that fills txbuffer for the transmit message
		Functions runs in the seperate thread; User is responsible to make function thread-safe and non-blockable
		If function returns GET_TX_BUFFER_TRANSMIT, message will be transmitted via COM port
		If function returns GET_TX_BUFFER_NO_MESSAGE, thread will continue to wait for the message
		If function returns GET_TX_BUFFER_TERMINATE, communication will be immediately terminated
	*/
	DWORD (*GetTxBufferUserHandler)(uint8_t* pTxBuffer, size_t txBufferSize);
	/* Show timestamp at the start of rx/tx messagfe */
	BOOL showTimeStamp;
} PortSettings_t;

/* Communication functions */

HANDLE OpenSerialPort(const char* device, COMMTIMEOUTS* pTimeouts, DCB* pState);

void StartCommunication(HANDLE port, PortSettings_t portSettings, DWORD dwWaitMilliseconds);
DWORD CheckCommunicationRunning(void);
DWORD CloseCommunication(HANDLE port);

/* Message queue functions */

BOOL GetPortErrorMessage(Message_t* pMessage);
BOOL GetPortMessage(Message_t* pMessage);
BOOL PutPortMessage(Message_t message, BOOL fullQueueBlock);


#endif //_COMMUNICATION_H
