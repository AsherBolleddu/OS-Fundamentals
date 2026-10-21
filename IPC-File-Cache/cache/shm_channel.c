// In case you want to implement the shared memory IPC as a library
// This is optional but may help with code reuse
//
#include "shm_channel.h"
#include "gfserver.h"
#include "steque.h"
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static steque_t queue;
static pthread_mutex_t mutex_queue = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_queue = PTHREAD_COND_INITIALIZER;
static block_t **blocks;
static unsigned int num_blocks;
static size_t block_size;

static mqd_t mqdes = (mqd_t)-1;

// Proxy
void shm_pool_init(unsigned int nsegments, size_t segsize) {
  num_blocks = nsegments;
  block_size = segsize;
  blocks = calloc(nsegments, sizeof(block_t *));
  steque_init(&queue);
  char name[32];
  for (unsigned int i = 0; i < nsegments; ++i) {
    snprintf(name, sizeof(name), BLOCK_NAME_FMT, i);

    int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
      fprintf(stderr, "shm_open %s: %s\n", name, strerror(errno));
      shm_pool_destroy();
      exit(1);
    }

    if (ftruncate(shm_fd, segsize) == -1) {
      fprintf(stderr, "ftruncate %s: %s\n", name, strerror(errno));
      shm_pool_destroy();
      exit(1);
    }

    block_t *blk =
        mmap(NULL, segsize, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (blk == MAP_FAILED) {
      fprintf(stderr, "mmap %s: %s\n", name, strerror(errno));
      shm_pool_destroy();
      exit(1);
    }

    blocks[i] = blk;
    close(shm_fd);

    strncpy(blk->block_name, name, sizeof(blk->block_name));
    blk->block_size = segsize;
    sem_init(&blk->ready, 1, 0);
    sem_init(&blk->consume, 1, 0);

    steque_enqueue(&queue, blk);
  }
}

// Proxy
block_t *shm_pool_get(void) {
  pthread_mutex_lock(&mutex_queue);
  while (steque_isempty(&queue))
    pthread_cond_wait(&cond_queue, &mutex_queue);
  block_t *blk = steque_pop(&queue);
  pthread_mutex_unlock(&mutex_queue);

  return blk;
}

// Proxy
void shm_pool_put(block_t *block) {
  pthread_mutex_lock(&mutex_queue);
  steque_enqueue(&queue, block);
  pthread_mutex_unlock(&mutex_queue);
  pthread_cond_signal(&cond_queue);
}

// Proxy
void shm_pool_destroy(void) {
  char name[32];
  for (unsigned int i = 0; i < num_blocks; ++i) {
    snprintf(name, sizeof(name), BLOCK_NAME_FMT, i);
    if (blocks[i] != NULL) {
      sem_destroy(&blocks[i]->ready);
      sem_destroy(&blocks[i]->consume);
      munmap(blocks[i], block_size);
    }
    shm_unlink(name);
  }
  free(blocks);
  steque_destroy(&queue);
}

// Cache
void mq_channel_create(void) {
  struct mq_attr attr = {.mq_maxmsg = 10,
                         .mq_msgsize = sizeof(cache_request_t)};
  mqdes = mq_open(MQ_NAME, O_CREAT | O_RDONLY, 0666, &attr);
  if (mqdes == (mqd_t)-1) {
    fprintf(stderr, "mq_open: %s\n", strerror(errno));
    exit(1);
  }
}

// Proxy
void mq_channel_open(void) {
  while (1) {
    mqdes = mq_open(MQ_NAME, O_WRONLY);
    if (mqdes != (mqd_t)-1)
      return;

    if (errno != ENOENT) {
      fprintf(stderr, "mq_open: %s\n", strerror(errno));
      exit(1);
    }
    sleep(1);
  }
}

// Proxy
int mq_channel_send(cache_request_t *req) {
  return mq_send(mqdes, (char *)req, sizeof(*req), 0);
}

// Cache
int mq_channel_receive(cache_request_t *req) {
  return mq_receive(mqdes, (char *)req, sizeof(*req), NULL);
}

// Proxy
void mq_channel_close(void) {
  if (mqdes != (mqd_t)-1)
    mq_close(mqdes);
}

// Cache
void mq_channel_destroy(void) {
  if (mqdes != (mqd_t)-1) {
    mq_close(mqdes);
    mq_unlink(MQ_NAME);
  }
}

// Cache
block_t *block_attach(const char *name, size_t size) {
  int shm_fd = shm_open(name, O_RDWR, 0);
  if (shm_fd == -1) {
    fprintf(stderr, "shm_open %s: %s\n", name, strerror(errno));
    return NULL;
  }

  block_t *blk =
      mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  close(shm_fd);
  if (blk == MAP_FAILED) {
    fprintf(stderr, "mmap %s: %s\n", name, strerror(errno));
    return NULL;
  }

  return blk;
}

// Cache
void block_detach(block_t *blk, size_t size) { munmap(blk, size); }
