#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

int main()
{
    // 创建客户端套接字
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);

    // 设置服务器的 IP 地址和端口号
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(12340); // 服务器端口号
    serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");

    // 连接到服务器
    int connectionResult = connect(clientSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    if (connectionResult < 0)
    {
        std::cerr << "连接服务器失败" << std::endl;
        return -1;
    }

    // 向服务器发送信息
    const char *message = "*2\r\n$3\r\nGET\r\n$7\r\nCS06142\r\n";
    // const char *message = "*4\r\n$3\r\nSET\r\n$7\r\nCS06142\r\n$5\r\nCloud\r\n$9\r\nComputing\r\n";
    // const char *message = "*3\r\n$3\r\nDEL\r\n$7\r\nCS06142\r\n$5\r\nCS162\r\n";
    int bytesSent = send(clientSocket, message, strlen(message), 0);
    if (bytesSent < 0)
    {
        std::cerr << "发送消息失败" << std::endl;
        close(clientSocket);
        return -1;
    }
    const int bufferSize = 1024;
    char buffer[bufferSize];
    std::string receivedCode;

    while (true)
    {
        int bytesRead = recv(clientSocket, buffer, bufferSize - 1, 0);
        if (bytesRead == -1)
        {
            std::cerr << "Error while receiving data" << std::endl;
            break;
        }
        else if (bytesRead == 0)
        {
            // 连接已关闭
            break;
        }

        buffer[bytesRead] = '\0';
        receivedCode += buffer;
    }
    std::cout << receivedCode;
    // 关闭客户端套接字
    close(clientSocket);

    return 0;
}
