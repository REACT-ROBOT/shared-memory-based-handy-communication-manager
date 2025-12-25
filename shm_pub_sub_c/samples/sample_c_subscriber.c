/**
 * @file sample_c_subscriber.c
 * @brief Sample subscriber using C language shm_pub_sub library
 *        C言語版shm_pub_subライブラリを使用したサブスクライバのサンプル
 *
 * Usage: ./sample_c_subscriber [topic_name]
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include "shm_pub_sub_c.h"

/* Sample data structure to subscribe (must match publisher) */
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

    printf("C Subscriber Sample\n");
    printf("Topic: %s\n", topic_name);
    printf("Data size: %zu bytes\n", sizeof(sample_data_t));
    printf("Press Ctrl+C to exit\n\n");

    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Create subscriber */
    shm_subscriber_t sub;
    int ret = shm_subscriber_create(&sub, topic_name, sizeof(sample_data_t));
    if (ret != SHM_SUCCESS) {
        fprintf(stderr, "Failed to create subscriber: %d\n", ret);
        return 1;
    }

    /* Set data expiry time to 2 seconds */
    shm_subscriber_set_expiry_time(&sub, 2000000);

    printf("Subscriber created, waiting for data...\n\n");

    /* Subscribe loop */
    sample_data_t data;
    int last_counter = 0;

    while (running) {
        bool success = false;

        ret = shm_subscribe(&sub, &data, &success);

        if (ret == SHM_SUCCESS && success) {
            /* Check if this is new data */
            if (data.counter != last_counter) {
                last_counter = data.counter;

                uint64_t now = shm_get_current_time_usec();
                uint64_t ts = shm_subscriber_get_timestamp(&sub);
                double latency_ms = (double)(now - ts) / 1000.0;

                printf("Received: counter=%d, value=%.2f, time=%.6f, msg='%s' (latency: %.2f ms)\n",
                       data.counter, data.value, data.timestamp, data.message, latency_ms);
            }
        } else if (ret == SHM_ERROR_SHM_OPEN) {
            /* Publisher not started yet, wait */
            if (!shm_subscriber_is_connected(&sub)) {
                printf("Waiting for publisher...\n");
            }
        } else if (ret == SHM_ERROR_DATA_EXPIRED) {
            printf("Data expired (no updates for >2 seconds)\n");
        } else if (ret == SHM_ERROR_NO_DATA) {
            /* No data available yet */
        }

        /* Wait 10ms */
        usleep(10000);
    }

    printf("\nShutting down...\n");

    /* Cleanup */
    shm_subscriber_destroy(&sub);

    printf("Subscriber destroyed\n");
    return 0;
}
