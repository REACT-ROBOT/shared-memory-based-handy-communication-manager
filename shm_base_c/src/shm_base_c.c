/**
 * @file shm_base_c.c
 * @brief C language library implementation for shared memory base operations
 *        共有メモリ基本操作のためのC言語ライブラリ実装
 *
 * @note Memory layout is compatible with C++ shm_base library.
 *       メモリレイアウトはC++版shm_baseライブラリと互換性があります。
 */

#define _POSIX_C_SOURCE 200809L

#include "shm_base_c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>


/* ============================================================================
 * Utility Functions / ユーティリティ関数
 * ============================================================================ */

uint64_t shm_get_current_time_usec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000ULL) + ((uint64_t)ts.tv_nsec / 1000ULL);
}

char* shm_make_path(const char* name)
{
    if (name == NULL || strlen(name) == 0) {
        return NULL;
    }

    size_t name_len = strlen(name);
    size_t prefix_len = strlen("/shm_");
    size_t buf_size = prefix_len + name_len + 1;

    char* result = (char*)malloc(buf_size);
    if (result == NULL) {
        return NULL;
    }

    strcpy(result, "/shm_");

    const char* src = name;
    char* dst = result + prefix_len;

    /* Skip leading '/' if present */
    if (*src == '/') {
        src++;
    }

    while (*src != '\0') {
        if (*src == '/') {
            *dst = '_';
        } else {
            *dst = *src;
        }
        src++;
        dst++;
    }
    *dst = '\0';

    return result;
}


/* ============================================================================
 * Shared Memory Functions / 共有メモリ関数
 * ============================================================================ */

int shm_shared_memory_init(shm_shared_memory_t* shm, const char* name, int oflag, int perm)
{
    if (shm == NULL || name == NULL) {
        return SHM_ERROR_INVALID_ARG;
    }

    memset(shm, 0, sizeof(shm_shared_memory_t));
    shm->fd = -1;
    shm->oflag = oflag;
    shm->perm = perm;

    shm->name = shm_make_path(name);
    if (shm->name == NULL) {
        return SHM_ERROR_INVALID_ARG;
    }

    return SHM_SUCCESS;
}

int shm_shared_memory_connect(shm_shared_memory_t* shm, size_t size)
{
    if (shm == NULL || shm->name == NULL) {
        return SHM_ERROR_INVALID_ARG;
    }

    shm->fd = shm_open(shm->name, shm->oflag, shm->perm);
    if (shm->fd < 0) {
        return SHM_ERROR_SHM_OPEN;
    }

    struct stat st;
    if (fstat(shm->fd, &st) < 0) {
        close(shm->fd);
        shm->fd = -1;
        return SHM_ERROR_SHM_OPEN;
    }

    if (size == 0) {
        shm->size = st.st_size;
    } else {
        shm->size = size;
        if ((size_t)st.st_size < size) {
            if (ftruncate(shm->fd, size) < 0) {
                close(shm->fd);
                shm->fd = -1;
                return SHM_ERROR_FTRUNCATE;
            }
            fstat(shm->fd, &st);
        }
    }

    if (shm->size == 0) {
        close(shm->fd);
        shm->fd = -1;
        return SHM_ERROR_SHM_OPEN;
    }

    shm->ptr = (unsigned char*)mmap(NULL, shm->size,
                                     PROT_READ | PROT_WRITE,
                                     MAP_SHARED, shm->fd, 0);
    if (shm->ptr == MAP_FAILED) {
        close(shm->fd);
        shm->fd = -1;
        shm->ptr = NULL;
        return SHM_ERROR_MMAP;
    }

    return SHM_SUCCESS;
}

void shm_shared_memory_disconnect(shm_shared_memory_t* shm)
{
    if (shm == NULL) {
        return;
    }

    if (shm->ptr != NULL && shm->ptr != MAP_FAILED && shm->size > 0) {
        munmap(shm->ptr, shm->size);
        shm->ptr = NULL;
    }

    if (shm->fd >= 0) {
        close(shm->fd);
        shm->fd = -1;
    }

    shm->size = 0;
}

