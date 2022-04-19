#include "linkedlist.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

struct node *state;
pthread_mutex_t state_lock;


short SocketCreate(void) {
  short hSocket;
  hSocket = socket(AF_INET, SOCK_STREAM, 0);
  return hSocket;
}

int BindCreatedSocket(int hSocket, int ClientPort) {
  int iRetval = -1;
  struct sockaddr_in remote = {0};
  
  remote.sin_family = AF_INET;
  remote.sin_addr.s_addr = htonl(INADDR_ANY);
  remote.sin_port = htons(ClientPort);
  iRetval = bind(hSocket, (struct sockaddr *)&remote, sizeof(remote));
  return iRetval;
}

// Try to connect with server
int SocketConnect(int hSocket, in_addr_t addr, int port) {
  int iRetval = -1;
  struct sockaddr_in remote = {0};
  remote.sin_addr.s_addr = addr;
  remote.sin_family = AF_INET;
  remote.sin_port = htons(port);
  iRetval =
      connect(hSocket, (struct sockaddr *)&remote, sizeof(struct sockaddr_in));
  return iRetval;
}

// Send the data to the server and set the timeout of 20 seconds
int SocketSend(int hSocket, char *Rqst, short lenRqst) {
  int shortRetval = -1;
  struct timeval tv;
  tv.tv_sec = 20; /* 20 Secs Timeout */
  tv.tv_usec = 0;
  if (setsockopt(hSocket, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv)) <
      0) {
    fprintf(stderr, "Time Out\n");
    return -1;
  }
  shortRetval = send(hSocket, Rqst, lenRqst, 0);
  return shortRetval;
}

// Receive the data from the server
int SocketReceive(int hSocket, char *Rsp, short RvcSize) {
  int shortRetval = -1;
  struct timeval tv;
  tv.tv_sec = 20; /* 20 Secs Timeout */
  tv.tv_usec = 0;
  if (setsockopt(hSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv)) <
      0) {
    fprintf(stderr, "Time Out\n");
    return -1;
  }
  shortRetval = recv(hSocket, Rsp, RvcSize, 0);
  return shortRetval;
}

// Handle the failover of all clients from a given server
void failover(struct sockaddr_in serverIp) {
  pthread_mutex_lock(&state_lock);
  struct node *server =
      find_server_connections(state, serverIp.sin_addr.s_addr);
  pthread_mutex_unlock(&state_lock);

  // If it has connections
  if (server->connections != NULL) {
    struct cll *cursor = server->connections;
    while (cursor != NULL) {

      in_addr_t client_ip = cursor->client_ip;
      char filename[50];
      strcpy(filename, cursor->filename);

      char FAILOVERREQUEST[255] = "failover request";
      char FAILOVERREPLY[255];
      int hSocket;

      // Create socket
      hSocket = SocketCreate();
      if (hSocket == -1) {
        fprintf(stderr, "(Failover) Could not create socket\n");
        return;
      }
      fprintf(stderr, "(Failover) Socket is created\n");

      // Connect to remote server, port 2000 is the client's failover port
      if (SocketConnect(hSocket, client_ip, 2000) < 0) {
        fprintf(stderr, "(Failover) Connect failed.\n");
        return;
      }
      fprintf(stderr, "(Failover) Sucessfully conected with client\n");

      // Send data to the server
      SocketSend(hSocket, FAILOVERREQUEST, strlen(FAILOVERREQUEST));

      // Received the data from the server
      SocketReceive(hSocket, FAILOVERREPLY, 200);

      close(hSocket);
      cursor = cursor->next;
    }
  }
}

struct HeartbeatConnectionArgs {
  int socket;
  struct sockaddr_in serverIp;
};

void *HeartbeatListeningReading(void *args) {
  struct HeartbeatConnectionArgs *hcargs = args;

  char message[200];

  int shortRetval = -1;
  struct timeval tv;
  tv.tv_sec = 2; /* 2 Secs Timeout */
  tv.tv_usec = 0;
  if (setsockopt(hcargs->socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,
                 sizeof(tv)) < 0) {
    fprintf(stderr, "Time Out\n");
    return NULL;
  }
  while (1) {
    // Receive a reply from the server
    shortRetval = -1;
    shortRetval = recv(hcargs->socket, message, 200, 0);
    if (shortRetval < 0) {
      break;
    } else if (strcmp(message, "Heartbeat") == 0) {
      fprintf(stderr, "(Heartbeat Listening) Heartbeat received\n");
    } else {
      break;
    }
    message[0] = 0;
  }

  fprintf(
      stderr,
      "(Heartbeat Listening) Heartbeat not received, initiating failovers\n");
  failover(hcargs->serverIp);
  return NULL;
}

