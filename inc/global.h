#ifndef _GLOBAL_H_
#define _GLOBAL_H_

#include <netinet/in.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <math.h>
#include "log.h"

#define DEBUG

// 单位是byte
#define SIZE32 4
#define SIZE16 2
#define SIZE8 1

// 一些Flag
#define NO_FLAG 0
#define NO_WAIT 1
#define TIMEOUT 2
#define TRUE 1
#define FALSE 0

// 定义最大包长 防止IP层分片
#define MAX_DLEN 1375 // 最大包内数据长度
#define MAX_LEN 1400  // 最大包长度

// seq start for CLINET and SERVER
#define CLIENT_ISN 0
#define SERVER_ISN 0

// TCP socket 状态定义
#define CLOSED 0
#define LISTEN 1
#define SYN_SENT 2
#define SYN_RECV 3
#define ESTABLISHED 4
#define FIN_WAIT_1 5
#define FIN_WAIT_2 6
#define CLOSE_WAIT 7
#define CLOSING 8
#define LAST_ACK 9
#define TIME_WAIT 10

// TCP 拥塞控制状态
#define SLOW_START 0
#define CONGESTION_AVOIDANCE 1
#define FAST_RECOVERY 2

// #define SERVER_IP "172.17.0.6"
// #define CLIENT_IP "172.17.0.5"

#define SERVER_IP "172.17.0.3"
#define CLIENT_IP "172.17.0.2"

// TCP 接受窗口大小
#define TCP_RECVWN_SIZE 32 * MAX_DLEN // 比如最多放32个满载数据包

#define TCP_BUF_SIZE 10000

#define TIMEOUT_INTERVAL 1000 // 超时时间间隔（毫秒）
#define ALPHA 0.125
#define BETA 0.25
#define K 4

typedef struct send_buf_node send_buf_node;

typedef struct send_buf_node
{
  int num;                  // 编号 用来解决乱序
  struct timeval send_time; // 时间戳
  int flag;                 // 是否发送
  char *data;               // 包头+数据
  int data_len;
  send_buf_node *next_node;
} send_buf_node;
typedef struct send_buf
{
  send_buf_node *buf_head;
  send_buf_node *buf_end;
  // 已使用的空间 按报文计数
  int len;
  // 已创建的字节数
  int buf_len;
} send_buf;

// TCP 发送窗口
// 注释的内容如果想用就可以用 不想用就删掉 仅仅提供思路和灵感
typedef struct
{
  // uint16_t window_size;
  pthread_mutex_t window_send_lock;
  send_buf *window_send_buf;
  pthread_mutex_t window_wait_ack_lock;
  send_buf *window_wait_ack_buf;
  uint32_t base;
  uint32_t nextseq;
  uint32_t estmated_rtt;
  int ack_cnt;
  pthread_mutex_t ack_cnt_lock;
  // struct timeval send_time;
  struct timeval timeout;
  uint16_t rwnd;
  uint16_t adv_window;
  //   int congestion_status;
  //   uint16_t cwnd;
  //   uint16_t ssthresh;
} sender_window_t;

// TCP 接受窗口

typedef struct
{
  // 按照字节流放置数据
  char received[TCP_RECVWN_SIZE];
  uint32_t base;
  //   received_packet_t* head;
  //   char buf[TCP_RECVWN_SIZE];
  // uint8_t marked[TCP_RECVWN_SIZE];
  uint32_t expect_seq;
} receiver_window_t;

// TCP 窗口 每个建立了连接的TCP都包括发送和接受两个窗口
typedef struct
{
  sender_window_t *wnd_send;
  receiver_window_t *wnd_recv;
} window_t;

typedef struct
{
  uint32_t ip;
  uint16_t port;
} tju_sock_addr;

// TJU_TCP 结构体 保存TJU_TCP用到的各种数据
typedef struct
{
  int state; // TCP的状态

  tju_sock_addr bind_addr;               // 存放bind和listen时该socket绑定的IP和端口
  tju_sock_addr established_local_addr;  // 存放建立连接后 本机的 IP和端口
  tju_sock_addr established_remote_addr; // 存放建立连接后 连接对方的 IP和端口

  pthread_mutex_t send_lock; // 发送数据锁
  send_buf *sending_buf;     // 发送数据缓存区
  int sending_len;           // 发送数据缓存长度  按字节

  pthread_mutex_t recv_lock; // 接收数据锁
  char *received_buf;        // 接收数据缓存区
  int received_len;          // 接收数据缓存长度 按字节

  pthread_cond_t wait_cond; // 可以被用来唤醒recv函数调用时等待的线程

  window_t window; // 发送和接受窗口
  struct sock_queue *half_queue;
  struct sock_queue *full_queue;

  pthread_mutex_t ack_lock;
  int ack; // 按字节计数
  pthread_mutex_t seq_lock;
  int seq; // 按字节计数

} tju_tcp_t;

typedef struct sock_node
{
  tju_tcp_t *node;
  struct sock_node *next;
} sock_node;

typedef struct sock_queue
{
  sock_node *sock_head;
  sock_node *sock_end;
  int len;
} sock_queue;

#endif