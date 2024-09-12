#include "tju_tcp.h"
/*
创建 TCP socket
初始化对应的结构体
设置初始状态为 CLOSED
*/
tju_tcp_t *tju_socket()
{
  tju_tcp_t *sock = (tju_tcp_t *)malloc(sizeof(tju_tcp_t));
  sock->state = CLOSED;

  pthread_mutex_init(&(sock->send_lock), NULL);
  sock->sending_len = 0;

  pthread_mutex_init(&(sock->recv_lock), NULL);
  sock->received_len = 0;

  if (pthread_cond_init(&sock->wait_cond, NULL) != 0)
  {
    perror("ERROR condition variable not set\n");
    exit(-1);
  }
  sock->half_queue = q_init();
  sock->full_queue = q_init();

  sock->window.wnd_send = malloc(sizeof(sender_window_t));
  sock->window.wnd_recv = malloc(sizeof(receiver_window_t));

  // 传指针
  init_send_buf(&(sock->sending_buf));
  init_send_buf(&(sock->window.wnd_send->window_send_buf));
  init_send_buf(&(sock->window.wnd_send->window_wait_ack_buf));
  if (sock->sending_buf == NULL)
    log_error("sock->sending_buf\n");

  // 发送线程
  log_info("进入发送线程");
  pthread_t thread_id;
  int rst = pthread_create(&thread_id, NULL, send_from_buffer, (void *)sock);
  log_info("回到主线程");

  return sock;
}

/*
绑定监听的地址 包括ip和端口
*/
int tju_bind(tju_tcp_t *sock, tju_sock_addr bind_addr)
{
  sock->bind_addr = bind_addr;
  return 0;
}

/*
被动打开 监听bind的地址和端口
设置socket的状态为LISTEN
注册该socket到内核的监听socket哈希表
*/
int tju_listen(tju_tcp_t *sock)
{
  sock->state = LISTEN;

  int hashval = cal_hash(sock->bind_addr.ip, sock->bind_addr.port, 0, 0);

  listen_socks[hashval] = sock;
  return 0;
}

/*
接受连接
返回与客户端通信用的socket
这里返回的socket一定是已经完成3次握手建立了连接的socket
因为只要该函数返回, 用户就可以马上使用该socket进行send和recv
*/
tju_tcp_t *tju_accept(tju_tcp_t *listen_sock)
{
  while (!listen_sock->full_queue->len)
    ; // NOTE: 当 listen 的全连接空的时候阻塞
  printf("accept connect\n");
  tju_tcp_t *new_connection = q_pop(listen_sock->full_queue);

  tju_sock_addr local_addr, remote_addr;
  local_addr = new_connection->established_local_addr;
  remote_addr = new_connection->established_remote_addr;

  int hashval = cal_hash(local_addr.ip, local_addr.port, remote_addr.ip, remote_addr.port);
  established_socks[hashval] = new_connection;

  // 分配缓冲区
  init_send_buf(&new_connection->sending_buf);
  new_connection->received_buf = (char *)malloc(MAX_LEN * TCP_BUF_SIZE);

  new_connection->state = ESTABLISHED;
  printf("写入建立表 %d,%d\n", local_addr.port, remote_addr.port);

  return new_connection;
}