struct ServerNetwork {
  in_addr_t *ip_list;
  int ip_count;
  int port;
};

void *HeartbeatListening(void *args) {
  struct ServerNetwork *hargs = args;
  int listenSocket = SocketCreate();

  if (listenSocket == -1) {
    fprintf(stderr, "(Heartbeat Listening) Could not create socket\n");
    return NULL;
  }
  fprintf(stderr, "(Heartbeat Listening) Socket created\n");
  // Bind
  if (BindCreatedSocket(listenSocket, hargs->port) < 0) {
    fprintf(stderr, "(Heartbeat Listening) Bind failed\n");
    return NULL;
  }
  fprintf(stderr, "(Heartbeat Listening) Socket binded\n");
  listen(listenSocket, hargs->ip_count);
  fprintf(stderr, "(Heartbeat Listening) Socket listening for %d ips\n",
          hargs->ip_count);
  while (1) {
    struct sockaddr_in otherServer;
    int otherServerLen = sizeof(struct sockaddr_in);

    // accept connection from an incoming server
    int sock = accept(listenSocket, (struct sockaddr *)&otherServer,
                      (socklen_t *)&otherServerLen);
    if (sock < 0) {
      fprintf(stderr, "(Heartbeat Listening) Socket accept failed\n");
      return NULL;
    }
    fprintf(stderr, "(Heartbeat Listening) Socket connection accepted\n");

    pthread_t receivingThread;
    struct HeartbeatConnectionArgs hcargs;
    hcargs.serverIp = otherServer;
    hcargs.socket = sock;

    int foundServer = 0;
    for (int i = 0; i < hargs->ip_count; i++) {
      // Found ip in list so this is an allowed server
      if (hargs->ip_list[i] == otherServer.sin_addr.s_addr) {
        foundServer = 1;
        break;
      }
    }
    if (foundServer) {
      pthread_create(&receivingThread, NULL, HeartbeatListeningReading,
                     &hcargs);
      fprintf(stderr, "(Heartbeat Listening) Receiving started\n");
    } else {
      fprintf(stderr, "(Heartbeat Listening) Error: Weird ip\n");
    }
  }
  free(args);
}

struct HeartbeatSendingArgs {
  in_addr_t *ip_list;
  int ipcount;
  int port;
};

void *HeartbeatSending(void *args) {
  struct HeartbeatSendingArgs *hsargs = args;
  sleep(2); // Delay to ensure other servers are listening

  // Set up connections with every other server
  int hsSocket[hsargs->ipcount];
  for (int i = 0; i < hsargs->ipcount; i++) {
    // Create socket
    hsSocket[i] = SocketCreate();
    if (hsSocket[i] == -1) {
      fprintf(stderr, "(Heartbeat Sending) Could not create socket\n");
      return NULL;
    }
    fprintf(stderr, "(Heartbeat Sending) Socket created\n");

    // Connect to other server
    if (SocketConnect(hsSocket[i], hsargs->ip_list[i], hsargs->port) < 0) {
      fprintf(stderr, "(Heartbeat Sending) Connect failed\n");
      return NULL;
    }
    fprintf(stderr,
            "(Heartbeat Sending) Sucessfully connected with other server\n");
  }

  while (1) {
    // Send a heartbeat to every other server
    for (int i = 0; i < hsargs->ipcount; i++) {
      char *message = "Heartbeat";
      SocketSend(hsSocket[i], message, strlen(message));
      fprintf(stderr, "(Heartbeat Sending) Sent a heartbeat\n");
    }

    sleep(1);
  }
  free(args);
}

