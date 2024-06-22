#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <windows.h>
#include "Communication.h"

#pragma comment (lib, "OneCore.lib")

static void PrintError(const char* context)
{
	DWORD error_code = GetLastError();
	char buffer[256];
	DWORD size = FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK,
		NULL, error_code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
		buffer, sizeof(buffer), NULL);
	if (size == 0) { buffer[0] = 0; }
	fprintf(stderr, "%s: %s\n", context, buffer);
}

// Opens the specified serial port, configures its timeouts, and sets its
// state.� Returns a handle on success, or INVALID_HANDLE_VALUE on failure.
HANDLE OpenSerialPort(const char* device, COMMTIMEOUTS* pTimeouts, DCB* pState)
{
	HANDLE port = CreateFileA(device, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (port == INVALID_HANDLE_VALUE)
	{
		PrintError(device);
		return INVALID_HANDLE_VALUE;
	}

	// Flush away any bytes previously read or written.
	BOOL success = FlushFileBuffers(port);
	if (!success)
	{
		PrintError("Failed to flush serial port");
		CloseHandle(port);
		return INVALID_HANDLE_VALUE;
	}

	// Configure read and write operations to time out after 50 ms.
	COMMTIMEOUTS defaultTimeouts = { 0 };
	defaultTimeouts.ReadIntervalTimeout = 0;
	defaultTimeouts.ReadTotalTimeoutConstant = 50;
	defaultTimeouts.ReadTotalTimeoutMultiplier = 0;
	defaultTimeouts.WriteTotalTimeoutConstant = 50;
	defaultTimeouts.WriteTotalTimeoutMultiplier = 0;
	if (pTimeouts == NULL) pTimeouts = &defaultTimeouts;

	success = SetCommTimeouts(port, pTimeouts);
	if (!success)
	{
		PrintError("Failed to set serial timeouts");
		CloseHandle(port);
		return INVALID_HANDLE_VALUE;
	}

	// Use usual state, if no information is passed (9600, 8n1)
	DCB defaultState = { 0 };
	defaultState.DCBlength = sizeof(DCB);
	defaultState.BaudRate = 9600;
	defaultState.ByteSize = 8;
	defaultState.Parity = NOPARITY;
	defaultState.StopBits = ONESTOPBIT;
	if (pState == NULL) pState = &defaultState;

	success = SetCommState(port, pState);
	if (!success)
	{
		PrintError("Failed to set serial settings");
		CloseHandle(port);
		return INVALID_HANDLE_VALUE;
	}

	return port;
}

// Writes bytes to the serial port, returning 0 on success and -1 on failure.
static int WritePort(HANDLE port, uint8_t* buffer, size_t size)
{
	DWORD written;
	BOOL success = WriteFile(port, buffer, (DWORD)size, &written, NULL);
	if (!success)
	{
		PrintError("Failed to write to port");
		return -1;
	}
	if (written != size)
	{
		PrintError("Failed to write all bytes to port");
		return -1;
	}
	return 0;
}

// Reads bytes from the serial port.
// Returns after all the desired bytes have been read, or if there is a
// timeout or other error.
// Returns the number of bytes successfully read into the buffer, or -1 if
// there was an error reading.
static SSIZE_T ReadPort(HANDLE port, uint8_t* buffer, size_t size)
{
	DWORD received;
	BOOL success = ReadFile(port, buffer, (DWORD)size, &received, NULL);
	if (!success)
	{
		PrintError("Failed to read from port");
		return -1;
	}
	return received;
}

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

static void PrintTimestamp(void)
{
	SYSTEMTIME time = { 0 };
	GetLocalTime(&time);

	printf("[%02d:%02d:%02d.%03d]", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
}

DWORD WINAPI PortReadingLoopThread(LPVOID threadParameters)
{
	PortThreadParameters_t* pPortParameters = (PortThreadParameters_t*)threadParameters;

	assert(pPortParameters != NULL);

	while (1) {

		uint8_t rxBuffer[1024] = { 0 };
		size_t rxMessageLength = 0;
		SSIZE_T neededBytesToRead = 0;

		do {
			uint8_t rxChar;
			neededBytesToRead = ReadPort(pPortParameters->portHandler, &rxChar, sizeof(rxChar));


			// Check, if COM port is requested to be closed
			if (pPortParameters->txThreadRunning == FALSE) break;

			// Check for timeout
			if (rxMessageLength == 0 && neededBytesToRead == 0) continue;

			rxBuffer[rxMessageLength] = rxChar;
			rxMessageLength++;
		} while ((neededBytesToRead > 0 && rxMessageLength < sizeof(rxBuffer)) || (rxMessageLength == 0 && neededBytesToRead == 0));

		// Finish thread execution, if it is requested from other thread
		if (pPortParameters->txThreadRunning == FALSE) break;

		// Check for the error
		if (neededBytesToRead == -1) {
			printf("Port closing\n");
			break;
		}


		WaitForSingleObject(pPortParameters->terminalWriteMutex, INFINITE);

		if (pPortParameters->settings.showTimeStamp) PrintTimestamp();
		printf("[RX] ");
		for (size_t rxByteCount = 0; rxByteCount < rxMessageLength; rxByteCount++) {
			printf("%c", rxBuffer[rxByteCount]);
		}

		if (!ReleaseMutex(pPortParameters->terminalWriteMutex)) break;

		// Safety for checking overflow
		if (neededBytesToRead > 0) continue;
	}

	pPortParameters->rxThreadRunning = FALSE;
	ReleaseMutex(pPortParameters->terminalWriteMutex);

	return 0;
}

DWORD WINAPI PortWrittingLoopThread(LPVOID threadParameters)
{
	PortThreadParameters_t* pPortParameters = (PortThreadParameters_t*)threadParameters;

	assert(pPortParameters != NULL);

	while (1) {

		// TODO: Decide, what to do with buffer size
		// TODO: Thread should be terminated at any time, if read thread detect problem. Currently it is blocked by fgets

		uint8_t txBuffer[1024] = { 0 };

		if (fgets(&txBuffer[0], sizeof(txBuffer), stdin) == NULL) continue;

		if (!pPortParameters->rxThreadRunning) break;

		size_t txMessageLength = strlen(txBuffer);
		assert(txMessageLength <= sizeof(txBuffer));

		// Check for the command to close COM port communication
		if (strncmp(&txBuffer[0], "-close", 6) == 0) {
			printf("Port closing\n");
			break;
		}

		// TODO: Add support for the different message formats (CR, LF, CRLF)

		WaitForSingleObject(pPortParameters->terminalWriteMutex, INFINITE);

		if (pPortParameters->settings.showTimeStamp) PrintTimestamp();
		printf("[TX] ");
		for (size_t i = 0; i < txMessageLength; i++) printf("%c", txBuffer[i]);
		if (WritePort(pPortParameters->portHandler, &txBuffer[0], txMessageLength) != 0) {
			printf("Port closing\n");
			break;
		}

		if (!ReleaseMutex(pPortParameters->terminalWriteMutex)) break;

		continue;
	}

	pPortParameters->txThreadRunning = FALSE;
	ReleaseMutex(pPortParameters->terminalWriteMutex);

	return 0;
}