void shm_shared_memory_disconnect_and_unlink(shm_shared_memory_t* shm)
{
    if (shm == NULL) {
        return;
    }

    char* name_copy = NULL;
    if (shm->name != NULL) {
        name_copy = strdup(shm->name);
    }

    shm_shared_memory_disconnect(shm);

    if (name_copy != NULL) {
        shm_unlink(name_copy);
        free(name_copy);
    }

    if (shm->name != NULL) {
        free(shm->name);
        shm->name = NULL;
    }
}

bool shm_shared_memory_is_disconnected(const shm_shared_memory_t* shm)
{
    if (shm == NULL || shm->fd < 0) {
        return true;
    }

    struct stat st;
    if (fstat(shm->fd, &st) < 0) {
        return true;
    }

    if (st.st_nlink <= 0) {
        return true;
    }

    return false;
}

int shm_unlink_by_name(const char* name)
{
    if (name == NULL) {
        return -1;
    }

    char* path = shm_make_path(name);
    if (path == NULL) {
        return -1;
    }

    int result = shm_unlink(path);
    free(path);

    return result;
}


/* ============================================================================
 * Ring Buffer Layout Functions / リングバッファレイアウト関数
 * ============================================================================ */

/**
 * @brief Get aligned size for atomic uint32_t (matches C++ get_aligned_size)
 */
static size_t get_aligned_size_uint32(void)
{
    /* C++ uses get_aligned_size<std::atomic<uint32_t>>(1) which aligns to 8 on ARM */
    return SHM_ALIGN(sizeof(uint32_t));
}

void shm_ring_buffer_calculate_layout(size_t element_size, int buffer_num,
                                       shm_ring_buffer_layout_t* layout)
{
    if (layout == NULL) {
        return;
    }

    size_t current_offset = 0;

    /* 1. initialization_flag (uint32_t, atomic) - starts at 0 */
    current_offset = 0;

    /* 2. pthread_init_flag (uint32_t, atomic) - aligned */
    current_offset += get_aligned_size_uint32();

    /* 3. mutex (pthread_mutex_t) - aligned to SHM_ALIGNMENT */
    layout->mutex_offset = SHM_ALIGN(current_offset);
    current_offset = layout->mutex_offset + sizeof(pthread_mutex_t);

    /* 4. condition (pthread_cond_t) - aligned to SHM_ALIGNMENT */
    layout->cond_offset = SHM_ALIGN(current_offset);
    current_offset = layout->cond_offset + sizeof(pthread_cond_t);

    /* 5. element_size (size_t) - aligned to SHM_ALIGNMENT */
    layout->element_size_offset = SHM_ALIGN(current_offset);
    current_offset = layout->element_size_offset + sizeof(size_t);

    /* 6. buf_num (size_t) - aligned to SHM_ALIGNMENT */
    layout->buf_num_offset = SHM_ALIGN(current_offset);
    current_offset = layout->buf_num_offset + sizeof(size_t);

    /* 7. timestamp_list (uint64_t * buffer_num, atomic) - aligned to SHM_ALIGNMENT */
    layout->timestamp_offset = SHM_ALIGN(current_offset);
    current_offset = layout->timestamp_offset + sizeof(uint64_t) * buffer_num;

    /* 8. data_list - aligned to at least 8 bytes */
    layout->data_offset = SHM_ALIGN(current_offset);
    current_offset = layout->data_offset + element_size * buffer_num;

    layout->total_size = current_offset;
}

size_t shm_ring_buffer_get_size(size_t element_size, int buffer_num)
{
    shm_ring_buffer_layout_t layout;
    shm_ring_buffer_calculate_layout(element_size, buffer_num, &layout);
    return layout.total_size;
}


/* ============================================================================
 * Ring Buffer Functions / リングバッファ関数
 * ============================================================================ */

/**
 * @brief Setup ring buffer pointers from memory layout
 */
static void setup_ring_buffer_pointers(shm_ring_buffer_t* rb, unsigned char* memory_ptr,
                                        const shm_ring_buffer_layout_t* layout)
{
    rb->memory_ptr = memory_ptr;
    rb->initialization_flag = (volatile uint32_t*)memory_ptr;
    rb->pthread_init_flag = (volatile uint32_t*)(memory_ptr + get_aligned_size_uint32());
    rb->mutex = (pthread_mutex_t*)(memory_ptr + layout->mutex_offset);
    rb->condition = (pthread_cond_t*)(memory_ptr + layout->cond_offset);
    rb->element_size = (size_t*)(memory_ptr + layout->element_size_offset);
    rb->buf_num = (size_t*)(memory_ptr + layout->buf_num_offset);
    rb->timestamp_list = (volatile uint64_t*)(memory_ptr + layout->timestamp_offset);
    rb->data_list = memory_ptr + layout->data_offset;
}

