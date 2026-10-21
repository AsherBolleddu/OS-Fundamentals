#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
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
  "  transferclient [options]\n"                                               \
  "options:\n"                                                                 \
  "  -p                  Port (Default: 7247)\n"                               \
  "  -s                  Server (Default: localhost)\n"                        \
  "  -h                  Show this help message\n"                             \
  "  -o                  Output file (Default cs6200.txt)\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {{"server", required_argument, NULL, 's'},
                                       {"output", required_argument, NULL, 'o'},
                                       {"help", no_argument, NULL, 'h'},
                                       {"port", required_argument, NULL, 'p'},
                                       {NULL, 0, NULL, 0}};

/* Main ========================================================= */
int main(int argc, char **argv) {
  int option_char = 0;
  char *hostname = "localhost";
  unsigned short portno = 7247;
  char *filename = "cs6200.txt";

  setbuf(stdout, NULL);

  // Parse and set command line arguments
  while ((option_char =
              getopt_long(argc, argv, "s:p:o:hx", gLongOptions, NULL)) != -1) {
    switch (option_char) {
    case 's': // server
      hostname = optarg;
      break;
    case 'p': // listen-port
      portno = atoi(optarg);
      break;
    default:
      fprintf(stderr, "%s", USAGE);
      exit(1);
    case 'o': // filename
      filename = optarg;
      break;
    case 'h': // help
      fprintf(stdout, "%s", USAGE);
      exit(0);
      break;
    }
  }

  if (NULL == hostname) {
    fprintf(stderr, "%s @ %d: invalid host name\n", __FILE__, __LINE__);
    exit(1);
  }

  if (NULL == filename) {
    fprintf(stderr, "%s @ %d: invalid filename\n", __FILE__, __LINE__);
    exit(1);
  }

  if ((portno < 1025) || (portno > 65535)) {
    fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__,
            portno);
    exit(1);
  }

  /* Socket Code Here */
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *address;
  char portstr[6];
  snprintf(portstr, sizeof(portstr), "%d", portno);
  int r = getaddrinfo(hostname, portstr, &hints, &address);
  if (r != 0) {
    fprintf(stderr, "getaddrinfo() failed. (%s)\n", gai_strerror(r));
    return EXIT_FAILURE;
  }

  struct addrinfo *ptr;
  int client_fd;
  for (ptr = address; ptr != NULL; ptr = ptr->ai_next) {
    client_fd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if (client_fd == -1)
      continue;

    if (connect(client_fd, ptr->ai_addr, ptr->ai_addrlen) == 0)
      break;

    close(client_fd);
  }

  freeaddrinfo(address);

  if (ptr == NULL) {
    fprintf(stderr, "No connections found\n");
    return EXIT_FAILURE;
  }

  int file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR);
  if (file_fd == -1) {
    fprintf(stderr, "open() failed. (%s)\n", strerror(errno));
    close(client_fd);
    return EXIT_FAILURE;
  }

  char buf[BUFSIZE];
  ssize_t bytes_received;
  while ((bytes_received = recv(client_fd, buf, sizeof(buf), 0)) > 0) {
    ssize_t begin = 0;
    while (begin < bytes_received) {
      ssize_t written = write(file_fd, buf + begin, bytes_received - begin);
      if (written <= 0) {
        fprintf(stderr, "write() failed. (%s)\n", strerror(errno));
        close(file_fd);
        close(client_fd);
        return EXIT_FAILURE;
      }
      begin += written;
    }
  }

  if (bytes_received == -1) {
    fprintf(stderr, "recv() failed. (%s)\n", strerror(errno));
    close(file_fd);
    close(client_fd);
    return EXIT_FAILURE;
  }

  close(file_fd);
  close(client_fd);
  return 0;
}
