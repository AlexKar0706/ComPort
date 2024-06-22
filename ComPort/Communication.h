#ifndef _COMMUNICATION_H
#define _COMMUNICATION_H

#include <windows.h>

typedef struct {
	BOOL showTimeStamp;
} PortSettings_t;

typedef struct {
	HANDLE portHandler;
	HANDLE terminalWriteMutex;
	PortSettings_t settings;
	BOOL txThreadRunning;
	BOOL rxThreadRunning;
} PortThreadParameters_t;

HANDLE OpenSerialPort(const char* device, COMMTIMEOUTS* pTimeouts, DCB* pState);
void WaitAvailablePort(ULONG* availablePorts, ULONG portsMax, ULONG* availablePortsNum);
void PrintPorts(ULONG* availablePorts, ULONG availablePortsNum);
void RequestNewDevice(char* deviceName, size_t nameSizeMax);

DWORD WINAPI PortReadingLoopThread(LPVOID threadParameters);
DWORD WINAPI PortWrittingLoopThread(LPVOID threadParameters);


#endif //_COMMUNICATION_H
