#include "tju_buffer.h"
// len只是这一次包的大小
send_buf *create_buffer(tju_tcp_t *sock, char *data, int len)
{
  log_info("创建buffer");
  pthread_mutex_lock(&sock->send_lock);
  send_buf *buffer = sock->sending_buf;
  if (buffer == NULL)
    log_error("buffer is null\n");
  char *msg;
  //-2是因为三次握手
  int current_seq = sock->sending_buf->buf_len;
  // 文件索引
  int i;
  for (i = 0; i + MAX_DLEN <= len; i = i + MAX_DLEN)
  {
    // log_info("the seq is %d ", current);
    msg = create_packet_buf(sock->established_local_addr.port, sock->established_remote_addr.port, sock->sending_buf->buf_len, 0,
                            DEFAULT_HEADER_LEN, MAX_DLEN + DEFAULT_HEADER_LEN, NO_FLAG, 1, 0, data + i, MAX_DLEN);

    // 将创建的报文放入发送缓冲区
    insert_send_buf_node_back(buffer, sock->sending_buf->len, 0, msg, MAX_DLEN + DEFAULT_HEADER_LEN);

    // sock->seq += MAX_DLEN;
    // log_info("包的num is %d", sock->sending_buf->buf_end->num);

    sock->sending_buf->buf_len += MAX_DLEN;
  }
  // 处理尾巴
  if (i < len)
  {
    // ack需要在发送时确认
    msg = create_packet_buf(sock->established_local_addr.port, sock->established_remote_addr.port, sock->sending_buf->buf_len, 0,
                            DEFAULT_HEADER_LEN, len - i + DEFAULT_HEADER_LEN, NO_FLAG, 1, 0, data + i, len - i);

    // 将创建的报文放入发送缓冲区
    insert_send_buf_node_back(buffer, sock->sending_buf->len, 0, msg, len - i + DEFAULT_HEADER_LEN);
    sock->sending_buf->buf_len += len - i;
  }
  pthread_mutex_unlock(&sock->send_lock);

  return buffer;
}
// 将窗口内的未发送的发完
// 在每次收到累计ack时需要调用
// 发送带有超时标志的报文
// 自动分配发送队列
void *send_from_buffer(void *v_sock)
{
  tju_tcp_t *sock = (tju_tcp_t *)v_sock;
  while (1)
  {
    // 锁定发送窗口
    sleep(1);
    pthread_mutex_lock(&sock->window.wnd_send->window_send_lock);
    log_info("获得发送锁");
    if (sock->window.wnd_send->window_send_buf == NULL)
    {
      sock->window.wnd_send->window_send_buf = malloc(sizeof(send_buf));
      log_info("发送窗口为null,现在分配空间");
    }
    else
      log_info("发送队列长度 %d ", sock->window.wnd_send->window_send_buf->len);
    // 等待发送缓冲区不为空 或者已经初始化
    if (sock->window.wnd_send->window_send_buf != NULL && sock->window.wnd_send->window_send_buf->len > 0)
    {
      log_info("in send_from_buffer,begin to send");
      // 获取发送缓冲区的头节点
      send_buf_node *current_node = sock->window.wnd_send->window_send_buf->buf_head;

      // 发送数据包
      while (current_node != NULL)
      {
        // 获得发送时间
        gettimeofday(&current_node->send_time, NULL);

        pthread_mutex_lock(&(sock->ack_lock));
        // 发送时给ack序号赋值
        set_ack(current_node->data, sock->ack);
        pthread_mutex_unlock(&(sock->ack_lock));

        // 发送数据包到网络层
        sendToLayer3(current_node->data, current_node->data_len);
        sock->seq += current_node->data_len - DEFAULT_HEADER_LEN;
        // 移动到下一个节点
        current_node = current_node->next_node;
        // 发送队列更新
        sock->window.wnd_send->window_send_buf->buf_head = current_node;
        sock->window.wnd_send->window_send_buf->len--;

        log_info("len is %d", sock->window.wnd_send->window_send_buf->len);
      }
    }
    // 解锁发送窗口
    pthread_mutex_unlock(&sock->window.wnd_send->window_send_lock);
  }

  return NULL;
}
bool window_send_is_full(tju_tcp_t *sock)
{ // 在调用这个函数前发送窗口加锁
  // 无法发送窗口内的都送入nextseq即为full
  bool ret;
  ret = (sock->window.wnd_send->base + sock->window.wnd_send->rwnd == sock->window.wnd_send->nextseq);
#ifdef DEBUG
  // log_info("base is %d,rwnd is %d,nextseq is %d ", sock->window.wnd_send->base, sock->window.wnd_send->rwnd, sock->window.wnd_send->nextseq);
#endif
  return ret;
}
int s_push(tju_tcp_t *sock, send_buf_node *node)
{
  insert_send_buf_node_back(sock->window.wnd_send->window_send_buf, node->num, node->flag, node->data, node->data_len);
  sock->window.wnd_send->window_send_buf->len++;
  return 0;
}
send_buf_node *s_pop(tju_tcp_t *sock)
{
  // Check if the send buffer is empty
  if (sock->window.wnd_send->window_send_buf->buf_head == NULL)
  {
    return NULL;
  }

  // Get the head node
  send_buf_node *current = sock->window.wnd_send->window_send_buf->buf_head;

  // Move the head pointer to the next node
  sock->window.wnd_send->window_send_buf->buf_head = current->next_node;

  // If the send buffer becomes empty, update the tail pointer
  if (sock->window.wnd_send->window_send_buf->buf_head == NULL)
  {
    sock->window.wnd_send->window_send_buf->buf_end = NULL;
  }

  // Clear the next_node pointer of the current node
  current->next_node = NULL;

  // 复制node
  send_buf_node *ret = (send_buf_node *)malloc(sizeof(send_buf_node));
  memcpy(ret, current, sizeof(send_buf_node));

  // Free the memory of the current node
  free(current);

  return ret;
}

