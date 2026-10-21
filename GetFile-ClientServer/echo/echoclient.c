#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* Be prepared accept a response of this length */
#define BUFSIZE 1024

#define USAGE                                                                  \
  "usage:\n"                                                                   \
  "  echoclient [options]\n"                                                   \
  "options:\n"                                                                 \
  "  -p                  Port (Default: 53665)\n"                              \
  "  -s                  Server (Default: localhost)\n"                        \
  "  -m                  Message to send to server (Default: \"Hello "         \
  "Fall!!\")\n"                                                                \
  "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"message", required_argument, NULL, 'm'},
    {"port", required_argument, NULL, 'p'},
    {"help", no_argument, NULL, 'h'},
    {"server", required_argument, NULL, 's'},
    {NULL, 0, NULL, 0}};

/* Main ========================================================= */
int main(int argc, char **argv) {
  int option_char = 0;
  unsigned short portno = 53665;
  char *hostname = "localhost";
  char *message = "Hello Fall!!";

  // Parse and set command line arguments
  while ((option_char =
              getopt_long(argc, argv, "s:p:m:hx", gLongOptions, NULL)) != -1) {
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
    case 'm': // message
      message = optarg;
      break;
    case 'h': // help
      fprintf(stdout, "%s", USAGE);
      exit(0);
      break;
    }
  }

  setbuf(stdout, NULL); // disable buffering

  if ((portno < 1025) || (portno > 65535)) {
    fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__,
            portno);
    exit(1);
  }

  if (NULL == message) {
    fprintf(stderr, "%s @ %d: invalid message\n", __FILE__, __LINE__);
    exit(1);
  }

  if (NULL == hostname) {
    fprintf(stderr, "%s @ %d: invalid host name\n", __FILE__, __LINE__);
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

  /* Since AF_UNSPEC,
   * Loop through the Linked List to find a connection.
   * Has to be like this due to needing to work with IPv4 and IPv6.
   */

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

  if (send(client_fd, message, strlen(message), 0) == -1) {
    fprintf(stderr, "send() failed. (%s)\n", strerror(errno));
    close(client_fd);
    return EXIT_FAILURE;
  }

  char response[BUFSIZE];
  ssize_t bytes_received = recv(client_fd, response, sizeof(response), 0);
  if (bytes_received > 0) {
    fwrite(response, 1, bytes_received, stdout);
  }

  close(client_fd);
  return 0;
}
