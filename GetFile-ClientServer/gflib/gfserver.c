#include "gfserver-student.h"
#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>

// Modify this file to implement the interface specified in gfserver.h.

#define BUFSIZE 8192

struct gfserver_t {
  unsigned short port;
  int max_npending;
  gfh_error_t (*handler)(gfcontext_t **, const char *, void *);
  void *handlerarg;
};

struct gfcontext_t {
  int sock_fd;
};

void gfs_abort(gfcontext_t **ctx) {
  if (!ctx || !*ctx)
    return;
  close((*ctx)->sock_fd);
  free(*ctx);
  *ctx = NULL;
}

ssize_t gfs_send(gfcontext_t **ctx, const void *data, size_t len) {
  size_t begin = 0;
  while (begin < len) {
    ssize_t bytes_sent =
        send((*ctx)->sock_fd, (const char *)data + begin, len - begin, 0);
    if (bytes_sent == -1) {
      fprintf(stderr, "send() failed. (%s)\n", strerror(errno));
      return -1;
    }
    begin += bytes_sent;
  }

  return begin;
}

ssize_t gfs_sendheader(gfcontext_t **ctx, gfstatus_t status, size_t file_len) {
  char header[64];
  int length = 0;

  switch (status) {
  case GF_OK:
    length =
        snprintf(header, sizeof(header), "GETFILE OK %zu\r\n\r\n", file_len);
    break;
  case GF_FILE_NOT_FOUND:
    length = snprintf(header, sizeof(header), "GETFILE FILE_NOT_FOUND\r\n\r\n");
    break;
  case GF_ERROR:
    length = snprintf(header, sizeof(header), "GETFILE ERROR\r\n\r\n");
    break;
  case GF_INVALID:
    length = snprintf(header, sizeof(header), "GETFILE INVALID\r\n\r\n");
    break;
  }

  int begin = 0;
  while (begin < length) {
    ssize_t bytes_sent =
        send((*ctx)->sock_fd, header + begin, length - begin, 0);
    if (bytes_sent == -1) {
      fprintf(stderr, "send() failed. (%s)\n", strerror(errno));
      return -1;
    }
    begin += bytes_sent;
  }

  return begin;
}

gfserver_t *gfserver_create() {
  gfserver_t *gfserver = calloc(1, sizeof(gfserver_t));
  return gfserver;
}

void gfserver_set_port(gfserver_t **gfs, unsigned short port) {
  (*gfs)->port = port;
}

void gfserver_serve(gfserver_t **gfs) {
  signal(SIGPIPE, SIG_IGN);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_INET6;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo *address;
  char portstr[6];
  snprintf(portstr, sizeof(portstr), "%d", (*gfs)->port);
  int r = getaddrinfo(0, portstr, &hints, &address);
  if (r != 0) {
    fprintf(stderr, "getaddrinfo() failed, (%s)\n", gai_strerror(r));
    return;
  }

  int server_fd =
      socket(address->ai_family, address->ai_socktype, address->ai_protocol);
  if (server_fd == -1) {
    fprintf(stderr, "socket() failed. (%s)\n", strerror(errno));
    freeaddrinfo(address);
    return;
  }

  int yes = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) ==
      -1) {
    fprintf(stderr, "setsockopt() failed. (%s)\n", strerror(errno));
    close(server_fd);
    freeaddrinfo(address);
    return;
  }

  int option = 0;
  if (setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &option,
                 sizeof(option)) == -1) {
    fprintf(stderr, "setsockopt() failed. (%s)\n", strerror(errno));
    close(server_fd);
    freeaddrinfo(address);
    return;
  }

  if (bind(server_fd, address->ai_addr, address->ai_addrlen) == -1) {
    fprintf(stderr, "bind() failed. (%s)\n", strerror(errno));
    close(server_fd);
    freeaddrinfo(address);
    return;
  }

  freeaddrinfo(address);

  if (listen(server_fd, (*gfs)->max_npending) == -1) {
    fprintf(stderr, "listen() failed. (%s)\n", strerror(errno));
    close(server_fd);
    return;
  }

  while (1) {
    int client_fd = accept(server_fd, 0, 0);
    if (client_fd == -1) {
      continue;
    }

    struct timeval tv = {.tv_sec = 3, .tv_usec = 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    gfcontext_t *gfcontext = calloc(1, sizeof(gfcontext_t));
    gfcontext->sock_fd = client_fd;

    char header[BUFSIZE];
    int begin = 0;
    char *end = NULL;
    while (begin < BUFSIZE - 1) {
      ssize_t bytes_received =
          recv(client_fd, header + begin, BUFSIZE - begin - 1, 0);
      if (bytes_received <= 0)
        break;
      begin += bytes_received;
      header[begin] = '\0';
      if ((end = strstr(header, "\r\n\r\n")))
        break;
    }

    bool valid = false;
    char *path = NULL;
    if (end) {
      *end = '\0';
      char *scheme = strtok(header, " ");
      char *method = strtok(NULL, " ");
      path = strtok(NULL, " ");
      char *extra = strtok(NULL, " ");

      valid = scheme && method && path && !extra &&
              strcmp(scheme, "GETFILE") == 0 && strcmp(method, "GET") == 0 &&
              path[0] == '/';
    }

    if (!valid) {
      gfs_sendheader(&gfcontext, GF_INVALID, 0);
    } else {
      (*gfs)->handler(&gfcontext, path, (*gfs)->handlerarg);
    }

    if (gfcontext) {
      close(gfcontext->sock_fd);
      free(gfcontext);
    } else {
      close(client_fd);
    }
  }
}

void gfserver_set_handlerarg(gfserver_t **gfs, void *arg) {
  (*gfs)->handlerarg = arg;
}

void gfserver_set_handler(gfserver_t **gfs,
                          gfh_error_t (*handler)(gfcontext_t **, const char *,
                                                 void *)) {
  (*gfs)->handler = handler;
}

void gfserver_set_maxpending(gfserver_t **gfs, int max_npending) {
  (*gfs)->max_npending = max_npending;
}