void heartbeat(struct ServerNetwork args) {
  pthread_t listenThread, sendThread;

  struct ServerNetwork *args1;
  args1 = malloc(sizeof(struct ServerNetwork));
  args1->ip_list = args.ip_list;
  args1->port = args.port;
  args1->ip_count = args.ip_count;

  struct ServerNetwork *args2;
  args2 = malloc(sizeof(struct ServerNetwork));
  args2->ip_list = args.ip_list;
  args2->port = args.port;
  args2->ip_count = args.ip_count;

  pthread_create(&listenThread, NULL, HeartbeatListening, args1);
  pthread_create(&sendThread, NULL, HeartbeatSending, args2);
}

struct SyncConnectionArgs {
  int socket;
  struct sockaddr_in serverIp;
};

void *SyncListeningReading(void *args) {
  struct SyncConnectionArgs *scargs = args;

  char message[200];

  int shortRetval = -1;
  struct timeval tv;
  tv.tv_sec = 2; /* 2 Secs Timeout */
  tv.tv_usec = 0;
  if (setsockopt(scargs->socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,
                 sizeof(tv)) < 0) {
    fprintf(stderr, "Time Out\n");
    return NULL;
  }
  while (1) {
    // Receive a reply from the server
    shortRetval = -1;
    shortRetval = recv(scargs->socket, message, 200, 0);
    if (shortRetval < 0) {
      break;
    } else if (strcmp(message, "Sync") == 0) {
      fprintf(stderr, "(Sync Listening) Sync received\n");
    } else {
      break;
    }
    message[0] = 0;
  }

  fprintf(stderr, "(Sync Listening) Sync not received. Server assumed down\n");
  failover(scargs->serverIp);
  return NULL;
}

struct SyncArgs {
  in_addr_t *ip_list;
  int ipcount;
  int port;
};

void *SyncListening(void *args) {
  struct SyncArgs *sargs = args;
  int listenSocket = SocketCreate();

  if (listenSocket == -1) {
    fprintf(stderr, "(Sync Listening) Could not create socket\n");
    return NULL;
  }
  fprintf(stderr, "(Sync Listening) Socket created\n");
  // Bind
  if (BindCreatedSocket(listenSocket, sargs->port) < 0) {
    fprintf(stderr, "(Sync Listening) Bind failed.\n");
    return NULL;
  }
  fprintf(stderr, "(Sync Listening) Socket binded\n");
  listen(listenSocket, sargs->ipcount);
  fprintf(stderr, "(Sync Listening) Socket listening for %d ips\n",
          sargs->ipcount);
  while (1) {
    struct sockaddr_in otherServer;
    int otherServerLen = sizeof(struct sockaddr_in);

    // accept connection from an incoming server
    int sock = accept(listenSocket, (struct sockaddr *)&otherServer,
                      (socklen_t *)&otherServerLen);
    if (sock < 0) {
      fprintf(stderr, "(Sync Listening) Socket accept failed\n");
      return NULL;
    }
    fprintf(stderr, "(Sync Listening) Socket connection accepted\n");

    int foundServer = 0;
    for (int i = 0; i < sargs->ipcount; i++) {
      // Found ip in list so this is an allowed server
      if (sargs->ip_list[i] == otherServer.sin_addr.s_addr) {
        foundServer = 1;
        break;
      }
    }
    if (foundServer) {
      in_addr_t client_ip;
      in_port_t client_port;
      size_t filenameLength;
      int isNew; // 1 for new connection, 0 for finished connection

      int bytes_recvd;
      bytes_recvd = recv(sock, &client_ip, sizeof(in_addr_t), 0);
      if (bytes_recvd < 0) {
        fprintf(stderr, "(Sync Listening) Error: Receive client ip failed\n");
        continue;
      }

      bytes_recvd = recv(sock, &client_port, sizeof(in_port_t), 0);
      if (bytes_recvd < 0) {
        fprintf(stderr, "(Sync Listening) Error: Receive client port failed\n");
        continue;
      }

      bytes_recvd = recv(sock, &filenameLength, sizeof(size_t), 0);
      if (bytes_recvd < 0) {
        fprintf(stderr,
                "(Sync Listening) Error: Receive file name length failed\n");
        continue;
      }

      char filename[filenameLength];
      bytes_recvd = recv(sock, filename, filenameLength, 0);
      if (bytes_recvd < 0) {
        fprintf(stderr,
                "(Sync Listening) Error: Receive file name failed (filename "
                "length: %lu)\n",
                filenameLength);
        continue;
      }
      filename[bytes_recvd] = 0;

      bytes_recvd = recv(sock, &isNew, sizeof(isNew), 0);
      if (bytes_recvd < 0) {
        fprintf(stderr, "(Sync Listening) Error: Receive open/closed failed\n");
        continue;
      }

      pthread_mutex_lock(&state_lock);
      node *serverNode =
          find_server_connections(state, otherServer.sin_addr.s_addr);
      // is server added yet?
      if (serverNode == NULL) {
        state = add_server(state, otherServer.sin_addr.s_addr, NULL);
      }

      // get ips for printing
      char client_ip_str[40];
      inet_ntop(AF_INET, &client_ip, client_ip_str, sizeof(client_ip_str));
      char server_ip_str[40];
      inet_ntop(AF_INET, &otherServer.sin_addr.s_addr, server_ip_str,
                sizeof(server_ip_str));

      // is event a connection removal?
      if (isNew == 0) {
        fprintf(stderr, "(Sync Listening) Removing %s:%d from %s for file %s\n",
                client_ip_str, ntohs(client_port), server_ip_str, filename);
        remove_connection(state, otherServer.sin_addr.s_addr, client_ip,
                          client_port, filename);
      } else {
        // Add connection
        fprintf(stderr, "(Sync Listening) Adding %s:%d to %s for file %s\n",
                client_ip_str, ntohs(client_port), server_ip_str, filename);
        state = add_connection(state, otherServer.sin_addr.s_addr, client_ip,
                               client_port, filename);
      }

      pthread_mutex_unlock(&state_lock);

    } else {
      fprintf(stderr, "(Sync Listening) Error: Weird ip\n");
    }
  }
  free(args);
}

