#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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

cll *create_connection(in_addr_t c_ip, int c_port, char f[], cll *next) {
  cll *new_conns = (cll *)malloc(sizeof(cll));
  if (new_conns == NULL) {
    printf("Error creating a new node.\n");
    exit(0);
  }
  new_conns->client_ip = c_ip;
  new_conns->client_port = c_port;
  strcpy(new_conns->filename, f);
  new_conns->next = next;

  return new_conns;
}

node *create_server(in_addr_t sip, node *next, cll *conns) {
  node *new_node = (node *)malloc(sizeof(node));
  if (new_node == NULL) {
    fprintf(stderr, "Error creating a new node.\n");
    exit(0);
  }
  new_node->server_ip = sip;
  new_node->connections = conns;
  new_node->next = next;

  return new_node;
}

node *add_server(node *head, in_addr_t server_ip, cll *conns) {
  /* go to the last node */
  node *cursor = head;
  if (head == NULL) {
    return create_server(server_ip, NULL, conns);
  }

  while (cursor->next != NULL)
    cursor = cursor->next;

  /* create a new node */
  node *new_node = create_server(server_ip, NULL, conns);
  cursor->next = new_node;

  return head;
}

node *remove_server(node *head, in_addr_t sip) {

  node *current_node = head;
  node *previous_node = NULL;

  while (current_node != NULL) {

    // Check for server ip
    if (current_node->server_ip == sip) {

      if (current_node->next != NULL) {

        // case: node is at head
        if (previous_node == NULL) {

          head = current_node->next;

        }

        // case node is in middle somewhere
        else {

          previous_node->next = current_node->next;
        }

      }

      // case: node is at end of list
      else if (previous_node != NULL) {

        previous_node->next = NULL;

      }

      // case: node is only one is list
      else {

        head = NULL;
      }

      return head;
    }

    previous_node = current_node;
    current_node = current_node->next;
  }

  return head;
}

node *find_server_connections(node *head, in_addr_t sip) {

  node *cursor = head;
  while (cursor != NULL) {
    if (cursor->server_ip == sip)
      return cursor;
    cursor = cursor->next;
  }
  return NULL;
}

void remove_connection(node *head, in_addr_t sip, in_addr_t cip, int cport,
                       char fname[]) {

  node *current = find_server_connections(head, sip);
  cll *prev = NULL;

  int removed = 0;

  if (current != NULL && current->connections != NULL) {

    cll *cursor = current->connections;
    while (cursor != NULL) {
      if (cursor->client_ip == cip && cursor->client_port == cport &&
          strcmp(cursor->filename, fname) == 0) {
        removed = 1;
        if (prev == NULL) {
          current->connections = cursor->next;
        } else {
          prev->next = cursor->next;
        }
        break;
      }
      prev = cursor;
      cursor = cursor->next;
    }
  }
  if (removed == 0) {
    fprintf(stderr, "Error: didn't find connection to remove");
  }
}

node *add_connection(node *head, in_addr_t sip, in_addr_t cip, int cport,
                     char fname[]) {

  node *current = find_server_connections(head, sip);

  if (current == NULL) {
    return head;
  }

  cll *new_conn = create_connection(cip, cport, fname, NULL);

  if (current->connections != NULL) {

    cll *cursor = current->connections;
    while (cursor->next != NULL) {

      cursor = cursor->next;
    }

    cursor->next = new_conn;

  } else {

    current->connections = new_conn;
  }

  return head;
}

typedef void (*callback)(node *data);

void traverse(node *head, callback f) {
  node *cursor = head;
  while (cursor != NULL) {
    f(cursor);
    cursor = cursor->next;
  }
}

void display(node *n) {
  if (n != NULL) {

    fprintf(stderr, "[SERVER] %d \n", n->server_ip);

    cll *curr_conn = n->connections;

    if (curr_conn == NULL) {

      fprintf(stderr, "	[CONNECTION] HAS NO CONNECTIONS \n");

    }

    else {

      while (curr_conn != NULL) {

        fprintf(stderr, "\n	[CONNECTION] %d \n", n->connections->client_ip);
        fprintf(stderr, "	[CONNECTION] %d \n",
                n->connections->client_port);
        fprintf(stderr, "	[CONNECTION] %s \n", n->connections->filename);

        curr_conn = curr_conn->next;
      }
    }
  }
}
