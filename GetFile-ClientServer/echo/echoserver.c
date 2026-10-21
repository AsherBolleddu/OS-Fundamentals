#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFSIZE 1024

#define USAGE                                                                  \
  "usage:\n"                                                                   \
  "  echoserver [options]\n"                                                   \
  "options:\n"                                                                 \
  "  -p                  Port (Default: 53665)\n"                              \
  "  -m                  Maximum pending connections (default: 5)\n"           \
  "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"maxnpending", required_argument, NULL, 'm'},
    {"help", no_argument, NULL, 'h'},
    {"port", required_argument, NULL, 'p'},
    {NULL, 0, NULL, 0}};

int main(int argc, char **argv) {
  int option_char;
  int portno = 53665; /* port to listen on */
  int maxnpending = 5;

  // Parse and set command line arguments
  while ((option_char =
              getopt_long(argc, argv, "p:m:hx", gLongOptions, NULL)) != -1) {
    switch (option_char) {
    case 'm': // server
      maxnpending = atoi(optarg);
      break;
    case 'h': // help
      fprintf(stdout, "%s ", USAGE);
      exit(0);
      break;
    case 'p': // listen-port
      portno = atoi(optarg);
      break;
    default:
      fprintf(stderr, "%s ", USAGE);
      exit(1);
    }
  }

  setbuf(stdout, NULL); // disable buffering

  if ((portno < 1025) || (portno > 65535)) {
    fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__,
            portno);
    exit(1);
  }
  if (maxnpending < 1) {
    fprintf(stderr, "%s @ %d: invalid pending count (%d)\n", __FILE__, __LINE__,
            maxnpending);
    exit(1);
  }

  /* Socket Code Here */
  int r;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo *address;
  char portstr[6];
  snprintf(portstr, sizeof(portstr), "%d", portno);
  if ((r = getaddrinfo(0, portstr, &hints, &address)) != 0) {
    fprintf(stderr, "getaddrinfo() failed. (%s)\n", gai_strerror(r));
    return EXIT_FAILURE;
  }

  int server_fd =
      socket(address->ai_family, address->ai_socktype, address->ai_protocol);
  if (server_fd == -1) {
    fprintf(stderr, "socket() failed. (%s)\n", strerror(errno));
    freeaddrinfo(address);
    return EXIT_FAILURE;
  }

  int yes = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) ==
      -1) {
    fprintf(stderr, "setsockopt() failed. (%s)\n", strerror(errno));
    close(server_fd);
    freeaddrinfo(address);
    return EXIT_FAILURE;
  }

  int no = 0;
  if (setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no)) == -1) {
    fprintf(stderr, "setsockopt() failed. (%s)\n", strerror(errno));
    close(server_fd);
    freeaddrinfo(address);
    return EXIT_FAILURE;
  }

  if (bind(server_fd, address->ai_addr, address->ai_addrlen) == -1) {
    fprintf(stderr, "bind() failed. (%s)\n", strerror(errno));
    close(server_fd);
    freeaddrinfo(address);
    return EXIT_FAILURE;
  }

  freeaddrinfo(address);

  if (listen(server_fd, maxnpending) == -1) {
    fprintf(stderr, "listen() failed. (%s)\n", strerror(errno));
    close(server_fd);
    return EXIT_FAILURE;
  }

  signal(SIGPIPE, SIG_IGN);

  while (1) {
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd == -1) {
      continue;
    }

    char response[BUFSIZE];
    ssize_t bytes_received;
    bool send_failed = false;
    while (!send_failed && (bytes_received = recv(client_fd, response,
                                                  sizeof(response), 0)) > 0) {
      ssize_t begin = 0;
      while (begin < bytes_received) {
        ssize_t bytes_sent =
            send(client_fd, response + begin, bytes_received - begin, 0);
        if (bytes_sent == -1) {
          fprintf(stderr, "send() failed. (%s)\n", strerror(errno));
          send_failed = true;
          break;
        }
        begin += bytes_sent;
      }
    }

    close(client_fd);
  }

  close(server_fd);
  return 0;
}