void syncsetup(struct ServerNetwork args) {
  pthread_t listenThread;

  struct ServerNetwork *args1;
  args1 = malloc(sizeof(struct ServerNetwork));
  args1->ip_list = args.ip_list;
  args1->port = args.port;
  args1->ip_count = args.ip_count;

  pthread_create(&listenThread, NULL, SyncListening, args1);
}

void SyncSend(in_addr_t *otherServers, int otherServersLength,
              in_port_t sync_port, in_addr_t client_ip, in_port_t client_port,
              size_t filenameLength, char *filename, int isNew) {
  // Connect to all other servers and notify of new or finished connection

  int sock[otherServersLength];
  for (int i = 0; i < otherServersLength; i++) {
    // Create socket
    sock[i] = SocketCreate();
    if (sock[i] == -1) {
      fprintf(stderr, "(Sync Sending) Could not create socket\n");
      return;
    }
    fprintf(stderr, "(Sync Sending) Socket is created\n");

    // Connect to other server
    if (SocketConnect(sock[i], otherServers[i], sync_port) < 0) {
      fprintf(stderr, "(Sync Sending) Connect failed\n");
      return;
    }
    char server_ip_str[40];
    inet_ntop(AF_INET, &otherServers[i], server_ip_str, sizeof(server_ip_str));
    fprintf(stderr, "(Sync Sending) Sucessfully connected with server %s\n",
            server_ip_str);

    // Send sync message
    send(sock[i], &client_ip, sizeof(client_ip), 0);
    send(sock[i], &client_port, sizeof(client_port), 0);
    send(sock[i], &filenameLength, sizeof(filenameLength), 0);
    send(sock[i], filename, filenameLength, 0);
    send(sock[i], &isNew, sizeof(isNew), 0);
    fprintf(stderr, "(Sync Sending) Sent state change with file %s, type %d\n",
            filename, isNew);
  }
}

