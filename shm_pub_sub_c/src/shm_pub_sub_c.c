/**
 * @file shm_pub_sub_c.c
 * @brief C language library implementation for shared memory based publish/subscribe
 *        共有メモリベースの出版/購読通信のためのC言語ライブラリ実装
 *
 * @note Uses shm_base_c for C++ compatible memory layout.
 *       C++互換のメモリレイアウトのためにshm_base_cを使用。
 */

#define _POSIX_C_SOURCE 200809L

#include "shm_pub_sub_c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>


/* ============================================================================
 * Publisher Functions / パブリッシャ関数
 * ============================================================================ */

int shm_publisher_create(shm_publisher_t* pub, const char* name,
                         size_t data_size, int buffer_num)
{
    if (pub == NULL || name == NULL || data_size == 0) {
        return SHM_ERROR_INVALID_ARG;
    }

    memset(pub, 0, sizeof(shm_publisher_t));

    /* Set buffer count */
    if (buffer_num <= 0) {
        buffer_num = SHM_DEFAULT_BUFFER_NUM;
    }
    pub->buffer_num = buffer_num;
    pub->data_size = data_size;

    /* Initialize shared memory handle */
    int ret = shm_shared_memory_init(&pub->shm, name, O_RDWR | O_CREAT, SHM_PERM_DEFAULT);
    if (ret != SHM_SUCCESS) {
        return ret;
    }

    /* Calculate required size and connect */
    size_t required_size = shm_ring_buffer_get_size(data_size, buffer_num);
    ret = shm_shared_memory_connect(&pub->shm, required_size);
    if (ret != SHM_SUCCESS) {
        if (pub->shm.name) {
            free(pub->shm.name);
            pub->shm.name = NULL;
        }
        return ret;
    }

    /* Initialize ring buffer */
    ret = shm_ring_buffer_init_publisher(&pub->rb, pub->shm.ptr, data_size, buffer_num);
    if (ret != SHM_SUCCESS) {
        shm_shared_memory_disconnect(&pub->shm);
        if (pub->shm.name) {
            free(pub->shm.name);
            pub->shm.name = NULL;
        }
        return ret;
    }

    return SHM_SUCCESS;
}

void shm_publisher_destroy(shm_publisher_t* pub)
{
    if (pub == NULL) {
        return;
    }

    shm_shared_memory_disconnect(&pub->shm);

    if (pub->shm.name != NULL) {
        free(pub->shm.name);
        pub->shm.name = NULL;
    }

    memset(pub, 0, sizeof(shm_publisher_t));
}

int shm_publish(shm_publisher_t* pub, const void* data)
{
    if (pub == NULL || data == NULL) {
        return SHM_ERROR_INVALID_ARG;
    }

    if (pub->shm.ptr == NULL) {
        return SHM_ERROR_NOT_CONNECTED;
    }

    /* Find and allocate the oldest buffer */
    int oldest_idx = shm_ring_buffer_get_oldest(&pub->rb);
    bool allocated = shm_ring_buffer_allocate(&pub->rb, oldest_idx);

    /* If CAS failed, retry */
    if (!allocated) {
        for (int retry = 0; retry < 10; retry++) {
            usleep(1000); /* Wait 1ms */
            oldest_idx = shm_ring_buffer_get_oldest(&pub->rb);
            allocated = shm_ring_buffer_allocate(&pub->rb, oldest_idx);
            if (allocated) {
                break;
            }
        }
    }

    /* Copy data to buffer */
    unsigned char* dst = shm_ring_buffer_get_data_ptr(&pub->rb, oldest_idx);
    if (dst == NULL) {
        return SHM_ERROR_NOT_CONNECTED;
    }
    memcpy(dst, data, pub->data_size);

    /* Update timestamp */
    uint64_t current_time = shm_get_current_time_usec();
    shm_ring_buffer_set_timestamp(&pub->rb, oldest_idx, current_time);

    /* Signal waiting subscribers */
    shm_ring_buffer_signal(&pub->rb);

    return SHM_SUCCESS;
}


/* ============================================================================
 * Subscriber Functions / サブスクライバ関数
 * ============================================================================ */

int shm_subscriber_create(shm_subscriber_t* sub, const char* name, size_t data_size)
{
    if (sub == NULL || name == NULL || data_size == 0) {
        return SHM_ERROR_INVALID_ARG;
    }

    memset(sub, 0, sizeof(shm_subscriber_t));
    sub->data_size = data_size;
    sub->is_connected = false;

    /* Initialize shared memory handle (read-write for ring buffer access) */
    int ret = shm_shared_memory_init(&sub->shm, name, O_RDWR, 0);
    if (ret != SHM_SUCCESS) {
        return ret;
    }

    /* Set default expiry time */
    sub->rb.data_expiry_time_us = SHM_DEFAULT_EXPIRY_TIME_US;

    return SHM_SUCCESS;
}

