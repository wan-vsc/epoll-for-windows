#ifndef UTILITY_WIN_H_INCLUDED
#define UTILITY_WIN_H_INCLUDED

// Raise the Winsock fd_set limit before including the socket headers
// (default is 64; the Linux original used an epoll size of 5000).
#ifndef FD_SETSIZE
#define FD_SETSIZE 512
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <iostream>
#include <list>
#include <vector>
#include <atomic>
#include <thread>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

/**********************   macro definition **************************/
// server ip
#define SERVER_IP   "127.0.0.1"
// server port
#define SERVER_PORT 8888
// maximum clients select() can watch (must stay <= FD_SETSIZE)
#define MAX_CLIENT  500
// message buffer size (same value as the Linux original: 0xFFFF)
#define BUF_SIZE    0xFFFF

#define SERVER_WELCOME  "Welcome you join  to the chat room! Your chat ID is: Client #%llu"
#define SERVER_MESSAGE  "ClientID %llu say >> %s"

// exit command (case-insensitive)
#define EXIT_CMD "EXIT"
#define CAUTION  "There is only one int the char room!"

// clients_list holds every connected client socket.
// (One translation unit per executable, so defining it here is safe.)
list<SOCKET> clients_list;

/**********************   helper functions **************************/

// Print a Winsock error with the numeric WSAGetLastError() code.
inline void printWsaError(const char *tag)
{
    printf("%s failed, WSAGetLastError = %d\n", tag, (int)WSAGetLastError());
}

// Send a NUL-terminated text message (including the terminating '\0',
// so the receiving side can printf it as a C string). Returns bytes sent.
inline int sendText(SOCKET s, const char *text)
{
    int len = (int)strlen(text) + 1;
    int sent = send(s, text, len, 0);
    if (sent == SOCKET_ERROR) {
        printWsaError("send");
    }
    return sent;
}

// Case-insensitive prefix compare (Linux original used strncasecmp).
inline int strncasecmp_win(const char *a, const char *b, size_t n)
{
    return _strnicmp(a, b, n);
}

/**********************   server side helpers **************************/

// Handle data arriving from one connected client.
// Return value mirrors the Linux original: number of bytes received
// (0 / SOCKET_ERROR means the peer closed or an error happened).
inline int handleClientMessage(SOCKET clientfd)
{
    char buf[BUF_SIZE];
    char message[BUF_SIZE];
    memset(buf, 0, BUF_SIZE);
    memset(message, 0, BUF_SIZE);

    printf("read from client(clientID = %llu)\n", (unsigned long long)clientfd);
    int len = recv(clientfd, buf, BUF_SIZE, 0);

    if (len <= 0) {
        // 0 means graceful close; SOCKET_ERROR means a socket error.
        closesocket(clientfd);
        clients_list.remove(clientfd);
        printf("ClientID = %llu closed.\n now there are %d client in the char room\n",
               (unsigned long long)clientfd, (int)clients_list.size());
    } else {
        // The sender is the only person in the room: echo the caution text.
        if (clients_list.size() == 1) {
            sendText(clientfd, CAUTION);
            return len;
        }

        // Format the message and broadcast it to every OTHER client.
        sprintf_s(message, BUF_SIZE, SERVER_MESSAGE, (unsigned long long)clientfd, buf);

        for (list<SOCKET>::iterator it = clients_list.begin();
             it != clients_list.end(); ++it) {
            if (*it != clientfd) {
                if (send(*it, message, (int)strlen(message) + 1, 0) == SOCKET_ERROR) {
                    printWsaError("broadcast send");
                }
            }
        }
    }
    return len;
}

#endif // UTILITY_WIN_H_INCLUDED
