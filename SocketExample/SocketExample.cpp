#include "stdafx.h"
#include "Winsock2.h" // necessary for sockets, Windows.h is not needed.
#include "mswsock.h"
#include "process.h"  // necessary for threading
//
// Global variables
//
TCHAR CommandBuf[81];
HANDLE hCommandGot;       // event "the user has typed a command"
HANDLE hStopCommandGot;   // event "the main thread has recognized that it was the stop command"
HANDLE hCommandProcessed; // event "the main thread has finished the processing of command"
HANDLE hReadKeyboard;     // keyboard reading thread handle
HANDLE hStdIn;			  // stdin standard input stream handle
WSADATA WsaData;          // filled during Winsock initialization 
DWORD Error;
SOCKET hClientSocket = INVALID_SOCKET;
sockaddr_in ClientSocketInfo;
HANDLE hReceiveNet;       // TCP/IP info reading thread handle
BOOL SocketError;
BOOL isConnected;
BOOL isStarted;
//
// Prototypes
//
unsigned int __stdcall ReadKeyboard(void* pArguments);
unsigned int __stdcall ReceiveNet(void* pArguments);
int bufWcsCompare(char *buffer, wchar_t *str);
void sendMsg(SOCKET s, wchar_t *str);
void parseStringFromBuf(char *dst, char *buffer, int *parsedBytes);

