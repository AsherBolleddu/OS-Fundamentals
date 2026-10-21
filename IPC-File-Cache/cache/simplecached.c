#include "cache-student.h"
#include "gfserver.h"
#include "shm_channel.h"
#include "simplecache.h"
#include "steque.h"
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <printf.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <unistd.h>

// CACHE_FAILURE
#if !defined(CACHE_FAILURE)
#define CACHE_FAILURE (-1)
#endif

#define MAX_CACHE_REQUEST_LEN 6112
#define MAX_SIMPLE_CACHE_QUEUE_SIZE 783

unsigned long int cache_delay;
static steque_t req_queue;
static pthread_mutex_t rq_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rq_cond = PTHREAD_COND_INITIALIZER;

static void _sig_handler(int signo) {
  if (signo == SIGTERM || signo == SIGINT) {
    // This is where your IPC clean up should occur
    simplecache_destroy();
    mq_channel_destroy();
    steque_destroy(&req_queue);
    exit(signo);
  }
}

#define USAGE                                                                  \
  "usage:\n"                                                                   \
  "  simplecached [options]\n"                                                 \
  "options:\n"                                                                 \
  "  -c [cachedir]       Path to static files (Default: ./)\n"                 \
  "  -t [thread_count]   Thread count for work queue (Default is 8, Range is " \
  "1-100)\n"                                                                   \
  "  -d [delay]          Delay in simplecache_get (Default is 0, Range is "    \
  "0-2500000 (microseconds)\n "                                                \
  "  -h                  Show this help message\n"

// OPTIONS
static struct option gLongOptions[] = {
    {"cachedir", required_argument, NULL, 'c'},
    {"nthreads", required_argument, NULL, 't'},
    {"help", no_argument, NULL, 'h'},
    {"hidden", no_argument, NULL, 'i'},      /* server side */
    {"delay", required_argument, NULL, 'd'}, // delay.
    {NULL, 0, NULL, 0}};

void Usage() { fprintf(stdout, "%s", USAGE); }

static void handle_request(cache_request_t *req) {
  /* Cache steps cause I keep forgetting the flow
   * 1. Get the block data from request, and Cache attaches to the block
   * 2. Get the file descriptor via simplecache_get
   * 3. Get the file data via fstat
   * 4. Send the data
   * 5. Detach from the block
   */
  block_t *blk = block_attach(req->block_name, req->block_size);
  if (!blk)
    return;

  int fd = simplecache_get(req->path);
  if (fd == -1) {
    blk->status = GF_FILE_NOT_FOUND;
    sem_post(&blk->ready); // Cache signals didn't find file
    block_detach(blk, req->block_size);
    return;
  }

  struct stat statbuf;
  if (fstat(fd, &statbuf) == -1) {
    blk->status = GF_ERROR;
    sem_post(&blk->ready); // Cache signals error occurred
    block_detach(blk, req->block_size);
    return;
  }

  blk->status = GF_OK;
  blk->file_size = statbuf.st_size;
  sem_post(&blk->ready); // Cache signals the header data is ready

  size_t max_chunk =
      req->block_size - sizeof(block_t); // Remove size of the headers
  size_t offset = 0;
  while (offset < blk->file_size) {
    sem_wait(&blk->consume); // Wait for Proxy to be ready to consume data
    if (blk->abort) {
      sem_post(&blk->ready); // Cache acknowledge the DC
      break;
    }

    ssize_t n = pread(fd, blk->data, max_chunk, offset);
    if (n <= 0) {
      blk->status = GF_ERROR;
      sem_post(&blk->ready); // Cache signals failed read
      break;
    }

    blk->chunk_size = n;
    offset += n;
    sem_post(&blk->ready); // Cache signals data is ready to be sent
  }

  block_detach(blk, req->block_size);
}

static void *worker(void *arg) {
  while (1) {
    pthread_mutex_lock(&rq_lock);
    while (steque_isempty(&req_queue))
      pthread_cond_wait(&rq_cond, &rq_lock);
    cache_request_t *req = steque_pop(&req_queue);
    pthread_mutex_unlock(&rq_lock);

    handle_request(req);
    free(req);
  }

  return NULL;
}

int main(int argc, char **argv) {
  int nthreads = 6;
  char *cachedir = "locals.txt";
  char option_char;

  /* disable buffering to stdout */
  setbuf(stdout, NULL);

  while ((option_char = getopt_long(argc, argv, "d:ic:hlt:x", gLongOptions,
                                    NULL)) != -1) {
    switch (option_char) {
    default:
      Usage();
      exit(1);
    case 't': // thread-count
      nthreads = atoi(optarg);
      break;
    case 'h': // help
      Usage();
      exit(0);
      break;
    case 'c': // cache directory
      cachedir = optarg;
      break;
    case 'd':
      cache_delay = (unsigned long int)atoi(optarg);
      break;
    case 'i': // server side usage
    case 'o': // do not modify
    case 'a': // experimental
      break;
    }
  }

  if (cache_delay > 2500000) {
    fprintf(stderr, "Cache delay must be less than 2500000 (us)\n");
    exit(__LINE__);
  }

  if ((nthreads > 100) || (nthreads < 1)) {
    fprintf(stderr, "Invalid number of threads must be in between 1-100\n");
    exit(__LINE__);
  }
  if (SIG_ERR == signal(SIGINT, _sig_handler)) {
    fprintf(stderr, "Unable to catch SIGINT...exiting.\n");
    exit(CACHE_FAILURE);
  }
  if (SIG_ERR == signal(SIGTERM, _sig_handler)) {
    fprintf(stderr, "Unable to catch SIGTERM...exiting.\n");
    exit(CACHE_FAILURE);
  }
  /*Initialize cache*/
  simplecache_init(cachedir);

  steque_init(&req_queue);
  pthread_t workers[nthreads];
  for (int i = 0; i < nthreads; ++i)
    pthread_create(&workers[i], NULL, worker, NULL);

  // Cache should go here
  mq_channel_create();

  while (1) {
    cache_request_t *req = malloc(sizeof(*req));
    if (mq_channel_receive(req) == -1) {
      fprintf(stderr, "mq_receive: %s\n", strerror(errno));
      exit(1);
    }

    pthread_mutex_lock(&rq_lock);
    steque_enqueue(&req_queue, req);
    pthread_mutex_unlock(&rq_lock);
    pthread_cond_signal(&rq_cond);
  }

  // Line never reached
  return -1;
}
