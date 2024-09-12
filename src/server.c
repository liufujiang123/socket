#include "tju_tcp.h"
#include <string.h>

int main(int argc, char **argv)
{
  // 开启仿真环境
  startSimulation();
  // printf("startSimulation\n");

  tju_tcp_t *my_server = tju_socket();
  // printf("my_tcp state %d\n", my_server->state);

  tju_sock_addr bind_addr;
  bind_addr.ip = inet_network(SERVER_IP);
  bind_addr.port = 1234;
  tju_bind(my_server, bind_addr);
  // printf("my_server state %d\n", my_server->state);

  tju_listen(my_server);

  tju_tcp_t *new_conn = tju_accept(my_server);

  // printf("new_conn state %d\n", new_conn->state);

  // sleep(5);
  // printf("ready to send\n");
  // tju_send(new_conn, "hello world", 12);
  // tju_send(new_conn, "hello tju", 10);

  // char buf[2021];
  // tju_recv(new_conn, (void *)buf, 12);
  // printf("server recv %s\n", buf);

  // tju_recv(new_conn, (void *)buf, 10);
  // printf("server recv %s\n", buf);

  while (new_conn->state != CLOSE_WAIT)
    ;
  tju_close(new_conn);

  return EXIT_SUCCESS;
}
