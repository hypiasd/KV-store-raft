#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <unistd.h>

int main()
{
    // 创建套接字
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1)
    {
        std::cerr << "Failed to create socket" << std::endl;
        return -1;
    }

    // 定义服务器地址和端口
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(9000);

    // 绑定套接字到地址和端口
    if (bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) == -1)
    {
        std::cerr << "Failed to bind socket" << std::endl;
        return -1;
    }

    // 开始监听连接请求
    if (listen(serverSocket, 5) == -1)
    {
        std::cerr << "Failed to listen on socket" << std::endl;
        return -1;
    }

    std::cout << "Server is listening on port 8080..." << std::endl;

    while (true)
    {
        // 接受客户端连接
        sockaddr_in clientAddress{};
        socklen_t clientAddressLength = sizeof(clientAddress);
        int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress, &clientAddressLength);
        if (clientSocket == -1)
        {
            std::cerr << "Failed to accept client connection" << std::endl;
            continue;
        }

        // 从客户端接收数据
        char buffer[1024];
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead == -1)
        {
            std::cerr << "Failed to receive data from client" << std::endl;
            close(clientSocket);
            continue;
        }

        // 处理接收到的数据
        std::cout << "Received data from client: " << buffer << std::endl;

        // 发送响应给客户端
        const char *response = "Hello from server!";
        ssize_t bytesSent = send(clientSocket, response, strlen(response), 0);
        if (bytesSent == -1)
        {
            std::cerr << "Failed to send response to client" << std::endl;
        }

        // 关闭客户端套接字
        close(clientSocket);
    }

    // 关闭服务器套接字
    close(serverSocket);

    return 0;
}