//****************************************************************************************************************
//                                 MAIN THREAD
//****************************************************************************************************************
int _tmain(int argc, _TCHAR* argv[])
{
	//
	// Initializations for multithreading
	//
	if (!(hCommandGot = CreateEvent(NULL, TRUE, FALSE, NULL)) ||
		!(hStopCommandGot = CreateEvent(NULL, TRUE, FALSE, NULL)) ||
		!(hCommandProcessed = CreateEvent(NULL, TRUE, TRUE, NULL)))
	{
		_tprintf(_T("CreateEvent() failed, error %d\n"), GetLastError());
		return 1;
	}
	//
	// Prepare keyboard, start the thread
	//
	hStdIn = GetStdHandle(STD_INPUT_HANDLE);
	if (hStdIn == INVALID_HANDLE_VALUE)
	{
		_tprintf(_T("GetStdHandle() failed, error %d\n"), GetLastError());
		return 1;
	}
	if (!SetConsoleMode(hStdIn, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT))
	{
		_tprintf(_T("SetConsoleMode() failed, error %d\n"), GetLastError());
		return 1;
	}
	if (!(hReadKeyboard = (HANDLE)_beginthreadex(NULL, 0, &ReadKeyboard, NULL, 0, NULL)))
	{ 
		_tprintf(_T("Unable to create keyboard thread\n"));
		return 1;
	}
	//
	// Initializations for socket
	//
	if (Error = WSAStartup(MAKEWORD(2, 0), &WsaData)) // Initialize Windows socket support
	{
		_tprintf(_T("WSAStartup() failed, error %d\n"), Error);
		SocketError = TRUE;
	}
	//else if ((hClientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET)
	//{
	//	_tprintf(_T("socket() failed, error %d\n"), WSAGetLastError());
	//	SocketError = TRUE;
	//}

	// Main processing loop
	//
	while (TRUE)
	{
		if (WaitForSingleObject(hCommandGot, INFINITE) != WAIT_OBJECT_0)
		{ // Wait until the command has arrived (i.e. until CommandGot is signaled)
			_tprintf(_T("WaitForSingleObject() failed, error %d\n"), GetLastError());
			goto out;
		}
		ResetEvent(hCommandGot); // CommandGot back to unsignaled
		if (!_tcsicmp(CommandBuf, _T("connect"))) {
			if (isConnected) {
				_tprintf(_T("You are already connected to the emulator\n"));
			}
			else {


				//
				// Connect client to server
				//
				if (!SocketError)
				{
					ClientSocketInfo.sin_family = AF_INET;
					ClientSocketInfo.sin_addr.s_addr = inet_addr("127.0.0.1");
					ClientSocketInfo.sin_port = htons(1234);  // port number is selected just for example
					if ((hClientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET)
					{
						_tprintf(_T("socket() failed, error %d\n"), WSAGetLastError());
						SocketError = TRUE;
					}
					else if (connect(hClientSocket, (SOCKADDR*)&ClientSocketInfo, sizeof(ClientSocketInfo)) == SOCKET_ERROR)
					{
						_tprintf(_T("Unable to connect to server, error %d\n"), WSAGetLastError());
						SocketError = TRUE;
					}
				}
				//
				// Start net thread
				//
				if (!SocketError)
				{
					if (!(hReceiveNet = (HANDLE)_beginthreadex(NULL, 0, &ReceiveNet, NULL, 0, NULL)))
					{
						_tprintf(_T("Unable to create socket receiving thread\n"));
						goto out;
					}
				}
			}
			SetEvent(hCommandProcessed);
		}
		else if (!_tcsicmp(CommandBuf, _T("start"))) {
			if (isConnected) {
				sendMsg(hClientSocket, L"Start");
				isStarted = TRUE;
				_tprintf(_T("client sent: %s\n"), L"Start");
			}
			else {
				_tprintf(_T("Can't start as you're not connected to the emulator\n"));
			}
			SetEvent(hCommandProcessed);
		}
		else if (!_tcsicmp(CommandBuf, _T("stop"))) {
			if (isConnected) {
				sendMsg(hClientSocket, L"Stop");
				isStarted = FALSE;
				_tprintf(_T("client sent: %s\n"), L"Stop");

			}
			else {
				_tprintf(_T("Can't stop as you're not connected to the emulator\n"));
			}
			SetEvent(hCommandProcessed);

		}
		else if (!_tcsicmp(CommandBuf, _T("exit"))) // Case-insensitive comparation
		{
			SetEvent(hStopCommandGot); // To force the other threads to quit
			break;
		}
		else
		{
			_tprintf(_T("Command \"%s\" not recognized\n"), CommandBuf);
			SetEvent(hCommandProcessed); // To allow the keyboard reading thread to continue
		}
	}
	//
	// Shut down
	//
out:
	if (hReadKeyboard)
	{
		WaitForSingleObject(hReadKeyboard, INFINITE); // Wait until the end of keyboard thread
		CloseHandle(hReadKeyboard);
	}
	if (hReceiveNet)
	{
		WaitForSingleObject(hReceiveNet, INFINITE); // Wait until the end of receive thread
		CloseHandle(hReceiveNet);
	}
	if (hClientSocket != INVALID_SOCKET)
	{
		if (shutdown(hClientSocket, SD_RECEIVE) == SOCKET_ERROR)
		{
			if ((Error = WSAGetLastError()) != WSAENOTCONN) // WSAENOTCONN means that the connection was not established,
															// so the shut down was senseless
				_tprintf(_T("shutdown() failed, error %d\n"), WSAGetLastError());
		}
		closesocket(hClientSocket);
	}
	WSACleanup(); // clean Windows sockets support
	CloseHandle(hStopCommandGot);
	CloseHandle(hCommandGot);
	CloseHandle(hCommandProcessed);
	return 0;
}
//**************************************************************************************************************
//                          KEYBOARD READING THREAD
//**************************************************************************************************************
unsigned int __stdcall ReadKeyboard(void* pArguments)
{
	DWORD nReadChars;
	HANDLE KeyboardEvents[2];
	KeyboardEvents[1] = hCommandProcessed; 
	KeyboardEvents[0] = hStopCommandGot;
	DWORD WaitResult;
	//
	// Reading loop
	//
	while (TRUE)
	{
		WaitResult = WaitForMultipleObjects(2, KeyboardEvents,
			FALSE, // wait until one of the events becomes signaled
			INFINITE);
		   // Waiting until hCommandProcessed or hStopCommandGot becomes signaled. Initially hCommandProcessed
		   // is signaled, so at the beginning WaitForMultipleObjects() returns immediately with WaitResult equal
		   // with WAIT_OBJECT_0 + 1.
		if (WaitResult == WAIT_OBJECT_0)
			return 0;  // Stop command, i.e. hStopCommandGot is signaled
		else if (WaitResult == WAIT_OBJECT_0 + 1)
		{ // If the signaled event is hCommandProcessed, the WaitResult is WAIT_OBJECT_0 + 1
			_tprintf(_T("Insert command\n"));
			if (!ReadConsole(hStdIn, CommandBuf, 80, &nReadChars, NULL)) 
			{ // The problem is that when we already are in this function, the only way to leave it
			  // is to type something and then press ENTER. So we cannot step into this function at any moment.
			  // WaitForMultipleObjects() prevents it.
				_tprintf(_T("ReadConsole() failed, error %d\n"), GetLastError()); 
				return 1; 
			} 
			CommandBuf[nReadChars - 2] = 0; // The command in buf ends with "\r\n", we have to get rid of them
			ResetEvent(hCommandProcessed); 
			// Set hCommandProcessed to non-signaled. Therefore WaitForMultipleObjects() blocks the keyboard thread.
			// When the main thread has ended the analyzing of command, it sets hCommandprocessed or hStopCommandGot
			// to signaled and the keyboard thread can continue.
			SetEvent(hCommandGot); 
			// Set hCommandGot event to signaled. Due to that WaitForSingleObject() in the main thread
			// returns, the waiting stops and the analyzing of inserted command may begin
		}
		else
		{	// waiting failed
			_tprintf(_T("WaitForMultipleObjects()failed, error %d\n"), GetLastError());
			return 1;
		}
	}
	return 0;
}
//********************************************************************************************************************
//                          TCP/IP INFO RECEIVING THREAD
//********************************************************************************************************************
unsigned int __stdcall ReceiveNet(void* pArguments)
{
	//
	// Preparations
	//
	WSABUF DataBuf;  // Buffer for received data is a structure
	char ArrayInBuf[2048];
	DataBuf.buf = &ArrayInBuf[0];
	DataBuf.len = 2048;
	DWORD nReceivedBytes = 0, ReceiveFlags = 0;
	HANDLE NetEvents[2];
	NetEvents[0] = hStopCommandGot;
	WSAOVERLAPPED Overlapped;
	memset(&Overlapped, 0, sizeof Overlapped);
	Overlapped.hEvent = NetEvents[1] = WSACreateEvent(); // manual and nonsignaled
	DWORD Result, Error;
	//
	// Receiving loop
	//
	while (TRUE)
	{
		Result = WSARecv(hClientSocket,
						  &DataBuf,
						  1,  // no comments here
						  &nReceivedBytes, 
						  &ReceiveFlags, // no comments here
						  &Overlapped, 
						  NULL);  // no comments here
		if (Result == SOCKET_ERROR)					 
		{  // Returned with socket error, let us examine why
			if ((Error = WSAGetLastError()) != WSA_IO_PENDING)
			{  // Unable to continue, for example because the server has closed the connection
				_tprintf(_T("WSARecv() failed, error %d\n"), Error);
				goto out;
			}
			DWORD WaitResult = WSAWaitForMultipleEvents(2, NetEvents, FALSE, WSA_INFINITE, FALSE); // wait for data
			switch (WaitResult) // analyse why the waiting ended
			{
			case WAIT_OBJECT_0:
				// Waiting stopped because hStopCommandGot has become signaled, i.e. the user has decided to exit
				goto out; 
			case WAIT_OBJECT_0 + 1:
				// Waiting stopped because Overlapped.hEvent is now signaled, i.e. the receiving operation has ended. 
			    // Now we have to see how many bytes we have got.
				WSAResetEvent(NetEvents[1]); // to be ready for the next data package
				if (WSAGetOverlappedResult(hClientSocket, &Overlapped, &nReceivedBytes, FALSE, &ReceiveFlags))
				{
		   		   _tprintf(_T("%d bytes received\n"), nReceivedBytes);
				   _tprintf(_T("client received: %s\n"), DataBuf.buf + sizeof(int));
				   char ArrayOutBuf[2048];
				   if (bufWcsCompare(DataBuf.buf, L"Identify") == 0) {
					   sendMsg(hClientSocket, L"coursework");
					   _tprintf(_T("client sent: %s\n"), L"courswowrk");
				   }
				   else if (bufWcsCompare(DataBuf.buf, L"Accepted") == 0) {
					   isConnected = TRUE;
				   }
				   else if (isConnected && isStarted) {
					   
					   int packageLength;
					   int numChannels;
					   int parsedBytes = 0;
					   memcpy(&packageLength, DataBuf.buf + parsedBytes, sizeof(int));
					   parsedBytes += sizeof(int);
					   memcpy(&numChannels, DataBuf.buf + parsedBytes, sizeof(int));
					   parsedBytes += sizeof(int);
					   
					   for (int i = 0; i < numChannels; i++) {
						   int measurementsPoints;
						   memcpy(&measurementsPoints, DataBuf.buf + parsedBytes, sizeof(int));
						   parsedBytes += sizeof(int);
						   char channelName[128];
						   parseStringFromBuf(channelName, DataBuf.buf, &parsedBytes);
						   printf("%s:\n", channelName);
						   for (int j = 0; j < measurementsPoints; j++) {
							   char measurementName[128];
							   parseStringFromBuf(measurementName, DataBuf.buf, &parsedBytes);
							   printf("%s:", measurementName);
							   if (strcmp(measurementName, "Level") == 0) {
								   int measurement;
								   memcpy(&measurement, DataBuf.buf + parsedBytes, 4);
								   parsedBytes += 4;
								   _tprintf(_T("%d\n"), measurement);
							   }
							   else {
								   double measurement;
								   memcpy(&measurement, DataBuf.buf + parsedBytes, 8);
								   parsedBytes += 8;
								   _tprintf(_T("%lf\n"), measurement);
							   }

						   }
					   }
					   sendMsg(hClientSocket, L"Ready");
					   
				   }

				   break;
			    }
			    else
			    {	// Fatal problems
		  		   _tprintf(_T("WSAGetOverlappedResult() failed, error %d\n"), GetLastError());
				   goto out;
			    }
			default: // Fatal problems
				_tprintf(_T("WSAWaitForMultipleEvents() failed, error %d\n"), WSAGetLastError());
				goto out;
			}
		}
		else
		{  // Returned immediately without socket error
			if (!nReceivedBytes)
			{  // When the receiving function has read nothing and returned immediately, the connection is off  
				_tprintf(_T("Server has closed the connection\n"));
				isConnected = FALSE;
				if (hClientSocket != INVALID_SOCKET)
				{
					if (shutdown(hClientSocket, SD_RECEIVE) == SOCKET_ERROR)
					{
						if ((Error = WSAGetLastError()) != WSAENOTCONN) // WSAENOTCONN means that the connection was not established,
																		// so the shut down was senseless
							_tprintf(_T("shutdown() failed, error %d\n"), WSAGetLastError());
					}
					//closesocket(hClientSocket);
				}
				goto out;
			}
			else
			{
			 _tprintf(_T("%d bytes received\n"), nReceivedBytes);
			          // Here should follow the processing of received data
			 // TODO: Add smth here
			}
		}
	}
out:
	WSACloseEvent(NetEvents[1]);
	return 0;
}

int bufWcsCompare(char *buffer, wchar_t *str)
{
	char temp_buff[2048];
	//wchar_t *identifyStr = L"Identify";
	memcpy(temp_buff, str, (wcslen(str) + 1) * sizeof(wchar_t));
	return strcmp(buffer + sizeof(int), temp_buff);
	//delete temp_buff;
}

void sendMsg(SOCKET s, wchar_t * str)
{
	char temp_buff[2048];
	int messageLength = sizeof(int) + (wcslen(str) + 1) * sizeof(wchar_t);
	memcpy(temp_buff, &messageLength, sizeof(int));
	memcpy(temp_buff + sizeof(int), str, (wcslen(str) + 1) * sizeof(wchar_t));
	send(s, temp_buff, messageLength, 0);

}

void parseStringFromBuf(char *dst, char *buffer, int *parsedBytes)
{
	int strLength = 0;
	while (*(buffer + *parsedBytes + strLength) != '\0') {
		strLength++;
	}
	memcpy(dst, buffer + *parsedBytes, strLength + 1);
	*parsedBytes += strLength + 1;
}

