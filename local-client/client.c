#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP "192.168.72.94"
#define SERVER_PORT 6666

#define KEEPALIVE_IDLE     120  // 2分钟
#define KEEPALIVE_INTERVAL 30   // 30秒
#define KEEPALIVE_COUNT    4    // 4次

void set_keepalive(SOCKET sock) {
    int optval = 1;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&optval, sizeof(optval));

    optval = KEEPALIVE_IDLE;
    setsockopt(sock, IPPROTO_TCP, KEEPALIVE_IDLE, (char *)&optval, sizeof(optval));

    optval = KEEPALIVE_INTERVAL;
    setsockopt(sock, IPPROTO_TCP, KEEPALIVE_IDLE, (char *)&optval, sizeof(optval));

    optval = KEEPALIVE_COUNT;
    setsockopt(sock, IPPROTO_TCP, KEEPALIVE_COUNT, (char *)&optval, sizeof(optval));
}

int main() {
    WSADATA wsaData;
    SOCKET sock = INVALID_SOCKET;
    struct sockaddr_in server;
    char sendbuf[512] = "Hello from client";
    char recvbuf[512];
    int result, recvbuflen = 512;

    // Initialize Winsock
    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        printf("WSAStartup failed: %d\n", result);
        return 1;
    }

    // Create a SOCKET for connecting to server
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Set Keep-Alive options
    set_keepalive(sock);

    // Setup the server address structure
    server.sin_family = AF_INET;
    server.sin_port = htons(SERVER_PORT);
    server.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);

    // Connect to server
    result = connect(sock, (struct sockaddr *)&server, sizeof(server));
    if (result == SOCKET_ERROR) {
        printf("connect failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    while (1)
    {
        // Send data to server
        result = send(sock, sendbuf, (int)strlen(sendbuf), 0);
        if (result == SOCKET_ERROR) {
            printf("send failed: %d\n", WSAGetLastError());
            closesocket(sock);
            WSACleanup();
            return 1;
        }
        printf("Bytes Sent: %d\n", result);

        // Receive data from server
        result = recv(sock, recvbuf, recvbuflen, 0);
        if (result > 0) {
            printf("Bytes received: %d\n", result);
            recvbuf[result] = '\0';  // Null-terminate the received data
            printf("Received: %s\n", recvbuf);
        } else if (result == 0) {
            printf("Connection closed\n");
        } else {
            printf("recv failed: %d\n", WSAGetLastError());
        }
        
        Sleep(500);
    }

    // Cleanup
    closesocket(sock);
    WSACleanup();

    return 0;
}
