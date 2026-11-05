#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

typedef struct {
    ThreadPoolTaskFn fn;
    void *arg;
} Task;

struct ThreadPool {
    pthread_t *threads;
    int thread_count;

    Task *queue;
    int queue_capacity;
    int queue_head;
    int queue_tail;
    int queue_count;

    int active_workers;
    bool stop;

    pthread_mutex_t lock;
    pthread_cond_t cond_work;
    pthread_cond_t cond_idle;
};

static void *worker_entry(void *arg) {
    ThreadPool *tp = (ThreadPool *)arg;
    while (1) {
        pthread_mutex_lock(&tp->lock);

        while (tp->queue_count == 0 && !tp->stop) {
            pthread_cond_wait(&tp->cond_work, &tp->lock);
        }

        if (tp->stop && tp->queue_count == 0) {
            pthread_mutex_unlock(&tp->lock);
            break;
        }

        Task task = tp->queue[tp->queue_head];
        tp->queue_head = (tp->queue_head + 1) % tp->queue_capacity;
        tp->queue_count--;
        tp->active_workers++;

        pthread_mutex_unlock(&tp->lock);

        /* Run task outside lock */
        if (task.fn) {
            task.fn(task.arg);
        }

        pthread_mutex_lock(&tp->lock);
        tp->active_workers--;
        if (tp->queue_count == 0 && tp->active_workers == 0) {
            pthread_cond_broadcast(&tp->cond_idle);
        }
        pthread_mutex_unlock(&tp->lock);
    }
    return NULL;
}

ThreadPool *thread_pool_create(int num_threads) {
    if (num_threads <= 0) {
        long cores = sysconf(_SC_NPROCESSORS_ONLN);
        if (cores < 1) cores = 4;
        num_threads = (int)cores - 1;
        if (num_threads < 2) num_threads = 2;
        if (num_threads > 16) num_threads = 16;
    }

    ThreadPool *tp = calloc(1, sizeof(ThreadPool));
    if (!tp) return NULL;

    tp->thread_count = num_threads;
    tp->queue_capacity = 256;
    tp->queue = malloc(sizeof(Task) * (size_t)tp->queue_capacity);
    if (!tp->queue) {
        free(tp);
        return NULL;
    }

    pthread_mutex_init(&tp->lock, NULL);
    pthread_cond_init(&tp->cond_work, NULL);
    pthread_cond_init(&tp->cond_idle, NULL);

    tp->threads = malloc(sizeof(pthread_t) * (size_t)num_threads);
    if (!tp->threads) {
        free(tp->queue);
        free(tp);
        return NULL;
    }

    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&tp->threads[i], NULL, worker_entry, tp) != 0) {
            tp->thread_count = i;
            break;
        }
    }

    return tp;
}

void thread_pool_submit(ThreadPool *tp, ThreadPoolTaskFn fn, void *arg) {
    if (!tp || !fn) return;

    pthread_mutex_lock(&tp->lock);
    if (tp->stop) {
        pthread_mutex_unlock(&tp->lock);
        return;
    }

    /* Expand queue if full */
    if (tp->queue_count == tp->queue_capacity) {
        int new_cap = tp->queue_capacity * 2;
        Task *new_q = malloc(sizeof(Task) * (size_t)new_cap);
        if (new_q) {
            for (int i = 0; i < tp->queue_count; i++) {
                new_q[i] = tp->queue[(tp->queue_head + i) % tp->queue_capacity];
            }
            free(tp->queue);
            tp->queue = new_q;
            tp->queue_head = 0;
            tp->queue_tail = tp->queue_count;
            tp->queue_capacity = new_cap;
        } else {
            /* Queue full and OOM, drop or skip */
            pthread_mutex_unlock(&tp->lock);
            return;
        }
    }

    tp->queue[tp->queue_tail] = (Task){ fn, arg };
    tp->queue_tail = (tp->queue_tail + 1) % tp->queue_capacity;
    tp->queue_count++;

    pthread_cond_signal(&tp->cond_work);
    pthread_mutex_unlock(&tp->lock);
}

void thread_pool_wait_idle(ThreadPool *tp) {
    if (!tp) return;
    pthread_mutex_lock(&tp->lock);
    while (tp->queue_count > 0 || tp->active_workers > 0) {
        pthread_cond_wait(&tp->cond_idle, &tp->lock);
    }
    pthread_mutex_unlock(&tp->lock);
}

void thread_pool_destroy(ThreadPool *tp) {
    if (!tp) return;

    pthread_mutex_lock(&tp->lock);
    tp->stop = true;
    pthread_cond_broadcast(&tp->cond_work);
    pthread_mutex_unlock(&tp->lock);

    for (int i = 0; i < tp->thread_count; i++) {
        pthread_join(tp->threads[i], NULL);
    }

    pthread_mutex_destroy(&tp->lock);
    pthread_cond_destroy(&tp->cond_work);
    pthread_cond_destroy(&tp->cond_idle);

    free(tp->threads);
    free(tp->queue);
    free(tp);
}

int thread_pool_thread_count(ThreadPool *tp) {
    return tp ? tp->thread_count : 0;
}
