#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

// Shared global client connection data
struct clientConnectionInfo {
  char filename[50];
  off_t amountReceived;
  in_addr_t server_ip;
  int server_port;
};

struct clientConnectionInfo sharedConnectionInfo;
pthread_mutex_t lock;

// Create a Socket for server communication
short SocketCreate(void) {
  short hSocket;
  hSocket = socket(AF_INET, SOCK_STREAM, 0);
  return hSocket;
}

// Binds a socket to a port
int BindCreatedSocket(int sock, int ClientPort) {
  int iRetval = -1;
  struct sockaddr_in remote = {0};

  remote.sin_family = AF_INET;
  remote.sin_addr.s_addr = htonl(INADDR_ANY);
  remote.sin_port = htons(ClientPort);
  iRetval = bind(sock, (struct sockaddr *)&remote, sizeof(remote));
  return iRetval;
}

// Receive and handle a failover request
void *FailoverReceiver() {
  int socket_desc, sock, serverLen;
  struct sockaddr_in server;
  char server_message[200] = {0};
  char message[100] = {0};

  // Create socket
  socket_desc = SocketCreate();

  // Check for failed socket creation
  if (socket_desc == -1) {
    fprintf(stderr, "(Client Failover) Could not create socket\n");
    close(socket_desc);
    pthread_exit(NULL);
  }
  fprintf(stderr, "(Client Failover) Socket created\n");

  // Ensure socket can be reused for subsequent failovers
  int on = 1;
  if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &(on), sizeof(on))) {
    fprintf(stderr, "(Client Failover) Could not set socket option\n");
  }

  // Bind
  if (BindCreatedSocket(socket_desc, 2000) < 0) {
    fprintf(stderr, "(Client Failover) Bind failed.\n");
    close(socket_desc);
    pthread_exit(NULL);
  }
  fprintf(stderr, "(Client Failover) Bind done\n");

  // Listen
  listen(socket_desc, 3);

  // Accept and incoming connection
  fprintf(stderr, "(Client Failover) Waiting for incoming connections...\n");

  // Accept connection from an incoming server
  serverLen = sizeof(struct sockaddr_in);
  sock =
      accept(socket_desc, (struct sockaddr *)&server, (socklen_t *)&serverLen);
  
  if (sock < 0) {
    fprintf(stderr, "(Client Failover) Accept failed");
  }

  fprintf(stderr, "(Client Failover) Connection accepted\n");
  memset(server_message, '\0', sizeof server_message);
  memset(message, '\0', sizeof message);

  // Receive a reply from the server
  if (recv(sock, server_message, 200, 0) < 0) {
    fprintf(stderr, "recv failed");
    close(socket_desc);
    close(sock);
    return NULL;
  }
  fprintf(stderr, "(Client Failover) Server reply : %s\n", server_message);

  // Send an empty message
  if (send(sock, message, strlen(message), 0) < 0) {
    fprintf(stderr, "(Client Failover) Send failed");
    close(sock);
    close(socket_desc);
    pthread_exit(NULL);
  }

  pthread_mutex_lock(&lock);

  // Log current location in file (amount of data received already)
  sharedConnectionInfo.server_ip = server.sin_addr.s_addr;
  fprintf(stderr, "(Client Failover) Amount Received: %lu\n",
          sharedConnectionInfo.amountReceived);

  // Log failover server detail
  char server_ip_str[40];
  inet_ntop(AF_INET, &sharedConnectionInfo.server_ip, server_ip_str,
            sizeof(server_ip_str));
  fprintf(stderr, "(Client Failover) Failover Server IP: %s\n", server_ip_str);
  fprintf(stderr, "(Client Failover) Failover Server Port: %d\n",
          ntohs(sharedConnectionInfo.server_port));

  pthread_mutex_unlock(&lock);

  // Close sockets now they are finished with
  close(sock);
  close(socket_desc);
  pthread_exit(NULL);
}

// Attempt to connect with server
int SocketConnect(int sock, in_addr_t addr, in_port_t port) {
  int iRetval = -1;
  struct sockaddr_in remote = {0};
  remote.sin_family = AF_INET;
  remote.sin_addr.s_addr = addr;
  remote.sin_port = port;
  iRetval =
      connect(sock, (struct sockaddr *)&remote, sizeof(struct sockaddr_in));
  return iRetval;
}

