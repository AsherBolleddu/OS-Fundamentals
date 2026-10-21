#include "cache-student.h"
#include "gfserver.h"
#include "shm_channel.h"
#include <semaphore.h>
#include <stdio.h>
#include <string.h>

#define BUFSIZE (856)

ssize_t handle_with_cache(gfcontext_t *ctx, char *path, void *arg) {
  /* Proxy steps cause I keep forgetting the flow
   * 1. Grab a free block
   * 2. Build the request for the cache_request_t
   * 3. Send the request via message queue
   * 4. Wait for the cache to to fill the header data
   * 5. Send the header data to the client
   * 6. Send the data by chunks to the client
   */
  block_t *blk = shm_pool_get();

  cache_request_t req;
  memset(&req, 0, sizeof(req));
  snprintf(req.path, sizeof(req.path), "%s", path);
  snprintf(req.block_name, sizeof(req.block_name), "%s", blk->block_name);
  req.block_size = blk->block_size;

  if (mq_channel_send(&req) == -1) {
    shm_pool_put(blk);
    return SERVER_FAILURE;
  }

  sem_wait(&blk->ready); /* Wait for Cache to fill status and file size, Cache
                            will post this semaphore */

  if (blk->status != GF_OK) {
    shm_pool_put(blk);
    return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
  }

  size_t file_len = blk->file_size;
  gfs_sendheader(ctx, GF_OK, file_len);

  size_t bytes_sent = 0;
  while (bytes_sent < file_len) {
    sem_post(
        &blk->consume); // Proxy signals it's ready for the next chunk of data
    sem_wait(&blk->ready); // Wait for Cache to fill data

    if (blk->status != GF_OK) {
      shm_pool_put(blk);
      return SERVER_FAILURE;
    }

    ssize_t n = gfs_send(ctx, blk->data, blk->chunk_size);
    if (n != (ssize_t)blk->chunk_size) { // Client disconnected or socket error
      blk->abort = 1;
      sem_post(&blk->consume); // Proxy signals DC
      sem_wait(&blk->ready);   // Cache acknowledge DC
      blk->abort = 0;          // Reset
      shm_pool_put(blk);
      return SERVER_FAILURE;
    }
    bytes_sent += n;
  }

  shm_pool_put(blk);
  return bytes_sent;
}
