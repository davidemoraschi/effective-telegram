#include "worker.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct job {
  json_request_t req;
  struct job *next;
} job_t;

struct worker_pool {
  int shutting_down;
  int thread_count;
  worker_handler_t handler;
  void *ctx;

  job_t *head;
  job_t *tail;

#ifdef _WIN32
  CRITICAL_SECTION mu;
  CONDITION_VARIABLE cv;
  HANDLE *threads;
#else
  pthread_mutex_t mu;
  pthread_cond_t cv;
  pthread_t *threads;
#endif
};

static char *dup_text(const char *s) {
  size_t n = strlen(s);
  char *d = (char *)malloc(n + 1);
  if (!d) {
    return NULL;
  }
  memcpy(d, s, n + 1);
  return d;
}

static job_t *job_clone(const json_request_t *req) {
  job_t *job = (job_t *)calloc(1, sizeof(*job));
  if (!job) {
    return NULL;
  }
  job->req = *req;
  if (req->params_json) {
    job->req.params_json = dup_text(req->params_json);
    if (!job->req.params_json) {
      free(job);
      return NULL;
    }
  }
  return job;
}

#ifdef _WIN32
static DWORD WINAPI worker_main(LPVOID arg)
#else
static void *worker_main(void *arg)
#endif
{
  worker_pool_t *pool = (worker_pool_t *)arg;
  for (;;) {
#ifdef _WIN32
    EnterCriticalSection(&pool->mu);
    while (!pool->shutting_down && !pool->head) {
      SleepConditionVariableCS(&pool->cv, &pool->mu, INFINITE);
    }
    if (pool->shutting_down && !pool->head) {
      LeaveCriticalSection(&pool->mu);
      break;
    }
#else
    pthread_mutex_lock(&pool->mu);
    while (!pool->shutting_down && !pool->head) {
      pthread_cond_wait(&pool->cv, &pool->mu);
    }
    if (pool->shutting_down && !pool->head) {
      pthread_mutex_unlock(&pool->mu);
      break;
    }
#endif

    job_t *job = pool->head;
    if (job) {
      pool->head = job->next;
      if (!pool->head) {
        pool->tail = NULL;
      }
    }
#ifdef _WIN32
    LeaveCriticalSection(&pool->mu);
#else
    pthread_mutex_unlock(&pool->mu);
#endif

    if (job) {
      pool->handler(&job->req, pool->ctx);
      json_free_request(&job->req);
      free(job);
    }
  }

#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

worker_pool_t *worker_pool_create(int thread_count, worker_handler_t handler, void *ctx) {
  worker_pool_t *pool = (worker_pool_t *)calloc(1, sizeof(*pool));
  if (!pool) {
    return NULL;
  }
  pool->thread_count = thread_count;
  pool->handler = handler;
  pool->ctx = ctx;

#ifdef _WIN32
  InitializeCriticalSection(&pool->mu);
  InitializeConditionVariable(&pool->cv);
#else
  pthread_mutex_init(&pool->mu, NULL);
  pthread_cond_init(&pool->cv, NULL);
#endif

  pool->threads = calloc((size_t)thread_count, sizeof(*pool->threads));
  if (!pool->threads) {
    free(pool);
    return NULL;
  }

  for (int i = 0; i < thread_count; i++) {
#ifdef _WIN32
    pool->threads[i] = CreateThread(NULL, 0, worker_main, pool, 0, NULL);
#else
    pthread_create(&pool->threads[i], NULL, worker_main, pool);
#endif
  }
  return pool;
}

void worker_pool_submit(worker_pool_t *pool, const json_request_t *req) {
  job_t *job = job_clone(req);
  if (!job) {
    return;
  }

#ifdef _WIN32
  EnterCriticalSection(&pool->mu);
#else
  pthread_mutex_lock(&pool->mu);
#endif
  if (pool->tail) {
    pool->tail->next = job;
  } else {
    pool->head = job;
  }
  pool->tail = job;
#ifdef _WIN32
  WakeConditionVariable(&pool->cv);
  LeaveCriticalSection(&pool->mu);
#else
  pthread_cond_signal(&pool->cv);
  pthread_mutex_unlock(&pool->mu);
#endif
}

void worker_pool_shutdown(worker_pool_t *pool) {
  if (!pool) {
    return;
  }

#ifdef _WIN32
  EnterCriticalSection(&pool->mu);
#else
  pthread_mutex_lock(&pool->mu);
#endif
  pool->shutting_down = 1;
#ifdef _WIN32
  WakeAllConditionVariable(&pool->cv);
  LeaveCriticalSection(&pool->mu);
#else
  pthread_cond_broadcast(&pool->cv);
  pthread_mutex_unlock(&pool->mu);
#endif

  for (int i = 0; i < pool->thread_count; i++) {
#ifdef _WIN32
    WaitForSingleObject(pool->threads[i], INFINITE);
    CloseHandle(pool->threads[i]);
#else
    pthread_join(pool->threads[i], NULL);
#endif
  }

  job_t *job = pool->head;
  while (job) {
    job_t *next = job->next;
    json_free_request(&job->req);
    free(job);
    job = next;
  }

#ifdef _WIN32
  DeleteCriticalSection(&pool->mu);
#else
  pthread_mutex_destroy(&pool->mu);
  pthread_cond_destroy(&pool->cv);
#endif
  free(pool->threads);
  free(pool);
}