// 更新RTT和RTO
void update_rtt(tju_tcp_t *sock, struct timeval ack_time)
{
  struct timeval rtt;
  timersub(&ack_time, &sock->sending_buf->buf_head->send_time, &rtt);
  double rtt_ms = rtt.tv_sec * 1000.0 + rtt.tv_usec / 1000.0;

  if (sock->window.wnd_send->estmated_rtt == 0)
  {
    sock->window.wnd_send->estmated_rtt = rtt_ms;
    sock->window.wnd_send->timeout.tv_sec = rtt_ms / 1000;
    sock->window.wnd_send->timeout.tv_usec = (rtt_ms - (sock->window.wnd_send->timeout.tv_sec * 1000)) * 1000;
  }
  else
  {
    double rttvar = (1 - BETA) * (sock->window.wnd_send->timeout.tv_sec * 1000 + sock->window.wnd_send->timeout.tv_usec / 1000.0) + BETA * fabs(sock->window.wnd_send->estmated_rtt - rtt_ms);
    sock->window.wnd_send->estmated_rtt = (1 - ALPHA) * sock->window.wnd_send->estmated_rtt + ALPHA * rtt_ms;
    sock->window.wnd_send->timeout.tv_sec = (sock->window.wnd_send->estmated_rtt + K * rttvar) / 1000;
    sock->window.wnd_send->timeout.tv_usec = ((sock->window.wnd_send->estmated_rtt + K * rttvar) - (sock->window.wnd_send->timeout.tv_sec * 1000)) * 1000;
  }
}

// 检查超时,将超时包放入发送队列
void *check_timeouts(void *arg)
{
  tju_tcp_t *sock = (tju_tcp_t *)arg;
  while (1)
  {
    usleep(TIMEOUT_INTERVAL * 1000);
    pthread_mutex_lock(&sock->window.wnd_send->window_wait_ack_lock);
    struct timeval now;
    gettimeofday(&now, NULL);
    send_buf_node *prev_node = NULL;
    send_buf_node *current_node = sock->window.wnd_send->window_wait_ack_buf->buf_head;

    while (current_node != NULL)
    {
      long elapsed_time = (now.tv_sec - current_node->send_time.tv_sec) * 1000 + (now.tv_usec - current_node->send_time.tv_usec) / 1000;
      if (elapsed_time > sock->window.wnd_send->timeout.tv_sec * 1000)
      {
        printf("Packet %d timed out, retransmitting\n", current_node->num);

        // 从等待确认队列中取出节点
        // 第一个节点
        if (prev_node == NULL)
        {
          sock->window.wnd_send->window_wait_ack_buf->buf_head = current_node->next_node;
        } // 收缩
        else
        {
          prev_node->next_node = current_node->next_node;
        }
        // 更新长度
        sock->window.wnd_send->window_wait_ack_buf->len--;
        // 最后一个节点
        if (current_node == sock->window.wnd_send->window_wait_ack_buf->buf_end)
        {
          sock->window.wnd_send->window_wait_ack_buf->buf_end = prev_node;
        }

        // 将节点放入发送队列
        insert_send_buf_node_back(sock->window.wnd_send->window_send_buf, current_node->num, current_node->flag, current_node->data, current_node->data_len);

        // 移动到下一个节点
        send_buf_node *temp_node = current_node;
        current_node = current_node->next_node;
        free(temp_node);
      }
      else
      {
        prev_node = current_node;
        current_node = current_node->next_node;
      }
    }

    pthread_mutex_unlock(&sock->window.wnd_send->window_wait_ack_lock);
  }
  return NULL;
}

