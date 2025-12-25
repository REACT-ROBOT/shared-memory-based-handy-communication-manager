/**
 * @file sample_c_publisher.c
 * @brief Sample publisher using C language shm_pub_sub library
 *        C言語版shm_pub_subライブラリを使用したパブリッシャのサンプル
 *
 * Usage: ./sample_c_publisher [topic_name]
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include "shm_pub_sub_c.h"

/* Sample data structure to publish */
typedef struct {
    int32_t counter;
    float value;
    double timestamp;
    char message[64];
} sample_data_t;

static volatile int running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

int main(int argc, char* argv[])
{
    const char* topic_name = "/sample_topic";

    if (argc > 1) {
        topic_name = argv[1];
    }

    printf("C Publisher Sample\n");
    printf("Topic: %s\n", topic_name);
    printf("Data size: %zu bytes\n", sizeof(sample_data_t));
    printf("Press Ctrl+C to exit\n\n");

    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Create publisher */
    shm_publisher_t pub;
    int ret = shm_publisher_create(&pub, topic_name, sizeof(sample_data_t), 3);
    if (ret != SHM_SUCCESS) {
        fprintf(stderr, "Failed to create publisher: %d\n", ret);
        return 1;
    }

    printf("Publisher created successfully\n\n");

    /* Publish loop */
    sample_data_t data;
    data.counter = 0;

    while (running) {
        /* Prepare data */
        data.counter++;
        data.value = (float)data.counter * 0.1f;
        data.timestamp = (double)shm_get_current_time_usec() / 1000000.0;
        snprintf(data.message, sizeof(data.message), "Hello from C publisher #%d", data.counter);

        /* Publish */
        ret = shm_publish(&pub, &data);
        if (ret == SHM_SUCCESS) {
            printf("Published: counter=%d, value=%.2f, time=%.6f, msg='%s'\n",
                   data.counter, data.value, data.timestamp, data.message);
        } else {
            fprintf(stderr, "Failed to publish: %d\n", ret);
        }

        /* Wait 100ms */
        usleep(100000);
    }

    printf("\nShutting down...\n");

    /* Cleanup */
    shm_publisher_destroy(&pub);

    printf("Publisher destroyed\n");
    return 0;
}
