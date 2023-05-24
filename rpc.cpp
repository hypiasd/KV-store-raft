#include "rpc.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

Rpc::Rpc(int id, std::vector<int> &info, size_t num_thread) : Server(info[id])
{
    // 创建线程池
    thread_pool_ = new ThreadPool(num_thread);
    id_ = id;
    nodes_ = info;
    num_nodes_ = nodes_.size();
    // // 创建client_socket
    // for (int i = 0; i < num_nodes_; i++)
    // {
    //     if (i == id)
    //         continue;
    //     client_socket_[i] = socket(AF_INET, SOCK_STREAM, 0);
    //     // 设置服务器的 IP 地址和端口号
    //     sockaddr_in serverAddress;
    //     serverAddress.sin_family = AF_INET;
    //     serverAddress.sin_port = htons(nodes_[i]); // 服务器端口号
    //     serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");

    //     // 连接到服务器
    //     int connectionResult = connect(client_socket_[i], (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    //     if (connectionResult < 0)
    //     {
    //         std::cerr << "连接服务器失败" << std::endl;
    //     }
    // }
}

Rpc::~Rpc()
{
    for (int i = 0; i < num_nodes_; i++)
    {
        if (i == id_)
            continue;
        close(client_socket_[i]);
    };
    delete thread_pool_;
}

void Rpc::start()
{
    std::cout << "run rpc on " << nodes_[id_] << std::endl;
    Server::start();
}

void Rpc::handle_client(int client_socket, const char *buffer)
{
    thread_pool_->enqueue([this, buffer, client_socket]()
                          { handle_all(client_socket, buffer); });
}

void Rpc::handle_all(int client_socket, const char *buffer)
{
    if (decode(buffer))
        handle();
    sendmsg(client_socket);
}

void Rpc::sendmsg(int client_socket)
{
    if (send(client_socket, response_str_.c_str(), response_str_.size(), 0) == -1)
    {
        perror("Send response to client failed");
    }
    close(client_socket);
}

bool Rpc::decode(const char *buffer)
{
    if (buffer[0] != '*')
    {
        info_ = "-ERROR\r\n";
        std::cerr << "format is wrong!!!";
        return false;
    }
    std::string str(buffer);
    int pos1 = str.find("\r\n");
    int num_string = std::atoi(str.substr(1, pos1 - 1).c_str());
    int start = pos1 + 1;
    for (int i = 0; i < num_string; i++)
    {
        int pos2 = str.find("$", start);
        start = pos2 + 1;
        int pos3 = str.find("\r\n", pos2);
        int len = std::atoi(str.substr(pos2 + 1, pos3 - pos2 - 1).c_str());
        if (i == 0)
        {
            func_name_ = str.substr(pos3 + 2, len);
        }
        else
        {
            if (func_name_ != "DEL")
            {
                if (i == 1)
                    key_ = str.substr(pos3 + 2, len);
                else if (i == 2)
                    value_.append(str.substr(pos3 + 2, len));
                else
                    value_.append(" " + str.substr(pos3 + 2, len));
            }
            else
            {
                del_keys_.push_back(str.substr(pos3 + 2, len));
            }
        }
    }
    return true;
}

void Rpc::handle()
{
    std::cout << func_name_ << std::endl;
    std::cout << "-------------------" << std::endl;
    std::cout << key_ << std::endl;
    std::cout << "-------------------" << std::endl;
    std::cout << value_ << std::endl;
    std::cout << "-------------------" << std::endl;
}

void Rpc::encode()
{
    response_str_ = "*4\r\n$3\r\nSET\r\n$7\r\nCS06142\r\n$5\r\nCloud\r\n$9\r\nComputing\r\n";
}