void shm_subscriber_destroy(shm_subscriber_t* sub)
{
    if (sub == NULL) {
        return;
    }

    shm_shared_memory_disconnect(&sub->shm);

    if (sub->shm.name != NULL) {
        free(sub->shm.name);
        sub->shm.name = NULL;
    }

    memset(sub, 0, sizeof(shm_subscriber_t));
}

void shm_subscriber_set_expiry_time(shm_subscriber_t* sub, uint64_t time_us)
{
    if (sub != NULL) {
        shm_ring_buffer_set_expiry_time(&sub->rb, time_us);
    }
}

int shm_subscriber_connect(shm_subscriber_t* sub)
{
    if (sub == NULL) {
        return SHM_ERROR_INVALID_ARG;
    }

    /* Already connected? Check if still valid */
    if (sub->is_connected && !shm_shared_memory_is_disconnected(&sub->shm)) {
        return SHM_SUCCESS;
    }

    /* Disconnect if previously connected but now invalid */
    if (sub->shm.ptr != NULL) {
        shm_shared_memory_disconnect(&sub->shm);
        sub->is_connected = false;
    }

    /* Try to connect */
    int ret = shm_shared_memory_connect(&sub->shm, 0);
    if (ret != SHM_SUCCESS) {
        return ret;
    }

    /* Check if ring buffer is initialized */
    if (!shm_ring_buffer_check_initialized(sub->shm.ptr)) {
        /* Wait a bit for initialization */
        if (!shm_ring_buffer_wait_for_init(sub->shm.ptr, 500000)) { /* 500ms timeout */
            shm_shared_memory_disconnect(&sub->shm);
            return SHM_ERROR_NO_DATA;
        }
    }

    /* Initialize ring buffer for subscriber */
    uint64_t saved_expiry = sub->rb.data_expiry_time_us;
    ret = shm_ring_buffer_init_subscriber(&sub->rb, sub->shm.ptr);
    if (ret != SHM_SUCCESS) {
        shm_shared_memory_disconnect(&sub->shm);
        return ret;
    }
    sub->rb.data_expiry_time_us = saved_expiry;

    sub->is_connected = true;
    return SHM_SUCCESS;
}

bool shm_subscriber_is_connected(const shm_subscriber_t* sub)
{
    if (sub == NULL) {
        return false;
    }
    return sub->is_connected;
}

int shm_subscribe(shm_subscriber_t* sub, void* data, bool* is_success)
{
    if (sub == NULL || data == NULL) {
        if (is_success) *is_success = false;
        return SHM_ERROR_INVALID_ARG;
    }

    /* Try to connect if not connected */
    if (!sub->is_connected) {
        int ret = shm_subscriber_connect(sub);
        if (ret != SHM_SUCCESS) {
            if (is_success) *is_success = false;
            return ret;
        }
    }

    /* Check if connection is still valid */
    if (shm_shared_memory_is_disconnected(&sub->shm)) {
        sub->is_connected = false;
        int ret = shm_subscriber_connect(sub);
        if (ret != SHM_SUCCESS) {
            if (is_success) *is_success = false;
            return ret;
        }
    }

    /* Find newest buffer */
    int newest_idx = shm_ring_buffer_get_newest(&sub->rb);
    if (newest_idx < 0) {
        if (is_success) *is_success = false;
        /* Check if it's expiry or no data */
        /* Try without expiry check to distinguish */
        uint64_t saved_expiry = sub->rb.data_expiry_time_us;
        sub->rb.data_expiry_time_us = 0;
        int check_idx = shm_ring_buffer_get_newest(&sub->rb);
        sub->rb.data_expiry_time_us = saved_expiry;

        if (check_idx >= 0) {
            return SHM_ERROR_DATA_EXPIRED;
        }
        return SHM_ERROR_NO_DATA;
    }

    /* Copy data from buffer */
    unsigned char* src = shm_ring_buffer_get_data_ptr(&sub->rb, newest_idx);
    if (src == NULL) {
        if (is_success) *is_success = false;
        return SHM_ERROR_NOT_CONNECTED;
    }

    /* Use the minimum of stored element size and subscriber's expected size */
    size_t element_size = shm_ring_buffer_get_element_size(&sub->rb);
    size_t copy_size = sub->data_size;
    if (element_size < copy_size) {
        copy_size = element_size;
    }

    memcpy(data, src, copy_size);

    if (is_success) *is_success = true;
    return SHM_SUCCESS;
}

uint64_t shm_subscriber_get_timestamp(const shm_subscriber_t* sub)
{
    if (sub == NULL) {
        return 0;
    }
    return sub->rb.last_timestamp_us;
}
