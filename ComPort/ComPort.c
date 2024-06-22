#include <stdio.h>
#include <assert.h>
#include <windows.h>
#include "Communication.h"

void RunCommandLineCommunication(void) 
{
	while (1) {
		ULONG availablePortsNum;
		ULONG availablePorts[50];
		char deviceName[20] = "";

		WaitAvailablePort(availablePorts, sizeof(availablePorts) / sizeof(ULONG), &availablePortsNum);
		PrintPorts(availablePorts, availablePortsNum);
		RequestNewDevice(deviceName, sizeof(deviceName));
		HANDLE port = OpenSerialPort(deviceName, NULL, NULL);
		if (port == INVALID_HANDLE_VALUE) {
			Sleep(2000);
			system("cls");
			continue;
		}

		printf("Device opened. Start communication with COM port:\n\n");

		HANDLE portThreads[2] = { 0 };
		HANDLE threadTerminalWriteMutex = NULL;
		PortThreadParameters_t portParameter = { 0 };

		threadTerminalWriteMutex = CreateMutex(NULL, FALSE, NULL);
		assert(threadTerminalWriteMutex != NULL);

		portParameter.portHandler = port;
		portParameter.terminalWriteMutex = threadTerminalWriteMutex;
		portParameter.rxThreadRunning = portParameter.txThreadRunning = TRUE;
		portParameter.settings.showTimeStamp = TRUE;

		portThreads[0] = CreateThread(NULL, 0, PortWrittingLoopThread, &portParameter, 0, NULL);
		assert(portThreads[0] != NULL);

		portThreads[1] = CreateThread(NULL, 0, PortReadingLoopThread, &portParameter, 0, NULL);
		assert(portThreads[1] != NULL);

		WaitForMultipleObjects(sizeof(portThreads) / sizeof(portThreads[0]), portThreads, TRUE, INFINITE);

		Sleep(2000);
		CloseHandle(portThreads[0]);
		CloseHandle(portThreads[1]);
		CloseHandle(port);
		CloseHandle(threadTerminalWriteMutex);
		system("cls");
	}
}

int main(void)
{
	RunCommandLineCommunication();
	return 0;
}
