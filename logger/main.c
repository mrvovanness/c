#define _POSIX_C_SOURCE 200809L // for getpid() и pthread, must be before any #include

#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

static void deep_function(void)
{
    LOG_ERROR("something went terribly wrong in the deep function");
}

static void middle_function(void)
{
    LOG_WARNING("entering risky section");
    deep_function();
}

static void top_function(void)
{
    LOG_INFO("top_function started");
    middle_function();
    LOG_INFO("top_function finished");
}

static void simulate_oom(void)
{
    void *p = malloc(1);
    if (p != NULL) {
        LOG_DEBUG("malloc succeeded (normal case), freeing memory");
        free(p);
    }

    LOG_ERROR("malloc returned NULL — out of memory! "
              "(simulated, logger still works)");
}

static void *thread_routine(void *arg)
{
    int id = *(int *)arg;

    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)id;

    for (int i = 0; i < 5; ++i) {
        int secs = (rand_r(&seed) % 3) + 1;
        LOG_INFO("thread %d, iteration %d, sleeping %ds", id, i, secs);
        sleep((unsigned int)secs);
    }

    return NULL;
}

int main(void)
{
    if (logger_init("test.log") != 0) {
        perror("logger_init");
        return EXIT_FAILURE;
    }

    printf("Logger initialised.  Writing to test.log ...\n");

    LOG_DEBUG("application started, pid = %d", (int)getpid());
    LOG_INFO("configuration loaded");
    LOG_WARNING("disk usage above 90%%");
    LOG_ERROR("failed to open /etc/shadow");

    top_function();
    simulate_oom();

    printf("Spawning threads ...\n");
    enum { NUM_THREADS = 4 };
    pthread_t threads[NUM_THREADS];
    int       ids[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; ++i) {
        ids[i] = i;
        if (pthread_create(&threads[i], NULL, thread_routine, &ids[i]) != 0) {
            LOG_ERROR("pthread_create failed for thread %d", i);
        }
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    printf("Setting minimum level to WARNING ...\n");
    logger_set_level(LOG_WARNING);
    LOG_DEBUG("this DEBUG message should NOT appear in the log");
    LOG_INFO("this INFO message should NOT appear in the log");
    LOG_WARNING("this WARNING message SHOULD appear");
    LOG_ERROR("this ERROR message SHOULD appear");

    logger_shutdown();

    printf("Done.  Check test.log for output.\n");
    return EXIT_SUCCESS;
}
