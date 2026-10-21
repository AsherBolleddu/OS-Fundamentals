#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFSIZE 512

#define USAGE                                                                  \
  "usage:\n"                                                                   \
  "  transferserver [options]\n"                                               \
  "options:\n"                                                                 \
  "  -f                  Filename (Default: 6200.txt)\n"                       \
  "  -p                  Port (Default: 7247)\n"                               \
  "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"help", no_argument, NULL, 'h'},
    {"port", required_argument, NULL, 'p'},
    {"filename", required_argument, NULL, 'f'},
    {NULL, 0, NULL, 0}};

int main(int argc, char **argv) {
  int option_char;
  int portno = 7247;           /* port to listen on */
  char *filename = "6200.txt"; /* file to transfer */

  setbuf(stdout, NULL); // disable buffering

  // Parse and set command line arguments
  while ((option_char =
              getopt_long(argc, argv, "p:hf:x", gLongOptions, NULL)) != -1) {
    switch (option_char) {
    case 'p': // listen-port
      portno = atoi(optarg);
      break;
    case 'f': // file to transfer
      filename = optarg;
      break;
    case 'h': // help
      fprintf(stdout, "%s", USAGE);
      exit(0);
      break;
    default:
      fprintf(stderr, "%s", USAGE);
      exit(1);
    }
  }

  if ((portno < 1025) || (portno > 65535)) {
    fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__,
            portno);
    exit(1);
  }

  if (NULL == filename) {
    fprintf(stderr, "%s @ %d: invalid filename\n", __FILE__, __LINE__);
    exit(1);
  }

  /* Socket Code Here */
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo *address;
  char portstr[6];
  snprintf(portstr, sizeof(portstr), "%d", portno);
  int r = getaddrinfo(0, portstr, &hints, &address);
  if (r != 0) {
    fprintf(stderr, "getaddrinfo() failed, (%s)\n", gai_strerror(r));
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

  int option = 0;
  if (setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &option,
                 sizeof(option)) == -1) {
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

  if (listen(server_fd, 10) == -1) {
    fprintf(stderr, "listen() failed. (%s)\n", strerror(errno));
    close(server_fd);
    return EXIT_FAILURE;
  }

  signal(SIGPIPE, SIG_IGN);

  while (1) {
    int client_fd = accept(server_fd, 0, 0);
    if (client_fd == -1) {
      continue;
    }

    int file_fd = open(filename, O_RDONLY);
    if (file_fd == -1) {
      fprintf(stderr, "open() failed. (%s)", strerror(errno));
      close(client_fd);
      continue;
    }

    char buffer[BUFSIZE];
    ssize_t bytes_read;
    bool send_failed = false;
    while (!send_failed &&
           (bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
      ssize_t begin = 0;
      while (begin < bytes_read) {
        ssize_t bytes_sent =
            send(client_fd, buffer + begin, bytes_read - begin, 0);
        if (bytes_sent == -1) {
          fprintf(stderr, "send() failed. (%s)\n", strerror(errno));
          send_failed = true;
          break;
        }
        begin += bytes_sent;
      }
    }

    close(file_fd);
    close(client_fd);
  }

  close(server_fd);
  return 0;
}
