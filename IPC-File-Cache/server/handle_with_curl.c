#include "gfserver.h"
#include "proxy-student.h"
#include <curl/curl.h>
#include <curl/easy.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REQUEST_N 512
#define BUFSIZE (6426)

typedef struct {
  char *memory; // Buffer holding everything downloaded so far
  size_t size;  // How many bytes are in the buffer
} memory_struct_t;

// Each time we have a new chunk, reallocate the buffer and append the new chunk
static size_t write_cb(char *contents, size_t size, size_t nmemb, void *userp) {
  size_t realSize = size * nmemb; // Number of bytes in this chunk
  memory_struct_t *mem = (memory_struct_t *)userp;

  char *ptr =
      realloc(mem->memory, mem->size + realSize + 1); // +1 for null character

  if (!ptr) {
    fprintf(stderr, "Out of memory\n");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&mem->memory[mem->size], contents, realSize);
  mem->size += realSize;
  mem->memory[mem->size] = 0; // Add null character to treat buffer as C-string
  return realSize;
}

ssize_t handle_with_curl(gfcontext_t *ctx, const char *path, void *arg) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return SERVER_FAILURE;

  char url[BUFSIZE];
  snprintf(url, sizeof(url), "%s%s", (char *)arg, path);

  memory_struct_t chunk;
  memset(&chunk, 0, sizeof(chunk));

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

  CURLcode result = curl_easy_perform(curl);
  if (result != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(result));
    curl_easy_cleanup(curl);
    free(chunk.memory);
    return SERVER_FAILURE;
  }

  long code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  /* Couldn't tell if returning the correct code or not
   * fprintf(stderr, "%s -> HTTP %ld, %zu bytes\n", url, code, chunk.size);*/
  curl_easy_cleanup(curl);

  if (code >= 400) {
    free(chunk.memory);
    return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
  }

  gfs_sendheader(ctx, GF_OK, chunk.size);

  ssize_t bytesSent = 0;
  if (chunk.size > 0)
    bytesSent = gfs_send(ctx, chunk.memory, chunk.size);

  free(chunk.memory);

  if (bytesSent != chunk.size)
    return SERVER_FAILURE;

  return bytesSent;
}

/*
 * We provide a dummy version of handle_with_file that invokes handle_with_curl
 * as a convenience for linking!
 */
ssize_t handle_with_file(gfcontext_t *ctx, const char *path, void *arg) {
  return handle_with_curl(ctx, path, arg);
}