// 简单的把数据放入接收缓冲区
char *buf_recving(tju_tcp_t *sock, char *pkt_data, int data_len)
{

  sock->received_len += data_len;
  memcpy(sock->received_buf, pkt_data, data_len);
}

// 处理收到的累计确认（包里的数据已放入接收缓冲区）ack_num为ack号
// 更新窗口（base，nextseq，adv_window） 清理节点
void handle_cumulative_ack(tju_tcp_t *sock, uint32_t pkt_src, uint32_t pkt_dst, uint32_t pkt_seq, uint32_t ack_num, uint32_t pkt_len, uint32_t adv_window)
{
  // 所有current != NULL 是为了区分发送方和接收方
  log_info("处理ack,ack %d ,adv %d ", ack_num, adv_window);

  pthread_mutex_lock(&sock->ack_lock);
  sock->ack = pkt_seq + pkt_len - DEFAULT_HEADER_LEN;
  pthread_mutex_unlock(&sock->ack_lock);

  // 处理发送缓冲区
  pthread_mutex_lock(&sock->send_lock);
  send_buf *sending_buf = sock->sending_buf;
  send_buf_node *current_node = sending_buf->buf_head;
  send_buf_node *prev_node = NULL;
  int released_len = 0;

  while (current_node != NULL && get_seq(current_node->data) + get_plen(current_node->data) - DEFAULT_HEADER_LEN <= ack_num)
  {
    log_info("开始处理发送缓冲区 num %d, 当前seq末尾 %d, ack_num %d", current_node->num, get_seq(current_node->data) + get_plen(current_node->data) - DEFAULT_HEADER_LEN, ack_num);
    send_buf_node *temp = current_node;
    current_node = current_node->next_node;

    // 释放已确认的节点
    free(temp->data);
    free(temp);

    // 更新缓冲区头节点
    sending_buf->buf_head = current_node;

    // 如果当前节点是尾节点，更新尾节点
    if (current_node == NULL)
    {
      sending_buf->buf_end = prev_node;
    }
    // 增加释放的长度
    released_len++;
    // 更新窗口的 base
    sock->window.wnd_send->base++;
  }
  // 更新发送缓冲区的长度
  sending_buf->len -= released_len;

  pthread_mutex_lock(&(sock->window.wnd_send->window_send_lock));

  // 调整流量窗口
  sock->window.wnd_send->rwnd = adv_window;

  // 将窗口新增内容放入发送队列
  if (!window_send_is_full(sock))
  {
    log_info("开始初始化发送队列");
    log_info("获得窗口锁");
    send_buf_node *current = sock->sending_buf->buf_head;
    // 移动到发送缓冲区的待放入发送队列的位置
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
        // 这种情况适用于服务端，无发送缓冲区 或者发送缓冲区结束的客户端
        //  发送队列为空，主要应对服务器；  对于客户端，若发送缓冲区不空，则进行简单确认 否则终止
        char *msg;
        uint16_t plen = DEFAULT_HEADER_LEN;
        msg = create_packet_buf(pkt_dst, pkt_src, sock->seq, sock->ack,
                                DEFAULT_HEADER_LEN, plen, NO_FLAG, 1, 0, NULL, 0);
        sendToLayer3(msg, plen);
        // 若是无数据，仍是老ack
        sock->ack += pkt_seq + pkt_len - DEFAULT_HEADER_LEN;
        break;
        // else
        // { // 客户端所有内容发送完成  服务端发送端为空
        //   //tju_close(sock);
        // }
      }
    }
  }
  pthread_mutex_unlock(&(sock->window.wnd_send->window_send_lock));
  pthread_mutex_unlock(&sock->send_lock);

  // 处理窗口的待确认队列
  pthread_mutex_lock(&sock->window.wnd_send->window_wait_ack_lock);

  send_buf *window_wait_ack_buf = sock->window.wnd_send->window_wait_ack_buf;
  current_node = window_wait_ack_buf->buf_head;
  prev_node = NULL;
  int window_released_len = 0;
  for (int i = 0; i < sock->window.wnd_send->window_wait_ack_buf->len; i++)
  {
    if (current_node == NULL)
      break;
    else if (current_node->num < sock->window.wnd_send->base)
    {
      send_buf_node *temp = current_node;
      current_node = current_node->next_node;

      // 释放已确认的节点
      free(temp->data);
      free(temp);

      // 更新缓冲区头节点
      if (prev_node == NULL)
      {
        window_wait_ack_buf->buf_head = current_node;
      }
      else
      {
        prev_node->next_node = current_node;
      }

      // 如果当前节点是尾节点，更新尾节点
      if (current_node == NULL)
      {
        window_wait_ack_buf->buf_end = prev_node;
        break;
      }

      // 增加释放的长度
      window_released_len++;
    }
    else
    {
      // 滑动
      prev_node = prev_node->next_node;
      current_node = current_node->next_node;
    }
  }

  // 更新发送窗口的发送队列的长度
  window_wait_ack_buf->len -= window_released_len;

  pthread_mutex_unlock(&sock->window.wnd_send->window_wait_ack_lock);
}

