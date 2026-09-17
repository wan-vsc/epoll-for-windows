// epoll_server_win.cpp
// Windows-native port of the Linux epoll chat-room server.
//
// Porting map (Linux epoll -> Windows Winsock):
//   epoll_create/epoll_ctl/epoll_wait  ->  select() over an fd_set
//   close(fd)                          ->  closesocket(fd)
//   (blocking sockets; select tells us when a socket is readable)
//
// Protocol stays identical to the Linux original, so it interoperates
// with the ported client (and even with a real Linux client).

#include "utility_win.h"

static atomic<bool> g_running(true);

// Ctrl+C / window-close: stop the main select loop cleanly.
static BOOL WINAPI ConsoleHandler(DWORD ctrlType)
{
    (void)ctrlType;
    g_running = false;
    return TRUE;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    // Make printf output immediate even when stdout is redirected to a pipe/file.
    setvbuf(stdout, NULL, _IONBF, 0);

    // 1. Initialise Winsock 2.2.
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed, error = %d\n", (int)WSAGetLastError());
        return -1;
    }
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    // 2. Fill in the server address (IP + port).
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // 3. Create the listening socket.
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        printWsaError("listener socket");
        WSACleanup();
        return -1;
    }
    BOOL reuse = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
    printf("listen socket created\n");

    // 4. Bind the address.
    if (bind(listener, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printWsaError("bind");
        closesocket(listener);
        WSACleanup();
        return -1;
    }

    // 5. Listen.
    if (listen(listener, 5) == SOCKET_ERROR) {
        printWsaError("listen");
        closesocket(listener);
        WSACleanup();
        return -1;
    }
    printf("Start to listen: %s:%d\n", SERVER_IP, SERVER_PORT);
    printf("Press Ctrl+C to stop the server.\n");

    // 6. Main loop: select() plays the role of epoll_wait().
    while (g_running)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listener, &readSet);
        for (list<SOCKET>::iterator it = clients_list.begin(); it != clients_list.end(); ++it) {
            FD_SET(*it, &readSet);
        }

        // 500 ms timeout so the loop can notice g_running (Ctrl+C) promptly.
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500 * 1000;

        int readyCount = select(0, &readSet, NULL, NULL, &tv);
        if (readyCount == SOCKET_ERROR) {
            printWsaError("select");
            break;
        }
        if (readyCount == 0) {
            continue; // timeout, nothing readable
        }

        // New incoming connection?
        if (FD_ISSET(listener, &readSet))
        {
            struct sockaddr_in clientAddr;
            int clientLen = sizeof(clientAddr);
            SOCKET clientfd = accept(listener, (struct sockaddr *)&clientAddr, &clientLen);
            if (clientfd == INVALID_SOCKET) {
                printWsaError("accept");
            } else {
                printf("client connection from: %s : %u(IP : port), clientfd = %llu\n",
                       inet_ntoa(clientAddr.sin_addr),
                       ntohs(clientAddr.sin_port),
                       (unsigned long long)clientfd);

                clients_list.push_back(clientfd); // register the new client
                printf("Add new clientfd = %llu to watch list\n", (unsigned long long)clientfd);
                printf("Now there are %d clients int the chat room\n", (int)clients_list.size());

                // Send the welcome message.
                char message[BUF_SIZE];
                memset(message, 0, BUF_SIZE);
                sprintf_s(message, BUF_SIZE, SERVER_WELCOME, (unsigned long long)clientfd);
                sendText(clientfd, message);
            }
        }

        // Existing client readable? Take a snapshot because the list may
        // shrink inside handleClientMessage when a client disconnects.
        vector<SOCKET> snapshot(clients_list.begin(), clients_list.end());
        for (size_t i = 0; i < snapshot.size(); ++i)
        {
            SOCKET s = snapshot[i];
            if (FD_ISSET(s, &readSet)) {
                if (handleClientMessage(s) == SOCKET_ERROR) {
                    printWsaError("handle client");
                }
            }
        }
    }

    // 7. Cleanup.
    for (list<SOCKET>::iterator it = clients_list.begin(); it != clients_list.end(); ++it) {
        closesocket(*it);
    }
    clients_list.clear();
    closesocket(listener);
    WSACleanup();
    printf("server stopped.\n");
    return 0;
}