/*
连接到服务端
该函数以一个socket为参数
调用函数前, 该socket还未建立连接
函数正常返回后, 该socket一定是已经完成了3次握手, 建立了连接
因为只要该函数返回, 用户就可以马上使用该socket进行send和recv
*/
int tju_connect(tju_tcp_t *sock, tju_sock_addr target_addr)
{

  tju_sock_addr local_addr;
  local_addr.ip = inet_network(CLIENT_IP);
  local_addr.port = 5678; // 连接方进行connect连接的时候 内核中是随机分配一个可用的端口
  sock->established_local_addr = local_addr;
  sock->established_remote_addr = target_addr;
  int hashval = cal_hash(local_addr.ip, local_addr.port, 0, 0);
  listen_socks[hashval] = sock;
  // 这里也不能直接建立连接 需要经过三次握手
  // 实际在linux中 connect调用后 会进入一个while循环
  // 循环跳出的条件是socket的状态变为ESTABLISHED 表面看上去就是 正在连接中 阻塞
  // 而状态的改变在别的地方进行 在我们这就是tju_handle_packet

  // 发送请求
  // 等待
  // 被server同意，开始创建资源
  // ack
  uint16_t plen = DEFAULT_HEADER_LEN;
  char *msg;
  sock->seq = CLIENT_ISN;
  sock->ack = 0;
  msg = create_packet_buf(sock->established_local_addr.port, sock->established_remote_addr.port, sock->seq, sock->ack,
                          DEFAULT_HEADER_LEN, plen, SYN_FLAG_MASK, 1, 0, NULL, 0);
  // 防止分片？
  sendToLayer3(msg, plen);
  sock->state = SYN_SENT;
  // 超时？
  //  tju_handle_packet需要分析tcp包的类型，根据以前的状态，判断是否改变状态
  // 多个同时connect一个socket？
  // 设定超时重传定时器？
  while (sock->state != ESTABLISHED)
    ;
  hashval = cal_hash(sock->established_local_addr.ip, sock->established_local_addr.port, sock->established_remote_addr.ip, sock->established_remote_addr.port);

  established_socks[hashval] = sock;

  return 0;
}
// 传入原始数据，处理，发送
int tju_send(tju_tcp_t *sock, const void *buffer, int len)
{
#ifdef DEBUG
  // log_info("调用tju_send");
#endif
  log_info("调用tju_send");
  // 这里当然不能直接简单地调用sendToLayer3
  create_buffer(sock, buffer, len);

  // ？
  // 初始化发送窗口
  pthread_mutex_lock(&(sock->window.wnd_send->window_send_lock));
  log_info("获得窗口锁");
  if (!window_send_is_full(sock))
  {
    // log_info("开始初始化发送队列");
    send_buf_node *current = sock->sending_buf->buf_head;
    while (current != NULL && current->num != sock->window.wnd_send->nextseq)
    {
      current = current->next_node;
    }
    for (int i = sock->window.wnd_send->nextseq; i < sock->window.wnd_send->base + sock->window.wnd_send->rwnd; i++)
    {
      log_info("进入for循环");
      if (current != NULL)
      {
        log_info("插入发送队列");
        insert_send_buf_node_back(sock->window.wnd_send->window_send_buf, current->num, current->flag, current->data, current->data_len);
        // 更改窗口的nextseq
        sock->window.wnd_send->nextseq++;
        current = current->next_node;
      }
      else
      {
        break;
      }
    }
  }
  pthread_mutex_unlock(&(sock->window.wnd_send->window_send_lock));

  return 0;
}
int tju_recv(tju_tcp_t *sock, void *buffer, int len)
{
  // 接收窗口处理乱序：base窗口的起始seq, expect_seq是还没被装入数据的位置（前面已经有序）
  // 接收缓冲区被tju_recv函数接收
  //
  while (sock->received_len <= 0)
  {
    // 阻塞
  }

  while (pthread_mutex_lock(&(sock->recv_lock)) != 0)
    ; // 加锁

  int read_len = 0;
  if (sock->received_len >= len)
  { // 从中读取len长度的数据
    read_len = len;
  }
  else
  {
    read_len = sock->received_len; // 读取sock->received_len长度的数据(全读出来)
  }

  memcpy(buffer, sock->received_buf, read_len);

  if (read_len < sock->received_len)
  { // 还剩下一些
    char *new_buf = malloc(sock->received_len - read_len);
    memcpy(new_buf, sock->received_buf + read_len, sock->received_len - read_len);
    free(sock->received_buf);
    sock->received_len -= read_len;
    sock->received_buf = new_buf;
  }
  else
  {
    free(sock->received_buf);
    sock->received_buf = NULL;
    sock->received_len = 0;
  }
  pthread_mutex_unlock(&(sock->recv_lock)); // 解锁

  return 0;
}

