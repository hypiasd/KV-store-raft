#ifndef __TCP_SERVER_H__
#define __TCP_SERVER_H__

class Server
{
public:
    Server(int port);
    ~Server();
    void start();
    virtual void handle_client(int client_socket, const char *buffer) = 0;

protected:
    int port_;
    int server_socket_;
};

#endif // __TCP_SERVER_H__