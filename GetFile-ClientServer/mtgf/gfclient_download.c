#include "gfclient-student.h"
#include "steque.h"
#include <pthread.h>
#include <stdlib.h>

#define MAX_THREADS 1024
#define PATH_BUFFER_SIZE 512

#define USAGE                                                                  \
  "usage:\n"                                                                   \
  "  gfclient_download [options]\n"                                            \
  "options:\n"                                                                 \
  "  -h                  Show this help message\n"                             \
  "  -s [server_addr]    Server address (Default: 127.0.0.1)\n"                \
  "  -p [server_port]    Server port (Default: 63866)\n"                       \
  "  -w [workload_path]  Path to workload file (Default: workload.txt)\n"      \
  "  -t [nthreads]       Number of threads (Default 8 Max: 1024)\n"            \
  "  -n [num_requests]   Request download total (Default: 16)\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"nrequests", required_argument, NULL, 'n'},
    {"server", required_argument, NULL, 's'},
    {"workload", required_argument, NULL, 'w'},
    {"help", no_argument, NULL, 'h'},
    {"nthreads", required_argument, NULL, 't'},
    {"port", required_argument, NULL, 'p'},
    {NULL, 0, NULL, 0}};

typedef struct config_t {
  char *server;
  unsigned short port;
} config_t;

static steque_t queue;
static pthread_mutex_t mutexQueue;
static pthread_mutex_t mutexCounter;
static pthread_cond_t condQueue;
static pthread_t *threads;

static void Usage() { fprintf(stderr, "%s", USAGE); }

static void localPath(char *req_path, char *local_path) {
  static int counter = 0;
  pthread_mutex_lock(&mutexCounter);
  int n = counter++;
  pthread_mutex_unlock(&mutexCounter);

  sprintf(local_path, "%s-%06d", &req_path[1], n);
}

static FILE *openFile(char *path) {
  char *cur, *prev;
  FILE *ans;

  /* Make the directory if it isn't there */
  prev = path;
  while (NULL != (cur = strchr(prev + 1, '/'))) {
    *cur = '\0';

    if (0 > mkdir(&path[0], S_IRWXU)) {
      if (errno != EEXIST) {
        perror("Unable to create directory");
        exit(EXIT_FAILURE);
      }
    }

    *cur = '/';
    prev = cur;
  }

  if (NULL == (ans = fopen(&path[0], "w"))) {
    perror("Unable to open file");
    exit(EXIT_FAILURE);
  }

  return ans;
}

/* Callbacks ========================================================= */
static void writecb(void *data, size_t data_len, void *arg) {
  FILE *file = (FILE *)arg;
  fwrite(data, 1, data_len, file);
}

static void *worker(void *args) {
  config_t config = *(config_t *)args;
  while (1) {
    pthread_mutex_lock(&mutexQueue);
    while (steque_isempty(&queue)) {
      pthread_cond_wait(&condQueue, &mutexQueue);
    }

    char *req_path = steque_pop(&queue);
    pthread_mutex_unlock(&mutexQueue);

    if (!req_path)
      break;

    if (strlen(req_path) > PATH_BUFFER_SIZE) {
      fprintf(stderr, "Request path exceeded maximum of %d characters\n.",
              PATH_BUFFER_SIZE);
      exit(EXIT_FAILURE);
    }

    int returncode = 0;
    char local_path[PATH_BUFFER_SIZE];

    gfcrequest_t *gfr = NULL;
    FILE *file = NULL;

    localPath(req_path, local_path);

    file = openFile(local_path);

    gfr = gfc_create();
    gfc_set_path(&gfr, req_path);

    gfc_set_port(&gfr, config.port);
    gfc_set_server(&gfr, config.server);
    gfc_set_writearg(&gfr, file);
    gfc_set_writefunc(&gfr, writecb);

    fprintf(stdout, "Requesting %s%s\n", config.server, req_path);

    if (0 > (returncode = gfc_perform(&gfr))) {
      fprintf(stdout, "gfc_perform returned an error %d\n", returncode);
      fclose(file);
      if (0 > unlink(local_path))
        fprintf(stderr, "warning: unlink failed on %s\n", local_path);
    } else {
      fclose(file);
    }

    if (gfc_get_status(&gfr) != GF_OK) {
      if (0 > unlink(local_path)) {
        fprintf(stderr, "warning: unlink failed on %s\n", local_path);
      }
    }

    fprintf(stdout, "Status: %s\n", gfc_strstatus(gfc_get_status(&gfr)));
    fprintf(stdout, "Received %zu of %zu bytes\n", gfc_get_bytesreceived(&gfr),
            gfc_get_filelen(&gfr));

    gfc_cleanup(&gfr);

    /*
     * note that when you move the above logic into your worker thread, you will
     * need to coordinate with the boss thread here to effect a clean shutdown.
     */
  }

  return NULL;
}