int tju_handle_packet(tju_tcp_t *sock, char *pkt)
{

  int pkt_seq = get_seq(pkt);
  int pkt_src = get_src(pkt);
  int pkt_dst = get_dst(pkt);
  int pkt_ack = get_ack(pkt);
  int pkt_plen = get_plen(pkt);
  int pkt_flag = get_flags(pkt);
  int pkt_adv = get_advertised_window(pkt);
  // printf("server is recv\n");
  uint32_t data_len = pkt_plen - DEFAULT_HEADER_LEN;

  switch (sock->state)
  {
  case SYN_SENT:
    if (pkt_flag == (SYN_FLAG_MASK | ACK_FLAG_MASK) && pkt_ack == CLIENT_ISN + 1)
    {
      // 第二次握手
      // 发送ack
      char *msg;
      uint16_t plen = DEFAULT_HEADER_LEN;

      msg = create_packet_buf(pkt_dst, pkt_src, pkt_ack, pkt_seq + 1,
                              DEFAULT_HEADER_LEN, plen, ACK_FLAG_MASK, 1, 0, NULL, 0);
      sendToLayer3(msg, plen);
      sock->seq = pkt_ack + 1;
      sock->ack = pkt_seq + 1;

      // 初始化流量控制窗口
      sock->window.wnd_send->rwnd = pkt_adv;
      sock->window.wnd_send->base = 0;
      sock->window.wnd_send->nextseq = 0;

      // 初始化缓冲区
      init_send_buf(&sock->sending_buf);
      sock->received_buf = (char *)malloc(MAX_LEN * TCP_BUF_SIZE);

      sock->state = ESTABLISHED;
    }

    break;
  case LISTEN:
    if (pkt_flag == SYN_FLAG_MASK)
    {

      tju_tcp_t *new_sock = (tju_tcp_t *)malloc(sizeof(tju_tcp_t));
      new_sock = memcpy(new_sock, sock, sizeof(tju_tcp_t));
      // 标识连接的四元组
      tju_sock_addr remote_addr, local_addr;
      remote_addr.ip = inet_network(CLIENT_IP); // Listen 是 server 端的行为，所以远程地址就是 172.17.0.2
      remote_addr.port = pkt_src;
      local_addr.ip = sock->bind_addr.ip;
      local_addr.port = sock->bind_addr.port;

      new_sock->established_local_addr = local_addr;
      new_sock->established_remote_addr = remote_addr;

      new_sock->state = SYN_RECV;

      new_sock->ack = pkt_seq + 1;

      q_push(sock->half_queue, new_sock);

      char *msg;
      uint16_t plen = DEFAULT_HEADER_LEN;

      msg = create_packet_buf(pkt_dst, pkt_src, SERVER_ISN, pkt_seq + 1,
                              DEFAULT_HEADER_LEN, plen, ACK_FLAG_MASK + SYN_FLAG_MASK, 1, 0, NULL, 0);
      sendToLayer3(msg, plen);

      new_sock->seq = SERVER_ISN + 1;
    }
    // ？后期要做seq和ack检查
    else if (pkt_flag == ACK_FLAG_MASK)
    {
      // 第三次握手
      tju_tcp_t *new_temp_conn = q_pop(sock->half_queue);

      tju_tcp_t *new_conn = (tju_tcp_t *)malloc(sizeof(tju_tcp_t));

      printf("new_temp_conn\n");
      memcpy(new_conn, new_temp_conn, sizeof(tju_tcp_t));

      new_conn->ack = pkt_seq + 1;
      // 流量控制窗口初始化
      new_conn->window.wnd_send->rwnd = pkt_adv;
      // 初始化接收缓冲区窗口
      new_conn->window.wnd_recv->base = new_conn->ack;
      new_conn->window.wnd_recv->expect_seq = new_conn->ack;
      memset(new_conn->window.wnd_recv->received, -1, TCP_RECVWN_SIZE);

      q_push(sock->full_queue, new_conn);
    }
    break;
  case ESTABLISHED:
    if (pkt_flag == (FIN_FLAG_MASK | ACK_FLAG_MASK))
    {
      // 被动
      printf("服务端被动关闭，发送ack\n");
      log_info("包的seq %d，ack %d，flag %d", pkt_seq, pkt_ack, pkt_flag);
      // Send ACK for FIN
      char *msg;
      uint16_t plen = DEFAULT_HEADER_LEN;
      msg = create_packet_buf(pkt_dst, pkt_src, sock->seq, pkt_seq + 1,
                              DEFAULT_HEADER_LEN, plen, ACK_FLAG_MASK, 1, 0, NULL, 0);
      sendToLayer3(msg, plen);
      sock->seq += 1;
      sock->ack += 1;
      sock->state = CLOSE_WAIT;
    }
    else if (pkt_flag == NO_FLAG)
      log_info("收到包的seq %d，ack %d，flag %d", pkt_seq, pkt_ack, pkt_flag);
    { // 发送ack（1.发送端 放入发送队列等同，2.接收端 发送队列空，直接发送ack）
      // 发送缓冲区和待确认队列释放确认的节点 更新窗口
      // 窗口内的新增节点放入发送队列
      // 服务端和客户端
      handle_cumulative_ack(sock, pkt_src, pkt_dst, pkt_seq, pkt_ack, pkt_plen, pkt_adv);
    }
    break;
  case FIN_WAIT_1:
    if (pkt_flag == ACK_FLAG_MASK)
    {
      printf("处于fin_wait_1,收到ack\n");
      sock->state = FIN_WAIT_2;
      sock->ack += 1;
    }

    else if (pkt_flag == (FIN_FLAG_MASK | ACK_FLAG_MASK))
    {
      printf("收到fin,同时关闭\n");

      char *msg;
      uint16_t plen = DEFAULT_HEADER_LEN;
      msg = create_packet_buf(pkt_dst, pkt_src, sock->seq, pkt_seq + 1,
                              DEFAULT_HEADER_LEN, plen, ACK_FLAG_MASK, 1, 0, NULL, 0);
      sendToLayer3(msg, plen);
      sock->seq += 1;
      sock->ack += 1;
      sock->state = CLOSING;
    }
    break;
  case FIN_WAIT_2:
    if (pkt_flag == (FIN_FLAG_MASK | ACK_FLAG_MASK))
    {
      printf("fin_wait_2,收到fin,发送ack\n");

      char *msg;
      uint16_t plen = DEFAULT_HEADER_LEN;
      msg = create_packet_buf(pkt_dst, pkt_src, sock->seq, pkt_seq + 1,
                              DEFAULT_HEADER_LEN, plen, ACK_FLAG_MASK, 1, 0, NULL, 0);
      sendToLayer3(msg, plen);
      sock->seq += 1;
      sock->ack += 1;
      sock->state = TIME_WAIT;
    }
    break;
  case CLOSING:
    if (pkt_flag == ACK_FLAG_MASK)
    {
      printf("closing状态下收到ack\n");
      sock->state = TIME_WAIT;
      sock->ack += 1;
    }
    break;
  case LAST_ACK:
    if (pkt_flag == ACK_FLAG_MASK)
    {
      printf("LAST_ACK状态下收到ack\n");
      sock->ack += 1;
      sock->state = CLOSED;
    }
    break;
  default:
    break;
  }

  // 把收到的数据放到接受窗口
  pthread_mutex_lock(&(sock->recv_lock));
  // 加锁

  // 接收窗口处理乱序
  // 能完全进入窗口的放入窗口中
  // 确定建议的流量控制窗口
  // 通过长度判断进行区分接收端和发送端
  if ((pkt_plen - DEFAULT_HEADER_LEN != 0) && pkt_seq - sock->window.wnd_recv->base + pkt_plen - DEFAULT_HEADER_LEN <= TCP_RECVWN_SIZE)
  {
    log_info("处理接收窗口");
    memcpy(sock->window.wnd_recv->received + pkt_seq - sock->window.wnd_recv->base, pkt + DEFAULT_HEADER_LEN, pkt_plen - DEFAULT_HEADER_LEN);
    if (sock->window.wnd_recv->expect_seq == pkt_seq)
      sock->window.wnd_recv->expect_seq += pkt_plen - DEFAULT_HEADER_LEN;

    for (int i = sock->window.wnd_recv->expect_seq; i < sock->window.wnd_recv->base + TCP_RECVWN_SIZE; i++)
    { // 确定最后一个待有序字节的位置
      if (sock->window.wnd_recv->received[i] == -1)
      {
        sock->window.wnd_recv->expect_seq++;
      }
      else
        break;
    }
    // // 应对接收端，区分接收端和发送端
    // if (flag)
    // {
    //   sock->ack = sock->window.wnd_recv->expect_seq;
    //   char *msg;
    //   uint16_t plen = DEFAULT_HEADER_LEN;
    //   msg = create_packet_buf(pkt_dst, pkt_src, sock->seq, sock->ack,
    //                           DEFAULT_HEADER_LEN, plen, NO_FLAG, 1, 0, NULL, 0);
    //   sendToLayer3(msg, plen);
    // }
  }
  pthread_mutex_unlock(&(sock->recv_lock)); // 解锁

  return 0;
}

