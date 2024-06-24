#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <windows.h>
#include "Communication.h"

#pragma comment (lib, "OneCore.lib")

#define MESSAGE_QUEUE_SIZE	(10) // Maximum available size of queue

typedef struct {
	Message_t messages[MESSAGE_QUEUE_SIZE];
	size_t head;
	size_t tail;
	size_t messagesInQueue;
} MessageQueue_t;

typedef struct {
	HANDLE portHandler;
	PortSettings_t settings;
	BOOL txThreadRunning;
	BOOL rxThreadRunning;
} PortThreadParameters_t;

/* Private threads declaration */

static DWORD WINAPI PortReadingLoopThread(LPVOID threadParameters);
static DWORD WINAPI PortWrittingLoopThread(LPVOID threadParameters);

/* Private global declaration */

static Message_t errorMessage = { 0 };
static MessageQueue_t messageQueue = { 0 };
static HANDLE portThreads[2] = { 0 };
static HANDLE portMessageMutex = NULL;
static PortThreadParameters_t portParameter = { 0 };

/* Functions */

static void MessageQueueInit(void) 
{
	memset(&messageQueue, 0, sizeof(messageQueue));

	portMessageMutex = NULL;
	portMessageMutex = CreateMutex(NULL, FALSE, NULL);
	assert(portMessageMutex != NULL);
}

static bool MessageQueuePush(Message_t message) 
{
	if (messageQueue.messagesInQueue == MESSAGE_QUEUE_SIZE) return false;

	messageQueue.messages[messageQueue.head] = message;

	if (messageQueue.head < (MESSAGE_QUEUE_SIZE - 1)) messageQueue.head++;
	else messageQueue.head = 0;

	messageQueue.messagesInQueue++;

	return true;
}

static bool MessageQueuePop(Message_t* pMessage)
{
	if (messageQueue.messagesInQueue == 0) return false;

	*pMessage = messageQueue.messages[messageQueue.tail];

	if (messageQueue.tail < (MESSAGE_QUEUE_SIZE - 1)) messageQueue.tail++;
	else messageQueue.tail = 0;

	messageQueue.messagesInQueue--;

	return true;
}

static void SavePortError(const char* context)
{
	DWORD error_code = GetLastError();
	char buffer[256];
	DWORD size = FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK,
		NULL, error_code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
		buffer, sizeof(buffer), NULL);
	if (size == 0) { buffer[0] = 0; }
	snprintf((char* )errorMessage.buffer, sizeof(errorMessage.buffer), "%s: %s\n", context, buffer);
	errorMessage.length = strlen(context) + strlen(buffer);
}

