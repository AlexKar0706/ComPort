#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <windows.h>
#include "Communication.h"

void WaitAvailablePort(ULONG* availablePorts, ULONG portsMax, ULONG* availablePortsNum)
{
	assert(availablePorts != NULL && availablePortsNum != NULL);

	GetCommPorts(availablePorts, portsMax, availablePortsNum);

	uint8_t animation = 0;
	while (*availablePortsNum == 0) {

		if (animation == 0) printf("Waiting for the COM ports");
		else if (animation >= 1 && animation <= 3) printf(".");

		GetCommPorts(availablePorts, portsMax, availablePortsNum);

		Sleep(500);
		animation++;
		if (animation > 3) {
			animation = 0;
			system("cls");
		}

	}

	system("cls");
}

// Print available ports to the console
void PrintPorts(ULONG* availablePorts, ULONG availablePortsNum)
{
	assert(availablePorts != NULL);

	printf("Available ports:\n");

	for (ULONG portNum = 0; portNum < availablePortsNum; portNum++) {

		printf("COM%lu\n", availablePorts[portNum]);

	}

	printf("\n");
}

void RequestNewDevice(char* deviceName, size_t nameSizeMax)
{

	// TODO: Accept only available ports, dismiss other choices

	// TODO: Make ports select by keys instead of typying it's name

	// TODO: Update available devices during request in case of removing device

	while (1) {

		char InputBuffer[256] = "\\\\.\\";
		printf("Type device name: ");

		if (fgets(&InputBuffer[4], sizeof(InputBuffer) - 4, stdin) == NULL) continue;


		if ((strncmp("COM", &InputBuffer[4], 3) != 0) ||
			(!isdigit(InputBuffer[7])) ||
			(InputBuffer[8] != '\n' && !isdigit(InputBuffer[8])) ||
			(InputBuffer[9] != '\n' && isdigit(InputBuffer[8]))) {

			printf("Wrong device name\n");
			continue;

		}

		size_t digitNum = 1;
		if (isdigit(InputBuffer[8])) digitNum = 2;
		InputBuffer[7 + digitNum] = '\0';
		strncpy_s(deviceName, nameSizeMax, InputBuffer, (8 + digitNum));
		return;
	}
}

static void PrintMessage(Message_t* pMessage) 
{
	for (size_t i = 0; i < pMessage->length; i++) printf("%c", pMessage->buffer[i]);
}

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
			Message_t errorMessage = { 0 };
			GetPortErrorMessage(&errorMessage);
			PrintMessage(&errorMessage);
			Sleep(2000);
			system("cls");
			continue;
		}

		printf("Device opened. Start communication with COM port:\n\n");

		StartCommunication(port, 1);

		while (CheckCommunicationRunning() == WAIT_TIMEOUT) {
			Message_t newMessage = { 0 };
			if (GetPortMessage(&newMessage)) PrintMessage(&newMessage);
		}

		Message_t errorMessage = { 0 };
		if (GetPortErrorMessage(&errorMessage)) PrintMessage(&errorMessage);

		printf("Port closing\n");
		Sleep(2000);
		CloseCommunication(port);
	}
}

int main(void)
{
	RunCommandLineCommunication();
	return 0;
}
