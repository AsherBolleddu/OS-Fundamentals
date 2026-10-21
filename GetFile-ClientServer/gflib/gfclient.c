#include "gfclient-student.h"
#include <stdbool.h>
#include <stdlib.h>

// Modify this file to implement the interface specified in
// gfclient.h.

struct gfcrequest_t {
  const char *server;
  const char *path;
  unsigned short port;
  void (*headerfunc)(void *, size_t, void *);
  void *headerarg;
  void (*writefunc)(void *, size_t, void *);
  void *writearg;

  gfstatus_t status;
  size_t filelen;
  size_t bytesreceived;
};

// optional function for cleaup processing.
void gfc_cleanup(gfcrequest_t **gfr) {
  free(*gfr);
  *gfr = NULL;
}

gfcrequest_t *gfc_create() {
  gfcrequest_t *gfcrequest = calloc(1, sizeof(gfcrequest_t));
  gfcrequest->status = GF_INVALID;
  return gfcrequest;
}

size_t gfc_get_filelen(gfcrequest_t **gfr) { return (*gfr)->filelen; }

gfstatus_t gfc_get_status(gfcrequest_t **gfr) { return (*gfr)->status; }

size_t gfc_get_bytesreceived(gfcrequest_t **gfr) {
  return (*gfr)->bytesreceived;
}

void gfc_global_init() {}

void gfc_global_cleanup() {}

int gfc_perform(gfcrequest_t **gfr) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *address;
  char porstr[6];
  snprintf(porstr, sizeof(porstr), "%d", (*gfr)->port);
  int r = getaddrinfo((*gfr)->server, porstr, &hints, &address);
  if (r != 0) {
    fprintf(stderr, "getaddrinfo() failed. (%s)\n", gai_strerror(r));
    return -1;
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
    return -1;
  }

  char header_req[8192];
  int length = snprintf(header_req, sizeof(header_req),
                        "GETFILE GET %s\r\n\r\n", (*gfr)->path);
  if (length < 0 || (size_t)length >= sizeof(header_req)) {
    close(client_fd);
    return -1;
  }

  int begin = 0;
  while (begin < length) {
    ssize_t bytes_sent = send(client_fd, header_req + begin, length - begin, 0);
    if (bytes_sent == -1) {
      fprintf(stderr, "send() failed. (%s)\n", strerror(errno));
      close(client_fd);
      return -1;
    }
    begin += bytes_sent;
  }

  char header_resp[4096];
  size_t offset = 0;
  char *end = NULL;
  while (offset < sizeof(header_resp) - 1) {
    ssize_t bytes_received = recv(client_fd, header_resp + offset,
                                  sizeof(header_resp) - offset - 1, 0);
    if (bytes_received <= 0)
      break;
    offset += bytes_received;
    header_resp[offset] = '\0';
    if ((end = strstr(header_resp, "\r\n\r\n")))
      break;
  }

  if (!end) {
    (*gfr)->status = GF_INVALID;
    close(client_fd);
    return -1;
  }

  char *body = end + 4;
  size_t body_len = offset - (body - header_resp);

  if ((*gfr)->headerfunc)
    (*gfr)->headerfunc(header_resp, body - header_resp, (*gfr)->headerarg);

  *end = '\0';
  char *scheme = strtok(header_resp, " ");
  char *status = strtok(NULL, " ");
  char *lenstr = strtok(NULL, " ");
  char *extra = strtok(NULL, " ");

  bool valid = scheme && status && !extra && strcmp(scheme, "GETFILE") == 0;

  if (valid) {
    if (strcmp(status, "OK") == 0) {
      (*gfr)->status = GF_OK;
      if (lenstr) {
        char *endptr;
        long long filelen = strtoll(lenstr, &endptr, 10);
        valid = *endptr == '\0' && filelen >= 0;
        if (valid)
          (*gfr)->filelen = (size_t)filelen;
      } else {
        valid = false;
      }
    } else if (strcmp(status, "FILE_NOT_FOUND") == 0) {
      (*gfr)->status = GF_FILE_NOT_FOUND;
      valid = !lenstr;
    } else if (strcmp(status, "ERROR") == 0) {
      (*gfr)->status = GF_ERROR;
      valid = !lenstr;
    } else {
      valid = false;
    }
  }

  if (!valid) {
    (*gfr)->status = GF_INVALID;
    close(client_fd);
    return -1;
  }

  if ((*gfr)->status != GF_OK) {
    close(client_fd);
    return 0;
  }

  (*gfr)->bytesreceived = 0;
  if (body_len > 0) {
    if ((*gfr)->writefunc)
      (*gfr)->writefunc(body, body_len, (*gfr)->writearg);
    (*gfr)->bytesreceived += body_len;
  }

  while ((*gfr)->bytesreceived < (*gfr)->filelen) {
    ssize_t bytes_received =
        recv(client_fd, header_resp, sizeof(header_resp), 0);
    if (bytes_received <= 0) {
      close(client_fd);
      return -1;
    }

    if ((*gfr)->writefunc)
      (*gfr)->writefunc(header_resp, bytes_received, (*gfr)->writearg);
    (*gfr)->bytesreceived += bytes_received;
  }

  close(client_fd);
  return 0;
}

void gfc_set_headerarg(gfcrequest_t **gfr, void *headerarg) {
  (*gfr)->headerarg = headerarg;
}

void gfc_set_server(gfcrequest_t **gfr, const char *server) {
  (*gfr)->server = server;
}

void gfc_set_path(gfcrequest_t **gfr, const char *path) { (*gfr)->path = path; }

void gfc_set_port(gfcrequest_t **gfr, unsigned short port) {
  (*gfr)->port = port;
}

void gfc_set_headerfunc(gfcrequest_t **gfr,
                        void (*headerfunc)(void *, size_t, void *)) {
  (*gfr)->headerfunc = headerfunc;
}

void gfc_set_writefunc(gfcrequest_t **gfr,
                       void (*writefunc)(void *, size_t, void *)) {
  (*gfr)->writefunc = writefunc;
}

void gfc_set_writearg(gfcrequest_t **gfr, void *writearg) {
  (*gfr)->writearg = writearg;
}

const char *gfc_strstatus(gfstatus_t status) {
  const char *strstatus = "UNKNOWN";

  switch (status) {

  case GF_OK: {
    strstatus = "OK";
  } break;

  case GF_FILE_NOT_FOUND: {
    strstatus = "FILE_NOT_FOUND";
  } break;

  case GF_INVALID: {
    strstatus = "INVALID";
  } break;

  case GF_ERROR: {
    strstatus = "ERROR";
  } break;
  }

  return strstatus;
}