int main(int argc, char *argv[]) {
  int expected_args = 5;

  if (argc < expected_args) {
    fprintf(stderr, "Usage: %s primaryport heartbeatport syncport otherip...\n",
            argv[0]);
    exit(2);
  }

  int ip_count = argc - (expected_args-1);

  // Get other server ips
  in_addr_t ip_list[ip_count];
  for (int i = 0; i < ip_count; i++) {
    ip_list[i] = inet_addr(argv[i + expected_args - 1]);
  }

  // Prepare ips and port for heartbeat
  struct ServerNetwork hbnet;
  hbnet.ip_list = ip_list;
  hbnet.ip_count = ip_count;
  hbnet.port = atoi(argv[2]);

  // Initiate heartbeat mechanism
  heartbeat(hbnet);

  // Prepare ips and port for sync
  struct ServerNetwork syncnet;
  syncnet.ip_list = ip_list;
  syncnet.ip_count = ip_count;
  syncnet.port = atoi(argv[3]);
  syncsetup(syncnet);

  int socket_desc, sock, clientLen;
  struct sockaddr_in client;
  char client_message[200] = {0};
  char message[100] = {0};

  // Create socket
  fprintf(stderr, "(Transfer) Creating socket\n");
  socket_desc = SocketCreate();
  if (socket_desc == -1) {
    fprintf(stderr, "(Transfer) Could not create socket!\n");
    return 1;
  }
  fprintf(stderr, "(Transfer) Created socket\n");

  // Bind socket
  fprintf(stderr, "(Transfer) Binding socket\n");
  if (BindCreatedSocket(socket_desc, atoi(argv[1])) < 0) {
    fprintf(stderr, "(Transfer) Bind failed!\n");
    return 1;
  }
  fprintf(stderr, "(Transfer) Binded socket\n");

  // Listen
  listen(socket_desc, 3);
  fprintf(stderr, "(Transfer) Began listening\n");

  // Accept all incoming connections
  while (1) {
    clientLen = sizeof(struct sockaddr_in);

    // Accept connection from an incoming client
    fprintf(stderr, "(Transfer) Waiting for incoming connection from client\n");
    sock = accept(socket_desc, (struct sockaddr *)&client,
                  (socklen_t *)&clientLen);
    if (sock < 0) {
      fprintf(stderr, "Accept failed!");
      return 1;
    }
    fprintf(stderr, "(Transfer) Accepted connection from client\n");

    // Receive filename (client_message) from the client
    fprintf(stderr, "(Transfer) Receiving message\n");
    memset(client_message, '\0', sizeof client_message);
    memset(message, '\0', sizeof message);
    if (recv(sock, client_message, 200, 0) < 0) {
      fprintf(stderr, "(Transfer) recv failed\n");
      break;
    }

    // Set file name and length
    int filenameLength = strlen(client_message);
    char *filename = client_message;

    // Get the client address
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    getpeername(sock, (struct sockaddr *)&sin, &len);
    fprintf(stderr, "(Transfer) Client Port Number: %d\n", ntohs(sin.sin_port));

    // Client message
    fprintf(stderr, "(Transfer) Received message: %s\n", client_message);

    // Tell other servers about the new connection
    SyncSend(ip_list, ip_count, syncnet.port, sin.sin_addr.s_addr, sin.sin_port,
             filenameLength, filename, 1);

    // Read file
    fprintf(stderr, "(Transfer) Reading requested file\n");
    FILE *fh = fopen(filename, "rb");
    off_t fileSize;
    unsigned char message[256];

    // Get Filesize
    fprintf(stderr, "(Transfer) Getting filesize\n");
    struct stat finfo;
    fstat(fileno(fh), &finfo);
    fileSize = finfo.st_size;

    // Send filesize (only) to client
    fprintf(stderr, "(Transfer) Sending filesize <%lu>\n", fileSize);
    write(sock, &fileSize, sizeof(off_t));

    // Receive amount left
    off_t amountReceived;
    recv(sock, &amountReceived, sizeof(off_t), 0);
    fprintf(stderr, "(Transfer) Received amount received by client <%lu>\n",
            amountReceived);

    fseek(fh, amountReceived, 1);

    // Send file
    fprintf(stderr, "(Transfer) Sending file...\n");
    memset(message, 0, sizeof(message));
    off_t bytes_read;
    while ((bytes_read = fread(message, 1, sizeof(message), fh)) > 0) {
      write(sock, message, bytes_read);
      memset(message, 0, sizeof(message));
      usleep(1000);
    }

    fclose(fh);
    printf("Finished reading file\n");
    close(sock);

    // Tell other servers that the connection has ended
    SyncSend(ip_list, ip_count, syncnet.port, sin.sin_addr.s_addr, sin.sin_port,
             filenameLength, filename, 0);
  }
  return 0;
}
