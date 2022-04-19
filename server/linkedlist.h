#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct node {
  in_addr_t server_ip;
  struct node *next;
  struct cll *connections;
} node;

typedef struct cll {
  in_addr_t client_ip;
  int client_port;
  char filename[50];
  struct cll *next;
} cll;

typedef void (*callback)(node *data);

cll *create_connection(int c_ip, int c_port, char f[], cll *next);

node *create_server(in_addr_t sip, node *next, cll *conns);

node *add_server(node *head, in_addr_t server_ip, cll *conns);

node *remove_server(node *head, in_addr_t sip);

node *find_server_connections(node *head, in_addr_t sip);

void remove_connection(node *head, in_addr_t sip, in_addr_t cip, int cport,
                       char fname[]);

node *add_connection(node *head, in_addr_t sip, in_addr_t cip, int cport,
                     char fname[]);

void traverse(node *head, callback f);

void display(node *n);
