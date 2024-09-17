// 结构
#ifndef _TJU_BUFFER_H_
#define _TJU_BUFFER_H_
#include "kernel.h"

send_buf *create_buffer(tju_tcp_t *sock, char *data, int len);
void *send_from_buffer(void *socket);
void init_send_buf(send_buf **buffer);
void insert_send_buf_node_back(send_buf *buffer, int num, int flag, const char *data, int data_len);
void insert_send_buf_node_back(send_buf *buffer, int num, int flag, const char *data, int data_len);
char *buf_recving(tju_tcp_t *sock, char *pkt_data, int data_len);
int s_push(send_buf *buf, send_buf_node *node);
send_buf_node *s_pop(tju_tcp_t *sock);
bool window_send_is_full(tju_tcp_t *sock);
void handle_cumulative_ack(tju_tcp_t *sock, uint32_t pkt_src, uint32_t pke_dst, uint32_t pkt_seq, uint32_t ack_num, uint32_t plen, uint32_t adv_window);
#endif