/* Main ========================================================= */
int main(int argc, char **argv) {
  /* COMMAND LINE OPTIONS ============================================= */
  char *workload_path = "workload.txt";
  char *server = "localhost";
  int option_char = 0;
  unsigned short port = 63866;
  char *req_path = NULL;

  int nthreads = 8;

  int nrequests = 14;

  setbuf(stdout, NULL); // disable caching

  // Parse and set command line arguments
  while ((option_char = getopt_long(argc, argv, "p:n:hs:t:r:w:", gLongOptions,
                                    NULL)) != -1) {
    switch (option_char) {

    case 's': // server
      server = optarg;
      break;
    case 'w': // workload-path
      workload_path = optarg;
      break;
    case 'r': // nrequests
    case 'n': // nrequests
      nrequests = atoi(optarg);
      break;
    case 't': // nthreads
      nthreads = atoi(optarg);
      break;
    case 'p': // port
      port = atoi(optarg);
      break;
    default:
      Usage();
      exit(1);

    case 'h': // help
      Usage();
      exit(0);
    }
  }

  if (EXIT_SUCCESS != workload_init(workload_path)) {
    fprintf(stderr, "Unable to load workload file %s.\n", workload_path);
    exit(EXIT_FAILURE);
  }
  if (port > 65331) {
    fprintf(stderr, "Invalid port number\n");
    exit(EXIT_FAILURE);
  }
  if (nthreads < 1 || nthreads > MAX_THREADS) {
    fprintf(stderr, "Invalid amount of threads\n");
    exit(EXIT_FAILURE);
  }
  gfc_global_init();

  config_t config;
  config.port = port;
  config.server = server;

  // add your threadpool creation here
  steque_init(&queue);
  pthread_mutex_init(&mutexQueue, NULL);
  pthread_mutex_init(&mutexCounter, NULL);
  pthread_cond_init(&condQueue, NULL);
  threads = malloc(sizeof(pthread_t) * nthreads);
  for (int i = 0; i < nthreads; ++i) {
    if (pthread_create(&threads[i], NULL, worker, &config) != 0) {
      fprintf(stderr, "pthread_create failed\n");
      exit(EXIT_FAILURE);
    }
  }

  /* Build your queue of requests here */
  for (int i = 0; i < nrequests; i++) {
    /* Note that when you have a worker thread pool, you will need to move this
     * logic into the worker threads */
    req_path = workload_get_path();
    pthread_mutex_lock(&mutexQueue);
    steque_enqueue(&queue, req_path);
    pthread_cond_signal(&condQueue);
    pthread_mutex_unlock(&mutexQueue);
  }

  for (int i = 0; i < nthreads; ++i) {
    pthread_mutex_lock(&mutexQueue);
    steque_enqueue(&queue, NULL);
    pthread_cond_signal(&condQueue);
    pthread_mutex_unlock(&mutexQueue);
  }

  for (int i = 0; i < nthreads; ++i) {
    if (pthread_join(threads[i], NULL) != 0) {
      fprintf(stderr, "pthread_join failed\n");
    }
  }

  gfc_global_cleanup(); /* use for any global cleanup for AFTER your thread
                         pool has terminated. */

  steque_destroy(&queue);
  pthread_mutex_destroy(&mutexCounter);
  pthread_mutex_destroy(&mutexQueue);
  pthread_cond_destroy(&condQueue);
  free(threads);
  return 0;
}
