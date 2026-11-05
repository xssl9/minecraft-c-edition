#ifndef MCED_THREAD_POOL_H
#define MCED_THREAD_POOL_H

#include <stdbool.h>
#include <stddef.h>

typedef void (*ThreadPoolTaskFn)(void *arg);

typedef struct ThreadPool ThreadPool;

ThreadPool *thread_pool_create(int num_threads);
void thread_pool_submit(ThreadPool *tp, ThreadPoolTaskFn fn, void *arg);
void thread_pool_wait_idle(ThreadPool *tp);
void thread_pool_destroy(ThreadPool *tp);
int thread_pool_thread_count(ThreadPool *tp);

#endif /* MCED_THREAD_POOL_H */