// Receieve a file, connecting to a failover server when needed
int clientReceive() {
  
  // Initialise and call failover receiver thread
  pthread_t mr_thread;
  pthread_create(&mr_thread, NULL, FailoverReceiver, 0);

  // Repeatedly make transfer attempts, until the entire file is downloaded
  while (1) {

    // Initialise initial shared variable values.
    pthread_mutex_lock(&lock);
    char *filename = sharedConnectionInfo.filename;
    in_addr_t serv_addr = sharedConnectionInfo.server_ip;
    off_t amountReceived = sharedConnectionInfo.amountReceived;
    in_port_t server_port = sharedConnectionInfo.server_port;
    pthread_mutex_unlock(&lock);

    // Create socket
    int sock;
    sock = SocketCreate();
    if (sock == -1) {
      fprintf(stderr, "(Transfer) Could not create socket\n");
      return 1;
    }
    fprintf(stderr, "(Transfer) Socket is created\n");

    // Connect to remote server
    char server_ip_str[40];
    inet_ntop(AF_INET, &serv_addr, server_ip_str, sizeof(server_ip_str));
    fprintf(stderr, "(Transfer) Server Address: %s\n", server_ip_str);
    fprintf(stderr, "(Transfer) Server Port: %d\n", ntohs(server_port));
    if (SocketConnect(sock, serv_addr, server_port) < 0) {
      fprintf(stderr, "(Transfer) connect failed.\n");
      return 1;
    }
    fprintf(stderr, "(Transfer) Sucessfully conected with server\n");

    // First send the filename to server
    fprintf(stderr, "(Transfer) Sending filename<%s>\n", filename);
    write(sock, filename, strlen(filename));

    // Receive file size from server
    off_t filesize;
    recv(sock, &filesize, sizeof(off_t), 0);
    fprintf(stderr, "(Transfer) Received file size <%s>\n", filename);

    // Then send the amountReceived to server
    fprintf(stderr, "(Transfer) Sending amount received already by client <%lu>\n",
            amountReceived);
    write(sock, &amountReceived, sizeof(off_t));

    // Make handler for output file
    FILE *fh = fopen(sharedConnectionInfo.filename, "a+b");

    // Receive file
    fprintf(stderr, "(Transfer) Receiving file...\n");
    unsigned char message[256];
    off_t amountLeft = filesize - amountReceived;
    size_t bytes_recvd;

    // Repeatedly receive packets from server until an empty packet is received
    while (1) {
      memset(message, 0, sizeof(message));
      bytes_recvd = recv(sock, message, sizeof(message), 0);

      amountLeft -= bytes_recvd;
      amountReceived += bytes_recvd;

      pthread_mutex_lock(&lock);
      sharedConnectionInfo.amountReceived = amountReceived;
      pthread_mutex_unlock(&lock);

      if (bytes_recvd <= 0)
        break;
      fwrite(message, 1, bytes_recvd, fh);
    }

    if (amountLeft <= 0) {
      fprintf(stderr, "(Transfer) Success!\n");
      fprintf(stderr, "(Transfer) Received file\n");
      return 0;
    } else {
      /* Amount left is strictly positive so a failover is needed to complete
         the transfer.
      */
      pthread_mutex_lock(&lock);
      fprintf(stderr, "(Transfer) Failover will occur! <%lu>\n",
              sharedConnectionInfo.amountReceived);
      pthread_mutex_unlock(&lock);

      // Wait for failover to occur, then start new failover thread
      pthread_join(mr_thread, NULL);
      pthread_create(&mr_thread, NULL, FailoverReceiver, 0);
    }

    // Tidy up file and sockets
    fclose(fh);
    close(sock);
    shutdown(sock, 0);
    shutdown(sock, 1);
    shutdown(sock, 2);
  }

  return 0;
}

// Main driver code
int main(int argc, char *argv[]) {
  struct timespec start, end;
  uint64_t total_time_us;
  // Get time in order to measure transfer time
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);

  if (argc != 4) {
    fprintf(stderr, "Usage: %s remote_ip remote_port filename\n", argv[0]);
    exit(2);
  }

  pthread_mutex_init(&lock, NULL); // Init lock for connection info

  // Set shared varible to initial values
  strcpy(sharedConnectionInfo.filename, argv[3]);
  sharedConnectionInfo.amountReceived = 0;
  sharedConnectionInfo.server_port = htons(atoi(argv[2]));
  if (inet_pton(AF_INET, argv[1], &sharedConnectionInfo.server_ip) <= 0) {
    fprintf(stderr, "\n(Transfer) inet_pton error occured\n");
    return 1;
  }

  // Receive the file
  int rv = clientReceive();

  // Calculate time taken
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);
  total_time_us = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
  fprintf(stderr, "(Transfer) Time taken: %lu microseconds\n", total_time_us);

  return rv;
}