// Opens the specified serial port, configures its timeouts, and sets its
// state.  Returns a handle on success, or INVALID_HANDLE_VALUE on failure.
HANDLE OpenSerialPort(const char* device, COMMTIMEOUTS* pTimeouts, DCB* pState)
{
	HANDLE port = CreateFileA(device, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (port == INVALID_HANDLE_VALUE)
	{
		SavePortError(device);
		return INVALID_HANDLE_VALUE;
	}

	// Flush away any bytes previously read or written.
	BOOL success = FlushFileBuffers(port);
	if (!success)
	{
		SavePortError("Failed to flush serial port");
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
		SavePortError("Failed to set serial timeouts");
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
		SavePortError("Failed to set serial settings");
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
		SavePortError("Failed to write to port");
		return -1;
	}
	if (written != size)
	{
		SavePortError("Failed to write all bytes to port");
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
		SavePortError("Failed to read from port");
		return -1;
	}
	return received;
}

// Prepare threads for communication, and then activate them
// Wait for the threads complition for the specified provided time
void StartCommunication(HANDLE port, PortSettings_t portSettings, DWORD dwWaitMilliseconds)
{
	memset(portThreads, 0, sizeof(portThreads));
	memset(&portParameter, 0, sizeof(PortThreadParameters_t));
	MessageQueueInit();

	portParameter.portHandler = port;
	portParameter.rxThreadRunning = portParameter.txThreadRunning = TRUE;
	portParameter.settings = portSettings;

	portThreads[0] = CreateThread(NULL, 0, PortWrittingLoopThread, &portParameter, 0, NULL);
	assert(portThreads[0] != NULL);

	portThreads[1] = CreateThread(NULL, 0, PortReadingLoopThread, &portParameter, 0, NULL);
	assert(portThreads[1] != NULL);

	WaitForMultipleObjects(sizeof(portThreads) / sizeof(portThreads[0]), portThreads, TRUE, dwWaitMilliseconds);
}

// Returns windows-like stile communication objects status
DWORD CheckCommunicationRunning(void)
{
	return WaitForMultipleObjects(sizeof(portThreads) / sizeof(portThreads[0]), portThreads, TRUE, 0);
}

// Get error message
// Returns TRUE, if message loaded successfully
// Returns FALSE, if no message provided
BOOL GetPortErrorMessage(Message_t* pMessage)
{
	if (errorMessage.length == 0) return FALSE;

	*pMessage = errorMessage;
	memset(&errorMessage, 0, sizeof(errorMessage));

	return TRUE;
}

// Get message from the queue
// Returns TRUE, if message loaded successfully
// Returns FALSE, if message queue was empty
BOOL GetPortMessage(Message_t* pMessage)
{
	bool popStatus;

	WaitForSingleObject(portMessageMutex, INFINITE);
	popStatus = MessageQueuePop(pMessage);
	assert(ReleaseMutex(portMessageMutex) != 0);

	if (popStatus) return TRUE;
	return FALSE;
}

// Put new message to the queue
// By bool parameter, function will block itself, until queue is available to put new parameter
// Returns TRUE, if message reached message queue
// Returns FALSE, if message could not reach message queue
BOOL PutPortMessage(Message_t message, BOOL fullQueueBlock) 
{
	bool pushStatus;

	do {

		WaitForSingleObject(portMessageMutex, INFINITE);
		pushStatus = MessageQueuePush(message);
		assert(ReleaseMutex(portMessageMutex) != 0);

		// Sleep for 10ms before next attempt
		if ((pushStatus == false) && (fullQueueBlock == TRUE)) Sleep(10);

	} while ((pushStatus == false) && (fullQueueBlock == TRUE));

	if (pushStatus) return TRUE;
	return FALSE;
}

// Finish on-going communication by closing every thread
// Function returns zero, if communicatin finished successfully
// Function returns non-zero, if communication is already closed, or something went wrong
DWORD CloseCommunication(HANDLE port)
{
	if (CheckCommunicationRunning() != WAIT_OBJECT_0) return (1);

	CloseHandle(portThreads[0]);
	CloseHandle(portThreads[1]);
	CloseHandle(port);
	CloseHandle(portMessageMutex);

	return (0);
}

static void GetTimestampString(char* pString, size_t stringSize)
{
	char tempString[16] = "";
	SYSTEMTIME time = { 0 };

	assert(stringSize >= 16);

	GetLocalTime(&time);
	snprintf(tempString, sizeof(tempString), "[%02d:%02d:%02d.%03d]", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
	strncat_s(pString, stringSize, tempString, sizeof(tempString));
}

static DWORD WINAPI PortReadingLoopThread(LPVOID threadParameters)
{
	PortThreadParameters_t* pPortParameters = (PortThreadParameters_t*)threadParameters;

	assert(pPortParameters != NULL);

	while (1) {

		uint8_t rxBuffer[PORT_BUFFER_DATA_SIZE] = { 0 };
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
		if (neededBytesToRead == -1) break;

		char metadataBuffer[PORT_BUFFER_METADATA_SIZE] = { 0 };
		if (pPortParameters->settings.showTimeStamp) GetTimestampString(&metadataBuffer[0], sizeof(metadataBuffer));
		strncat_s(metadataBuffer, sizeof(metadataBuffer), "[RX] ", 6);

		Message_t newMessage = { 0 };
		size_t messageLength = 0;

		for (size_t i = 0; i < PORT_BUFFER_METADATA_SIZE; i++) {
			if (metadataBuffer[i] == '\0') break;
			newMessage.buffer[messageLength++] = metadataBuffer[i];
		}

		for (size_t rxByteCount = 0; rxByteCount < rxMessageLength; rxByteCount++) {
			newMessage.buffer[messageLength++] = rxBuffer[rxByteCount];
		}

		newMessage.length = messageLength;

		PutPortMessage(newMessage, TRUE);

		// Safety for checking overflow
		if (neededBytesToRead > 0) continue;
	}

	pPortParameters->rxThreadRunning = FALSE;

	return 0;
}

static DWORD WINAPI PortWrittingLoopThread(LPVOID threadParameters)
{
	PortThreadParameters_t* pPortParameters = (PortThreadParameters_t*)threadParameters;
	assert(pPortParameters != NULL);

	DWORD(*GetTxBuffer)(uint8_t * pTxBuffer, size_t txBufferSize) = pPortParameters->settings.GetTxBufferUserHandler;
	while (1) {

		uint8_t txBuffer[PORT_BUFFER_DATA_SIZE] = { 0 };
		DWORD getBufferStatus = 0;

		while (1) {
			getBufferStatus = GetTxBuffer(&txBuffer[0], sizeof(txBuffer));

			if (!pPortParameters->rxThreadRunning) break;

			if (getBufferStatus == GET_TX_BUFFER_TRANSMIT || getBufferStatus == GET_TX_BUFFER_TERMINATE) break;
		}

		if (!pPortParameters->rxThreadRunning || getBufferStatus == GET_TX_BUFFER_TERMINATE) break;

		size_t txMessageLength = strlen(txBuffer);
		assert(txMessageLength <= sizeof(txBuffer));

		// TODO: Add support for the different message formats (CR, LF, CRLF)

		if (WritePort(pPortParameters->portHandler, &txBuffer[0], txMessageLength) != 0) break;

		char metadataBuffer[PORT_BUFFER_METADATA_SIZE] = { 0 };
		if (pPortParameters->settings.showTimeStamp) GetTimestampString(&metadataBuffer[0], sizeof(metadataBuffer));
		strncat_s(metadataBuffer, sizeof(metadataBuffer), "[TX] ", 6);

		Message_t newMessage = { 0 };
		size_t messageLength = 0;

		for (size_t i = 0; i < PORT_BUFFER_METADATA_SIZE; i++) {
			if (metadataBuffer[i] == '\0') break;
			newMessage.buffer[messageLength++] = metadataBuffer[i];
		}

		for (size_t txByteCount = 0; txByteCount < txMessageLength; txByteCount++) {
			newMessage.buffer[messageLength++] = txBuffer[txByteCount];
		}

		newMessage.length = messageLength;

		PutPortMessage(newMessage, TRUE);

		continue;
	}

	pPortParameters->txThreadRunning = FALSE;

	return 0;
}