// 通过ack_num获得对应发送缓冲区包的位置
int get_send_buf_pkt_pos(tju_tcp_t *sock, int ack_num)
{
  // buf_head是发送窗口开始的地方
  send_buf *buffer = sock->sending_buf;
  send_buf_node *current_node = buffer->buf_head;

  // 遍历发送缓冲区，定位
  while (current_node != NULL && get_seq(current_node->data) + get_plen(current_node->data) - DEFAULT_HEADER_LEN < ack_num)
  {
    send_buf_node *temp = current_node;
    current_node = current_node->next_node;
    if (current_node == buffer->buf_head)
    {
      buffer->buf_head = NULL;
      buffer->buf_end = NULL;
      return 0;
      break;
    }
  }
  return current_node->num;
}

void init_send_buf(send_buf **buffer)
{
  *buffer = (send_buf *)malloc(sizeof(send_buf));
  if (*buffer == NULL)
    log_error("buffer is null\n");
  (*buffer)->buf_head = NULL;
  (*buffer)->buf_end = NULL;
  (*buffer)->len = 0;
  // 应对的是序号需要从三次握手之后的序号开始
  (*buffer)->buf_len = 2;
}

void insert_send_buf_node_back(send_buf *buffer, int num, int flag, const char *data, int data_len)
{
  send_buf_node *new_node = (send_buf_node *)malloc(sizeof(send_buf_node));
  new_node->num = num;
  new_node->flag = flag;
  new_node->data = (char *)malloc(data_len * sizeof(char));
  // log_info("before memcpy");
  memcpy(new_node->data, data, data_len);
  // log_info("after memcpy");

  new_node->data_len = data_len;

  new_node->next_node = NULL;

  if (buffer == NULL)
    buffer = malloc(sizeof(send_buf));
  if (buffer->buf_head == NULL)
  {
    log_info("进入buffer空\n");

    // 如果缓冲区为空，初始化头节点和尾节点
    buffer->buf_head = new_node;
    buffer->buf_end = new_node;
  }
  else
  {
    // log_info("进入buffer非空\n");

    // 插入新节点到链表尾部
    buffer->buf_end->next_node = new_node; // 尾节点指向新节点
    buffer->buf_end = new_node;            // 更新尾节点
  }

  buffer->len += 1;
}

void insert_send_buf_node_front(send_buf *buffer, int num, int flag, const char *data, int data_len)
{
  send_buf_node *new_node = (send_buf_node *)malloc(sizeof(send_buf_node));
  new_node->num = num;
  new_node->flag = flag;
  new_node->data = (char *)malloc(data_len);
  memcpy(new_node->data, data, data_len);
  new_node->data_len = data_len;
  new_node->next_node = NULL;

  if (buffer->buf_head == NULL)
  {
    // 如果缓冲区为空，初始化头节点和尾节点
    buffer->buf_head = new_node;
    buffer->buf_end = new_node;
    // new_node->next_node = new_node; // 循环链表，指向自己
  }
  else
  {
    // 插入新节点到链表头部
    // new_node->next_node = buffer->buf_head;
    new_node->next_node = buffer->buf_head; // 新节点指向头节点
    buffer->buf_head = new_node;            // 更新头节点
  }

  buffer->len += 1;
}