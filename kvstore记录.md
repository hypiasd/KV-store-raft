# kvstore记录

## 一、rpc服务器

使用的buttonrpc，链接：https://github.com/button-chen/buttonrpc_cpp14

```c++
buttonrpc server_;                                      *// rpc服务器*

buttonrpc client_[100];                                 *// rpc客户端， 用于发送信息*
```

```c++
std::thread server_thread([this](int port)
                              {
        server_.as_server(port);
        server_.bind("vote", &KVStore::vote, this);
        {std::lock_guard<std::mutex> lock(cv_mtx_);
        cv_.notify_one();}
        {std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << "run rpc server on: " << port << std::endl;}
        server_.run(); },
                              nodes_[id_]);
{
    std::unique_lock<std::mutex> lock(cv_mtx_);
    cv_.wait(lock);
}
for (int i = 0; i < num_nodes_; i++)
{
    client_[i].as_client("127.0.0.1", nodes_[i]);
    client_[i].set_timeout(50); // 50ms超时重传
}
```

## 二、raft参数

**状态**：

所有服务器上的持久性状态
(在响应 RPC 请求之前，已经更新到了稳定的存储设备)

| 参数        | 解释                                                         |
| ----------- | ------------------------------------------------------------ |
| currentTerm | 服务器已知最新的任期（在服务器首次启动时初始化为0，单调递增） |
| votedFor    | 当前任期内收到选票的 candidateId，如果没有投给任何候选人 则为空 |
| log[]       | 日志条目；每个条目包含了用于状态机的命令，以及领导人接收到该条目时的任期（初始索引为1） |

所有服务器上的易失性状态

| 参数        | 解释                                                         |
| ----------- | ------------------------------------------------------------ |
| commitIndex | 已知已提交的最高的日志条目的索引（初始值为0，单调递增）      |
| lastApplied | 已经被应用到状态机的最高的日志条目的索引（初始值为0，单调递增） |

领导人（服务器）上的易失性状态
(选举后已经重新初始化)

| 参数         | 解释                                                         |
| ------------ | ------------------------------------------------------------ |
| nextIndex[]  | 对于每一台服务器，发送到该服务器的下一个日志条目的索引（初始值为领导人最后的日志条目的索引+1） |
| matchIndex[] | 对于每一台服务器，已知的已经复制到该服务器的最高日志条目的索引（初始值为0，单调递增） |

## 三、状态机

```c++
void KVStore::transition(state st)
{
    state_ = st;
    switch (state_)
    {
    case follower:
        follower_func();
        break;
    case candidate:
        candidate_func();
        break;
    case leader:
        leader_func();
        break;
    }
}void KVStore::transition(state st)
{
    state_ = st;
    switch (state_)
    {
    case follower:
        follower_func();
        break;
    case candidate:
        candidate_func();
        break;
    case leader:
        leader_func();
        break;
    }
}
```

## 四、跟随者

### 状态操作：

1. 投票对象重设为-1

2. 收到的票数重设为0

3. 开启计时器

4. 改变状态：当计时器停止时，说明选举超时了，这时需要将状态改变为候选者

### 接收信息操作：

1. 接收候选人candidate的投票（server_绑定的vote函数）

2. 接收领导者leader的心跳 （server_绑定的append函数）



## 五、候选者

### 状态操作：

1. 投票对象设为自己
2. 收到的票数设为1
3. 任期加1
4. 开启计时（包含选举）
5. 改变状态：
    1. 若投票给了别人，说明有更合适的领导者，故状态变为跟随者
    2. 若不符合1，且票数超过了总结点数的一半，则状态变为领导者
    3. 若不符合2，则选举超时，状态依然保持候选者参加下一轮选举

### 接收信息操作：

**接收到信息后都要重新计时**

1. 接收其他候选人的投票
2. 接收领导者的心跳，若任期号不小于自身任期，则变回到跟随者，否则继续选举

## 六、选举投票

**请求投票（RequestVote）RPC**：

由候选人负责调用用来征集选票（5.2 节）

| 参数         | 解释                         |
| ------------ | ---------------------------- |
| term         | 候选人的任期号               |
| candidateId  | 请求选票的候选人的 ID        |
| lastLogIndex | 候选人的最后日志条目的索引值 |
| lastLogTerm  | 候选人最后日志条目的任期号   |

| 返回值      | 解释                                       |
| ----------- | ------------------------------------------ |
| term        | 当前任期号，以便于候选人去更新自己的任期号 |
| voteGranted | 候选人赢得了此张选票时为真                 |

**设candidate_term为请求投票者的任期，term为当前任期**

**lastLogIndex为请求投票者的最后日志条目的索引值，index为当前的**

**lastLogTerm为请求投票者的最后日志条目的任期号，logterm为当前的**

### 1、任期

更新任期：candidate_term > term

返回任期：max(candidate_term, term)

### 2、投票

1. 比较任期，若candidate_term <= term，则拒绝投票
2. 若candidate_term > term，更新term
    1. 若lastLogTerm < logterm, 则拒绝投票
    2. 若lastLogTerm = logterm, lastLogIndex < index, 则拒绝投票
    3. 若lastLogTerm = logterm, lastLogIndex = index, 若state为candidate（应该只可能是candidate），则拒绝投票，且投给自己
    4. 否则同意投票

> 注：
>
> 1、跟随者可以先后同意给不同的节点投票，前提是节点的优先级不同



## 七、领导者

### 状态操作：

1. 将投票对象设为-1，若之后改变了，说明状态也变了

2. 开启心跳定时器
3. 定时器退出，状态变更为跟随者

### 接收信息操作：

1. 接收其他候选人的投票，若投票了，则变为跟随者
2. 接收client请求，同时重新开始发心跳

## 八、AppendEntries

**追加条目（AppendEntries）RPC**：

由领导人调用，用于日志条目的复制，同时也被当做心跳使用

| 参数         | 解释                                                         |
| ------------ | ------------------------------------------------------------ |
| term         | 领导人的任期                                                 |
| leaderId     | 领导人 ID 因此跟随者可以对客户端进行重定向（译者注：跟随者根据领导人 ID 把客户端的请求重定向到领导人，比如有时客户端把请求发给了跟随者而不是领导人） |
| prevLogIndex | 紧邻新日志条目之前的那个日志条目的索引                       |
| prevLogTerm  | 紧邻新日志条目之前的那个日志条目的任期                       |
| entries[]    | 需要被保存的日志条目（被当做心跳使用时，则日志条目内容为空；为了提高效率可能一次性发送多个） |
| leaderCommit | 领导人的已知已提交的最高的日志条目的索引                     |

| 返回值  | 解释                                                         |
| ------- | ------------------------------------------------------------ |
| term    | 当前任期，对于领导人而言 它会更新自己的任期                  |
| success | 如果跟随者所含有的条目和 prevLogIndex 以及 prevLogTerm 匹配上了，则为 true |

### 对于接收者：

判断entries是否为空，为空则是一次心跳