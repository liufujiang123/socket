// 结构
#ifndef _TJU_QUEUE_H_
#define _TJU_QUEUE_H_
#include "kernel.h"

sock_queue *q_init();
int q_size(sock_queue *q);
sock_node *new_node(tju_tcp_t *);
tju_tcp_t *q_pop(sock_queue *q);
int q_push(sock_queue *q, tju_tcp_t *sock);
#endif