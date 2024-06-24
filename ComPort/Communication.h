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

typedef struct {
	BOOL showTimeStamp;
} PortSettings_t;

typedef struct {
	HANDLE portHandler;
	PortSettings_t settings;
	BOOL txThreadRunning;
	BOOL rxThreadRunning;
} PortThreadParameters_t;

HANDLE OpenSerialPort(const char* device, COMMTIMEOUTS* pTimeouts, DCB* pState);

void StartCommunication(HANDLE port, DWORD dwWaitMilliseconds);
DWORD CheckCommunicationRunning(void);
BOOL GetPortErrorMessage(Message_t* pMessage);
BOOL GetPortMessage(Message_t* pMessage);
BOOL PutPortMessage(Message_t message, BOOL fullQueueBlock);
DWORD CloseCommunication(HANDLE port);

DWORD WINAPI PortReadingLoopThread(LPVOID threadParameters);
DWORD WINAPI PortWrittingLoopThread(LPVOID threadParameters);


#endif //_COMMUNICATION_H