int shm_ring_buffer_init_publisher(shm_ring_buffer_t* rb, unsigned char* memory_ptr,
                                    size_t element_size, int buffer_num)
{
    if (rb == NULL || memory_ptr == NULL || element_size == 0 || buffer_num <= 0) {
        return SHM_ERROR_INVALID_ARG;
    }

    memset(rb, 0, sizeof(shm_ring_buffer_t));
    rb->data_expiry_time_us = 2000000; /* Default 2 seconds */

    /* Calculate layout */
    shm_ring_buffer_layout_t layout;
    shm_ring_buffer_calculate_layout(element_size, buffer_num, &layout);

    /* Setup pointers */
    setup_ring_buffer_pointers(rb, memory_ptr, &layout);

    /* Mark as not initialized first */
    __atomic_store_n(rb->initialization_flag, SHM_NOT_INITIALIZED, __ATOMIC_RELAXED);

    /* Initialize element_size and buf_num */
    *rb->element_size = element_size;
    *rb->buf_num = buffer_num;

    /* Initialize pthread mutex */
    pthread_mutexattr_t m_attr;
    pthread_mutexattr_init(&m_attr);
    pthread_mutexattr_setpshared(&m_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(rb->mutex, &m_attr);
    pthread_mutexattr_destroy(&m_attr);

    /* Initialize pthread condition */
    pthread_condattr_t c_attr;
    pthread_condattr_init(&c_attr);
    pthread_condattr_setpshared(&c_attr, PTHREAD_PROCESS_SHARED);
    pthread_condattr_setclock(&c_attr, CLOCK_MONOTONIC);
    pthread_cond_init(rb->condition, &c_attr);
    pthread_condattr_destroy(&c_attr);

    /* Initialize timestamps to 0 */
    for (int i = 0; i < buffer_num; i++) {
        __atomic_store_n(&rb->timestamp_list[i], 0, __ATOMIC_RELAXED);
    }

    /* Memory barrier and mark as initialized */
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(rb->initialization_flag, SHM_INITIALIZED, __ATOMIC_RELEASE);

    return SHM_SUCCESS;
}

int shm_ring_buffer_init_subscriber(shm_ring_buffer_t* rb, unsigned char* memory_ptr)
{
    if (rb == NULL || memory_ptr == NULL) {
        return SHM_ERROR_INVALID_ARG;
    }

    memset(rb, 0, sizeof(shm_ring_buffer_t));
    rb->data_expiry_time_us = 2000000; /* Default 2 seconds */

    /* First, we need to read element_size and buf_num from shared memory.
     * Calculate layout with dummy values to find their offsets */
    shm_ring_buffer_layout_t temp_layout;
    shm_ring_buffer_calculate_layout(0, 1, &temp_layout);

    /* Read element_size and buf_num */
    size_t element_size = *(size_t*)(memory_ptr + temp_layout.element_size_offset);
    size_t buf_num = *(size_t*)(memory_ptr + temp_layout.buf_num_offset);

    if (element_size == 0 || buf_num == 0) {
        return SHM_ERROR_NO_DATA;
    }

    /* Calculate actual layout */
    shm_ring_buffer_layout_t layout;
    shm_ring_buffer_calculate_layout(element_size, buf_num, &layout);

    /* Setup pointers */
    setup_ring_buffer_pointers(rb, memory_ptr, &layout);

    return SHM_SUCCESS;
}

bool shm_ring_buffer_check_initialized(unsigned char* memory_ptr)
{
    if (memory_ptr == NULL) {
        return false;
    }

    volatile uint32_t* flag = (volatile uint32_t*)memory_ptr;
    return (__atomic_load_n(flag, __ATOMIC_RELAXED) == SHM_INITIALIZED);
}

bool shm_ring_buffer_wait_for_init(unsigned char* memory_ptr, uint64_t timeout_usec)
{
    uint64_t start_time = shm_get_current_time_usec();

    while (!shm_ring_buffer_check_initialized(memory_ptr)) {
        uint64_t current_time = shm_get_current_time_usec();
        if (current_time - start_time >= timeout_usec) {
            return false;
        }
        usleep(50); /* 50 microseconds */
    }

    return true;
}

int shm_ring_buffer_get_oldest(const shm_ring_buffer_t* rb)
{
    if (rb == NULL || rb->buf_num == NULL) {
        return 0;
    }

    size_t buf_num = *rb->buf_num;
    if (buf_num == 0) {
        return 0;
    }

    int oldest_idx = 0;
    uint64_t oldest_ts = __atomic_load_n(&rb->timestamp_list[0], __ATOMIC_RELAXED);

    for (size_t i = 1; i < buf_num; i++) {
        uint64_t ts = __atomic_load_n(&rb->timestamp_list[i], __ATOMIC_RELAXED);
        if (ts < oldest_ts) {
            oldest_ts = ts;
            oldest_idx = i;
        }
    }

    return oldest_idx;
}

int shm_ring_buffer_get_newest(shm_ring_buffer_t* rb)
{
    if (rb == NULL || rb->buf_num == NULL) {
        return -1;
    }

    size_t buf_num = *rb->buf_num;
    if (buf_num == 0) {
        return -1;
    }

    int newest_idx = -1;
    uint64_t newest_ts = 0;

    for (size_t i = 0; i < buf_num; i++) {
        uint64_t ts = __atomic_load_n(&rb->timestamp_list[i], __ATOMIC_RELAXED);
        /* Skip invalid timestamps (0 or UINT64_MAX which indicates writing) */
        if (ts != 0 && ts != SHM_TIMESTAMP_WRITING && ts >= newest_ts) {
            newest_ts = ts;
            newest_idx = i;
        }
    }

    if (newest_idx < 0) {
        return -1;
    }

    /* Check expiry if enabled */
    if (rb->data_expiry_time_us > 0) {
        uint64_t current_time = shm_get_current_time_usec();
        if (current_time - newest_ts > rb->data_expiry_time_us) {
            return -1;
        }
    }

    rb->last_timestamp_us = newest_ts;
    return newest_idx;
}

bool shm_ring_buffer_allocate(shm_ring_buffer_t* rb, int buffer_num)
{
    if (rb == NULL || rb->buf_num == NULL) {
        return false;
    }

    if (buffer_num < 0 || (size_t)buffer_num >= *rb->buf_num) {
        return false;
    }

    uint64_t expected = __atomic_load_n(&rb->timestamp_list[buffer_num], __ATOMIC_ACQUIRE);
    if (expected == SHM_TIMESTAMP_WRITING) {
        return false;
    }

    return __atomic_compare_exchange_n(&rb->timestamp_list[buffer_num],
                                        &expected, SHM_TIMESTAMP_WRITING,
                                        false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

void shm_ring_buffer_set_timestamp(shm_ring_buffer_t* rb, int buffer_num, uint64_t timestamp_us)
{
    if (rb == NULL || rb->buf_num == NULL) {
        return;
    }

    if (buffer_num < 0 || (size_t)buffer_num >= *rb->buf_num) {
        return;
    }

    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&rb->timestamp_list[buffer_num], timestamp_us, __ATOMIC_RELEASE);
}

unsigned char* shm_ring_buffer_get_data_ptr(const shm_ring_buffer_t* rb, int buffer_num)
{
    if (rb == NULL || rb->element_size == NULL) {
        return NULL;
    }

    return rb->data_list + (buffer_num * (*rb->element_size));
}

void shm_ring_buffer_signal(shm_ring_buffer_t* rb)
{
    if (rb == NULL || rb->condition == NULL) {
        return;
    }

    pthread_cond_broadcast(rb->condition);
}

void shm_ring_buffer_set_expiry_time(shm_ring_buffer_t* rb, uint64_t time_us)
{
    if (rb != NULL) {
        rb->data_expiry_time_us = time_us;
    }
}

size_t shm_ring_buffer_get_element_size(const shm_ring_buffer_t* rb)
{
    if (rb == NULL || rb->element_size == NULL) {
        return 0;
    }
    return *rb->element_size;
}

size_t shm_ring_buffer_get_buffer_num(const shm_ring_buffer_t* rb)
{
    if (rb == NULL || rb->buf_num == NULL) {
        return 0;
    }
    return *rb->buf_num;
}
