#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "tcp_server.h"
#include "Epoll.h"

Server::Server(int port)
{
    // 创建socket
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0)
    {
        std::cerr << "Failed to create socket." << std::endl;
        return;
    }

    // 绑定端口和IP地址
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port); // 8080是端口号，可以更改为需要的端口号
    server_address.sin_addr.s_addr = INADDR_ANY;

    int on = 1;
    if ((setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)
    {
        std::cerr << "setsockopt failed." << std::endl;
        return;
    }
    if (bind(server_socket_, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        std::cerr << "Failed to bind socket." << std::endl;
        return;
    }
    std::cout << "server create on " << port << std::endl;
}

Server::~Server()
{
    close(server_socket_);
}

void Server::start()
{
    // 监听 socket
    if (listen(server_socket_, SOMAXCONN) == -1)
    {
        std::cerr << "Failed to listen on socket: " << std::strerror(errno) << std::endl;
        return;
    }

    // 创建 epoll 实例
    Epoll epoll(1000);

    // 添加 server_fd 到 epoll 实例中
    epoll.Add(server_socket_, EPOLLET);

    // 开始监听
    while (true)
    {
        int num_events = epoll.Wait();
        std::cout << "hh" << std::endl;

        // 处理每个事件
        for (int i = 0; i < num_events; ++i)
        {
            int fd = epoll.events_arr[i].data.fd;
            if (fd == server_socket_)
            {
                // 有新的连接请求，接受连接并将新的 socket 添加到 epoll 实例中
                sockaddr_in client_addr = {0};
                socklen_t client_addr_len = sizeof(client_addr);
                int client_fd = accept(server_socket_, (sockaddr *)&client_addr, &client_addr_len);

                if (client_fd == -1)
                {
                    std::cerr << "Failed to accept connection: " << std::endl;
                    continue;
                }
                // 将新的 socket 添加到 epoll 实例中
                epoll.Add(client_fd, EPOLLET);
            }
            else
            {
                // 不是新的连接请求，就读取数据
                char buf[1024];
                ssize_t nread = read(fd, buf, sizeof(buf));
                if (nread == -1)
                {
                    std::cerr << "Failed to read file: " << std::strerror(errno) << std::endl;
                    close(fd);
                    return;
                }
                handle_client(fd, buf);
            }
        }
    }
}