// epoll_client_win.cpp
// Windows-native port of the Linux epoll chat-room client.
//
// Porting map:
//   fork() + pipe() (child reads keyboard, parent watches socket+pipe)
//       ->  an std::thread reads the keyboard and sends directly,
//           while the main thread select()s on the server socket.
//   read(pipefd) / close(fd)  ->  send() / closesocket()
//
// Usage: start epoll_server_win.exe first, then run this program.
// Type messages and press Enter; type EXIT (any case) to leave.

#include "utility_win.h"

static atomic<bool> g_running(true);
static SOCKET g_sock = INVALID_SOCKET;

// Keyboard reader thread (replaces the Linux fork() child process).
static void KeyboardThread()
{
    char line[BUF_SIZE];

    while (g_running)
    {
        memset(line, 0, BUF_SIZE);
        if (fgets(line, BUF_SIZE, stdin) == NULL) {
            // stdin closed (Ctrl+Z on Windows, or piped EOF): leave like EXIT.
            g_running = false;
            shutdown(g_sock, SD_BOTH);
            break;
        }
        // Strip trailing CR/LF.
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (n == 0) {
            continue; // ignore empty lines
        }

        // Type EXIT to leave the chat room (case-insensitive).
        if (strncasecmp_win(line, EXIT_CMD, strlen(EXIT_CMD)) == 0) {
            g_running = false;
            // Wake the main thread's select() immediately.
            shutdown(g_sock, SD_BOTH);
            break;
        }

        if (send(g_sock, line, (int)strlen(line), 0) == SOCKET_ERROR) {
            printWsaError("send");
            g_running = false;
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    // Make printf output immediate even when stdout is redirected to a pipe/file.
    setvbuf(stdout, NULL, _IONBF, 0);

    // 1. Initialise Winsock.
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed, error = %d\n", (int)WSAGetLastError());
        return -1;
    }

    // 2. Server address (IP + port).
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // 3. Create the socket.
    g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_sock == INVALID_SOCKET) {
        printWsaError("socket");
        WSACleanup();
        return -1;
    }

    // 4. Connect to the server.
    if (connect(g_sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printWsaError("connect (is the server running?)");
        closesocket(g_sock);
        WSACleanup();
        return -1;
    }
    printf("Connected to chat server %s:%d\n", SERVER_IP, SERVER_PORT);
    printf("Please input 'exit' to exit the chat room\n");

    // 5. Start the keyboard reader thread.
    thread inputThread(KeyboardThread);
    inputThread.detach();

    char message[BUF_SIZE];

    // 6. Main loop: select() watches the server socket (was epoll_wait).
    while (g_running)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(g_sock, &readSet);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500 * 1000;

        int ready = select(0, &readSet, NULL, NULL, &tv);
        if (ready == SOCKET_ERROR) {
            // Happens after our own shutdown() on EXIT; simply leave.
            if (WSAGetLastError() == WSAEINTR || !g_running) break;
            printWsaError("select");
            break;
        }
        if (ready == 0) {
            continue;
        }

        if (FD_ISSET(g_sock, &readSet))
        {
            memset(message, 0, BUF_SIZE);
            int ret = recv(g_sock, message, BUF_SIZE, 0);

            if (ret == 0) {
                printf("Server closed connection: %llu\n", (unsigned long long)g_sock);
                g_running = false;
            } else if (ret < 0) {
                // After our own EXIT path shuts the socket down this is expected.
                if (g_running) printWsaError("recv");
                g_running = false;
            } else {
                printf("%s\n", message);
            }
        }
    }

    // 7. Cleanup.
    closesocket(g_sock);
    WSACleanup();
    printf("client exited.\n");
    return 0;
}
