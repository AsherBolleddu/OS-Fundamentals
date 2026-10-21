#include "content.h"
#include "gfserver-student.h"
#include "gfserver.h"
#include "steque.h"
#include "workload.h"
#include <pthread.h>
#include <stdlib.h>

typedef struct gfrequest_t {
  gfcontext_t *ctx;
  char *path;
} gfrequest_t;

static steque_t queue;
static pthread_mutex_t mutexQueue;
static pthread_cond_t condQueue;
static pthread_t *threads;

static void *worker(void *args) {
  while (1) {
    pthread_mutex_lock(&mutexQueue);
    while (steque_isempty(&queue)) {
      pthread_cond_wait(&condQueue, &mutexQueue);
    }
    gfrequest_t *gfrequest = steque_pop(&queue);
    pthread_mutex_unlock(&mutexQueue);

    int fd = content_get(gfrequest->path);
    struct stat st;
    if (fd == -1) {
      gfs_sendheader(&gfrequest->ctx, GF_FILE_NOT_FOUND, 0);
    } else if (fstat(fd, &st) == -1) {
      gfs_sendheader(&gfrequest->ctx, GF_ERROR, 0);
    } else {
      gfs_sendheader(&gfrequest->ctx, GF_OK, st.st_size);
      if (st.st_size == 0) {
        gfs_abort(&gfrequest->ctx);
      }

      off_t offset = 0;
      char buf[4096];
      while (offset < st.st_size) {
        ssize_t bytes_read = pread(fd, buf, sizeof(buf), offset);
        if (bytes_read <= 0) {
          gfs_abort(&gfrequest->ctx);
          break;
        }

        if (gfs_send(&gfrequest->ctx, buf, bytes_read) < 0) {
          gfs_abort(&gfrequest->ctx);
          break;
        }
        offset += bytes_read;
      }
    }

    free(gfrequest->path);
    free(gfrequest);
  }

  return NULL;
}

void init_threads(size_t numthreads) {
  steque_init(&queue);
  pthread_mutex_init(&mutexQueue, NULL);
  pthread_cond_init(&condQueue, NULL);
  threads = malloc(sizeof(pthread_t) * numthreads);
  for (size_t i = 0; i < numthreads; ++i) {
    if (pthread_create(&threads[i], NULL, worker, NULL) != 0) {
      fprintf(stderr, "pthread_create failed\n");
      exit(EXIT_FAILURE);
    }
  }
}

void cleanup_threads() { free(threads); }

//
//  The purpose of this function is to handle a get request
//
//  The ctx is a pointer to the "context" operation and it contains connection
//  state The path is the path being retrieved The arg allows the registration
//  of context that is passed into this routine. Note: you don't need to use
//  arg. The test code uses it in some cases, but
//        not in others.
//
gfh_error_t gfs_handler(gfcontext_t **ctx, const char *path, void *arg) {
  gfrequest_t *gfrequest = malloc(sizeof(gfrequest_t));
  if (!gfrequest) {
    gfs_sendheader(ctx, GF_ERROR, 0);
    return gfh_failure;
  }

  gfrequest->ctx = *ctx;
  gfrequest->path = strdup(path);
  if (!gfrequest->path) {
    free(gfrequest);
    gfs_sendheader(ctx, GF_ERROR, 0);
    return gfh_failure;
  }

  pthread_mutex_lock(&mutexQueue);
  steque_enqueue(&queue, gfrequest);
  pthread_cond_signal(&condQueue);
  pthread_mutex_unlock(&mutexQueue);

  *ctx = NULL;

  return gfh_success;
}
