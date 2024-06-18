#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <windows.h>

#pragma comment (lib, "OneCore.lib")

typedef struct {
	HANDLE portHandler;
	BOOL txThreadRunning;
	BOOL rxThreadRunning;
} PortThreadParameters_t;

void PrintError(const char* context)
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
// state.  Returns a handle on success, or INVALID_HANDLE_VALUE on failure.
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

	// Configure read and write operations to time out after 100 ms.
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
int WritePort(HANDLE port, uint8_t* buffer, size_t size)
{
	DWORD written;
	BOOL success = WriteFile(port, buffer, size, &written, NULL);
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
SSIZE_T ReadPort(HANDLE port, uint8_t* buffer, size_t size)
{
	DWORD received;
	BOOL success = ReadFile(port, buffer, size, &received, NULL);
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
			(InputBuffer[9] != '\n' &&  isdigit(InputBuffer[8]))) {

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

			// Check for timeout
			if (rxMessageLength == 0 && neededBytesToRead == 0) continue;

			rxBuffer[rxMessageLength] = rxChar;
			rxMessageLength++;
		} while (neededBytesToRead > 0 && rxMessageLength < sizeof(rxBuffer));

		// Check for the error
		if (neededBytesToRead == -1) {
			printf("Port closing\n");
			break;
		}

		// TODO: Add time stamp of message loging

		for (size_t rxByteCount = 0; rxByteCount < rxMessageLength; rxByteCount++) {
			printf("%c", rxBuffer[rxByteCount]);
		}

		// Safety for checking overflow
		if (neededBytesToRead > 0) continue;


		// TODO: Add sequence to close port by command or key
	}

	pPortParameters->rxThreadRunning = FALSE;

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

		// TODO: Add time stamp of message loging
		// TODO: Add support for the different message formats (CR, LF, CRLF)

		if (WritePort(pPortParameters->portHandler, &txBuffer[0], txMessageLength) != 0) {
			printf("Port closing\n");
			break;
		}

		// TODO: Add sequence to close port by command or key
		continue;
	}

	pPortParameters->txThreadRunning = FALSE;

	return 0;
}

int main(void)
{
	ULONG availablePortsNum;
	ULONG availablePorts[50];

	while (1) {
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

		printf("Device opened. Start reading COM port:\n\n");

		HANDLE portThreads[2] = { 0 };
		PortThreadParameters_t portParameter = { 0 };
		portParameter.portHandler = port;
		portParameter.rxThreadRunning = portParameter.txThreadRunning = TRUE;

		portThreads[0] = CreateThread(NULL, 0, PortWrittingLoopThread, &portParameter, 0, NULL);
		assert(portThreads[0] != NULL);

		portThreads[1] = CreateThread(NULL, 0, PortReadingLoopThread, &portParameter, 0, NULL);
		assert(portThreads[1] != NULL);

		WaitForMultipleObjects(sizeof(portThreads) / sizeof(portThreads[0]), portThreads, TRUE, INFINITE);

		Sleep(2000);
		CloseHandle(portThreads[0]);
		CloseHandle(portThreads[1]);
		CloseHandle(port);
		system("cls");
	}

	return 0;
}