/*
关闭一个TCP连接
这里涉及到四次挥手
*/

int tju_close(tju_tcp_t *sock)
{
  // 检查当前状态
  if (sock->state == ESTABLISHED)
  {
    // 发送FIN包
    char *msg;
    uint16_t plen = DEFAULT_HEADER_LEN;
    msg = create_packet_buf(sock->established_local_addr.port, sock->established_remote_addr.port, sock->seq, sock->ack,
                            DEFAULT_HEADER_LEN, plen, FIN_FLAG_MASK | ACK_FLAG_MASK, 1, 0, NULL, 0);
    sendToLayer3(msg, plen);
    sock->seq += 1;
    sock->state = FIN_WAIT_1;
    printf("established->fin_wait_1\n");
    while (sock->state != TIME_WAIT)
      ;
    sleep(3);
    // 等待
  }
  else if (sock->state == CLOSE_WAIT)
  {
    // 处理缓冲区剩余数据

    // 发送FIN包
    char *msg;
    uint16_t plen = DEFAULT_HEADER_LEN;
    msg = create_packet_buf(sock->established_local_addr.port, sock->established_remote_addr.port, sock->seq, sock->ack,
                            DEFAULT_HEADER_LEN, plen, FIN_FLAG_MASK | ACK_FLAG_MASK, 1, 0, NULL, 0);
    sendToLayer3(msg, plen);
    sock->seq == 1;
    sock->state = LAST_ACK;
    while (sock->state != CLOSED)
      ;
  }

  // 释放资源
  if (sock->received_buf != NULL)
  {
    free(sock->received_buf);
    sock->received_buf = NULL;
  }
  if (sock->sending_buf != NULL)
  {
    free(sock->sending_buf);
    sock->sending_buf = NULL;
  }

  // 更新状态
  sock->state = CLOSED;
}
