#include "tju_queue.h"
sock_queue *q_init()
{
  sock_queue *s_queue = malloc(sizeof(sock_queue));
  s_queue->len = 0;
  s_queue->sock_head = NULL;
  s_queue->sock_end = NULL;
  return s_queue;
}
int q_size(sock_queue *q)
{
  return q->len;
}
sock_node *new_node(tju_tcp_t *sock)
{
  sock_node *sk_node = malloc(sizeof(sock_node));
  sk_node->node = sock;
  sk_node->next = NULL;
  return sk_node;
}

tju_tcp_t *q_pop(sock_queue *q)
{
  if (q->len)
  {
    q->len--;
    sock_node *free_it = q->sock_head;

    tju_tcp_t *ret = tju_socket();
    memcpy(ret, q->sock_head->node, sizeof(tju_tcp_t));
    q->sock_head = q->sock_head->next;
    if (q->sock_head == NULL)
    {
      q->sock_end = NULL;
    }
    free(free_it);

    return ret;
  }
  // printf("队列空\n");
  return NULL;
}
int q_push(sock_queue *q, tju_tcp_t *sock)
{
  if (q->sock_end)
  {
    q->len++;
    sock_node *sk_node = new_node(sock);
    q->sock_end->next = sk_node;
    q->sock_end = sk_node;
  }
  else
  {
    q->len++;
    sock_node *sk_node = new_node(sock);
    q->sock_end = q->sock_head = sk_node;
  }
  return 0;
}