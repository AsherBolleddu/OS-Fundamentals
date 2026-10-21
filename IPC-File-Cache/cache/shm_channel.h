// In case you want to implement the shared memory IPC as a library
// You may use this file. It is optional. It does help with code reuse
#if !defined(SHM_CHANNEL_H)
#define SHM_CHANNEL_H

#include "gfserver.h"
#include <fcntl.h>
#include <mqueue.h>
#include <semaphore.h>
#include <stddef.h>

#define BLOCK_NAME_FMT "/block_%d"
#define MQ_NAME "/msg_queue"

typedef struct {
  char path[MAX_REQUEST_LEN]; // File name
  char block_name[32];        // Block name
  size_t block_size;          // How big
} cache_request_t;

typedef struct {
  sem_t ready;   // Binary semaphore. Cache posts -> "Data is ready"
  sem_t consume; // Binary semaphore. Proxy posts -> "Send next chunk of data"
  int status;    // GF Status
  int abort;     // Client disconnected -> Cache stops sending
  size_t file_size;    // File size. Cache updates this
  size_t chunk_size;   // How many bytes in this chunk
  size_t block_size;   // Size of block
  char block_name[32]; // Block name
  char data[];         // The actual content
} block_t;

void shm_pool_init(unsigned int nsegments, size_t segsize);
block_t *shm_pool_get(void);
void shm_pool_put(block_t *block);
void shm_pool_destroy(void);

void mq_channel_create(void);
void mq_channel_open(void);
int mq_channel_send(cache_request_t *req);
int mq_channel_receive(cache_request_t *req);
void mq_channel_close(void);
void mq_channel_destroy(void);
block_t *block_attach(const char *name, size_t size);
void block_detach(block_t *blk, size_t size);

#endif // SHM_CHANNEL_H
