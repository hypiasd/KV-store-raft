#ifndef __TCP_SERVER_H__
#define __TCP_SERVER_H__

class Server
{
public:
    Server(int port);
    ~Server();
    virtual void start(); // 开启服务器监听
    virtual void handle_client(int client_socket, const char *buffer) = 0;

protected:
    int port_;
    int server_socket_;
};

#endif // __TCP_SERVER_H__