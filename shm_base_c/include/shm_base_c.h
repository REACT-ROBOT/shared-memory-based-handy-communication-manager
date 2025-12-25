/**
 * @file shm_base_c.h
 * @brief C language library for shared memory base operations
 *        共有メモリ基本操作のためのC言語ライブラリ
 *
 * @note This is compatible with the C++ shm_base library memory layout.
 *       C++版shm_baseライブラリのメモリレイアウトと互換性があります。
 */

#ifndef __SHM_BASE_C_H__
#define __SHM_BASE_C_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

/* ============================================================================
 * Constants / 定数
 * ============================================================================ */

/**
 * @brief Default permission for shared memory (rw-rw-rw-)
 *        共有メモリのデフォルト権限
 */
#define SHM_PERM_DEFAULT 0666

/**
 * @brief Initialization magic values (compatible with C++ version)
 *        初期化マジック値（C++版と互換）
 */
#define SHM_INITIALIZED         1
#define SHM_NOT_INITIALIZED     0
#define SHM_PTHREAD_INITIALIZED 1
#define SHM_PTHREAD_NOT_INITIALIZED 0

/**
 * @brief Marker value for buffer being written
 *        書き込み中バッファのマーカー値
 */
#define SHM_TIMESTAMP_WRITING   UINT64_MAX

/**
 * @brief Return codes
 *        戻り値
 */
typedef enum {
    SHM_SUCCESS = 0,              /**< Success / 成功 */
    SHM_ERROR_INVALID_ARG = -1,   /**< Invalid argument / 不正な引数 */
    SHM_ERROR_SHM_OPEN = -2,      /**< Failed to open shared memory / 共有メモリのオープン失敗 */
    SHM_ERROR_MMAP = -3,          /**< Failed to mmap / mmapの失敗 */
    SHM_ERROR_FTRUNCATE = -4,     /**< Failed to ftruncate / ftruncateの失敗 */
    SHM_ERROR_NOT_CONNECTED = -5, /**< Not connected / 未接続 */
    SHM_ERROR_DATA_EXPIRED = -6,  /**< Data expired / データ期限切れ */
    SHM_ERROR_NO_DATA = -7,       /**< No data available / データなし */
    SHM_ERROR_TIMEOUT = -8,       /**< Timeout / タイムアウト */
} shm_error_t;


/* ============================================================================
 * Platform Detection / プラットフォーム検出
 * ============================================================================ */

/**
 * @brief Check if running on ARM platform
 *        ARMプラットフォームかどうかを確認
 */
#if defined(__ARM_ARCH) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
#define SHM_IS_ARM_PLATFORM 1
#else
#define SHM_IS_ARM_PLATFORM 0
#endif

/**
 * @brief Get alignment for a type (8 bytes on ARM, natural alignment on x86)
 *        型のアライメントを取得（ARMでは8バイト、x86では自然なアライメント）
 */
#if SHM_IS_ARM_PLATFORM
#define SHM_ALIGNMENT 8
#else
#define SHM_ALIGNMENT 8  /* Use 8 for compatibility */
#endif

/**
 * @brief Align a value to SHM_ALIGNMENT boundary
 *        値をSHM_ALIGNMENT境界にアライン
 */
#define SHM_ALIGN(x) (((x) + SHM_ALIGNMENT - 1) & ~((size_t)(SHM_ALIGNMENT - 1)))


/* ============================================================================
 * Ring Buffer Layout Structure (C++ Compatible)
 * リングバッファレイアウト構造体（C++互換）
 * ============================================================================ */

/**
 * @brief Offsets for ring buffer memory layout
 *        リングバッファのメモリレイアウトオフセット
 *
 * Memory layout (compatible with C++ RingBuffer):
 *   [0]                    : initialization_flag (uint32_t, atomic)
 *   [aligned]              : pthread_init_flag (uint32_t, atomic)
 *   [mutex_offset]         : mutex (pthread_mutex_t)
 *   [cond_offset]          : condition (pthread_cond_t)
 *   [element_size_offset]  : element_size (size_t)
 *   [buf_num_offset]       : buf_num (size_t)
 *   [timestamp_offset]     : timestamp_list (uint64_t * buffer_num, atomic)
 *   [data_offset]          : data_list (element_size * buffer_num)
 */
typedef struct {
    size_t total_size;           /**< Total size of shared memory / 共有メモリの総サイズ */
    size_t mutex_offset;         /**< Offset to pthread_mutex_t / mutexへのオフセット */
    size_t cond_offset;          /**< Offset to pthread_cond_t / conditionへのオフセット */
    size_t element_size_offset;  /**< Offset to element_size / element_sizeへのオフセット */
    size_t buf_num_offset;       /**< Offset to buf_num / buf_numへのオフセット */
    size_t timestamp_offset;     /**< Offset to timestamp_list / timestamp_listへのオフセット */
    size_t data_offset;          /**< Offset to data_list / data_listへのオフセット */
} shm_ring_buffer_layout_t;

/**
 * @brief Ring buffer handle structure
 *        リングバッファハンドル構造体
 */
typedef struct {
    unsigned char* memory_ptr;         /**< Pointer to shared memory / 共有メモリへのポインタ */

    /* Pointers to memory regions (calculated from layout) */
    volatile uint32_t* initialization_flag;  /**< Initialization flag / 初期化フラグ */
    volatile uint32_t* pthread_init_flag;    /**< Pthread init flag / pthread初期化フラグ */
    pthread_mutex_t* mutex;                  /**< Mutex / ミューテックス */
    pthread_cond_t* condition;               /**< Condition variable / 条件変数 */
    size_t* element_size;                    /**< Element size / 要素サイズ */
    size_t* buf_num;                         /**< Buffer count / バッファ数 */
    volatile uint64_t* timestamp_list;       /**< Timestamp list / タイムスタンプリスト */
    unsigned char* data_list;                /**< Data buffer / データバッファ */

    /* Local state */
    uint64_t last_timestamp_us;              /**< Last read timestamp / 最後に読んだタイムスタンプ */
    uint64_t data_expiry_time_us;            /**< Data expiry time / データ有効期限 */
} shm_ring_buffer_t;

/**
 * @brief Shared memory handle structure
 *        共有メモリハンドル構造体
 */
typedef struct {
    char* name;              /**< Shared memory name / 共有メモリ名 */
    int fd;                  /**< File descriptor / ファイルディスクリプタ */
    size_t size;             /**< Mapped size / マップサイズ */
    unsigned char* ptr;      /**< Pointer to mapped memory / マップされたメモリへのポインタ */
    int oflag;               /**< Open flags / オープンフラグ */
    int perm;                /**< Permission / パーミッション */
} shm_shared_memory_t;


/* ============================================================================
 * Utility Functions / ユーティリティ関数
 * ============================================================================ */

/**
 * @brief Get current time in microseconds (monotonic clock)
 *        現在時刻をマイクロ秒単位で取得（モノトニッククロック）
 * @return Current time in microseconds / マイクロ秒単位の現在時刻
 */
uint64_t shm_get_current_time_usec(void);


/* ============================================================================
 * Shared Memory Functions / 共有メモリ関数
 * ============================================================================ */

/**
 * @brief Convert topic name to shared memory path
 *        トピック名を共有メモリパスに変換
 * @param[in] name Topic name / トピック名
 * @return Allocated path string (caller must free) / 確保されたパス文字列（呼び出し元が解放）
 */
char* shm_make_path(const char* name);

/**
 * @brief Initialize shared memory handle
 *        共有メモリハンドルを初期化
 * @param[out] shm Pointer to shared memory handle / 共有メモリハンドルへのポインタ
 * @param[in] name Shared memory name / 共有メモリ名
 * @param[in] oflag Open flags (O_RDWR, O_CREAT, etc.) / オープンフラグ
 * @param[in] perm Permission / パーミッション
 * @return SHM_SUCCESS on success, error code on failure
 */
int shm_shared_memory_init(shm_shared_memory_t* shm, const char* name, int oflag, int perm);

/**
 * @brief Connect to shared memory
 *        共有メモリに接続
 * @param[in,out] shm Pointer to shared memory handle / 共有メモリハンドルへのポインタ
 * @param[in] size Required size (0 to use existing size) / 必要なサイズ（0で既存サイズを使用）
 * @return SHM_SUCCESS on success, error code on failure
 */
int shm_shared_memory_connect(shm_shared_memory_t* shm, size_t size);

/**
 * @brief Disconnect from shared memory
 *        共有メモリから切断
 * @param[in,out] shm Pointer to shared memory handle / 共有メモリハンドルへのポインタ
 */
void shm_shared_memory_disconnect(shm_shared_memory_t* shm);

/**
 * @brief Disconnect and unlink shared memory
 *        共有メモリを切断・削除
 * @param[in,out] shm Pointer to shared memory handle / 共有メモリハンドルへのポインタ
 */
void shm_shared_memory_disconnect_and_unlink(shm_shared_memory_t* shm);

/**
 * @brief Check if shared memory is disconnected
 *        共有メモリが切断されているか確認
 * @param[in] shm Pointer to shared memory handle / 共有メモリハンドルへのポインタ
 * @return true if disconnected, false otherwise
 */
bool shm_shared_memory_is_disconnected(const shm_shared_memory_t* shm);

/**
 * @brief Unlink shared memory by name
 *        名前で共有メモリを削除
 * @param[in] name Shared memory name / 共有メモリ名
 * @return 0 on success, -1 on failure
 */
int shm_unlink_by_name(const char* name);


/* ============================================================================
 * Ring Buffer Layout Functions / リングバッファレイアウト関数
 * ============================================================================ */

/**
 * @brief Calculate ring buffer memory layout (C++ compatible)
 *        リングバッファのメモリレイアウトを計算（C++互換）
 * @param[in] element_size Size of each data element / 各データ要素のサイズ
 * @param[in] buffer_num Number of buffers / バッファ数
 * @param[out] layout Pointer to layout structure / レイアウト構造体へのポインタ
 */
void shm_ring_buffer_calculate_layout(size_t element_size, int buffer_num,
                                       shm_ring_buffer_layout_t* layout);

/**
 * @brief Get required shared memory size for ring buffer
 *        リングバッファに必要な共有メモリサイズを取得
 * @param[in] element_size Size of each data element / 各データ要素のサイズ
 * @param[in] buffer_num Number of buffers / バッファ数
 * @return Required size in bytes / 必要なバイト数
 */
size_t shm_ring_buffer_get_size(size_t element_size, int buffer_num);


/* ============================================================================
 * Ring Buffer Functions / リングバッファ関数
 * ============================================================================ */

/**
 * @brief Initialize ring buffer for publisher (creates new)
 *        パブリッシャ用リングバッファを初期化（新規作成）
 * @param[out] rb Pointer to ring buffer handle / リングバッファハンドルへのポインタ
 * @param[in] memory_ptr Pointer to shared memory / 共有メモリへのポインタ
 * @param[in] element_size Size of each data element / 各データ要素のサイズ
 * @param[in] buffer_num Number of buffers / バッファ数
 * @return SHM_SUCCESS on success, error code on failure
 */
int shm_ring_buffer_init_publisher(shm_ring_buffer_t* rb, unsigned char* memory_ptr,
                                    size_t element_size, int buffer_num);

/**
 * @brief Initialize ring buffer for subscriber (connects to existing)
 *        サブスクライバ用リングバッファを初期化（既存に接続）
 * @param[out] rb Pointer to ring buffer handle / リングバッファハンドルへのポインタ
 * @param[in] memory_ptr Pointer to shared memory / 共有メモリへのポインタ
 * @return SHM_SUCCESS on success, error code on failure
 */
int shm_ring_buffer_init_subscriber(shm_ring_buffer_t* rb, unsigned char* memory_ptr);

/**
 * @brief Check if ring buffer is initialized
 *        リングバッファが初期化されているか確認
 * @param[in] memory_ptr Pointer to shared memory / 共有メモリへのポインタ
 * @return true if initialized, false otherwise
 */
bool shm_ring_buffer_check_initialized(unsigned char* memory_ptr);

/**
 * @brief Wait for ring buffer initialization
 *        リングバッファの初期化を待機
 * @param[in] memory_ptr Pointer to shared memory / 共有メモリへのポインタ
 * @param[in] timeout_usec Timeout in microseconds / タイムアウト（マイクロ秒）
 * @return true if initialized, false on timeout
 */
bool shm_ring_buffer_wait_for_init(unsigned char* memory_ptr, uint64_t timeout_usec);

/**
 * @brief Get oldest buffer number (for writing)
 *        最も古いバッファ番号を取得（書き込み用）
 * @param[in] rb Pointer to ring buffer handle / リングバッファハンドルへのポインタ
 * @return Buffer number (0 to buf_num-1)
 */
int shm_ring_buffer_get_oldest(const shm_ring_buffer_t* rb);

/**
 * @brief Get newest buffer number (for reading)
 *        最新のバッファ番号を取得（読み込み用）
 * @param[in] rb Pointer to ring buffer handle / リングバッファハンドルへのポインタ
 * @return Buffer number (0 to buf_num-1), or -1 if no valid data
 */
int shm_ring_buffer_get_newest(shm_ring_buffer_t* rb);

/**
 * @brief Allocate buffer for writing (mark as being written)
 *        書き込み用にバッファを確保（書き込み中としてマーク）
 * @param[in] rb Pointer to ring buffer handle / リングバッファハンドルへのポインタ
 * @param[in] buffer_num Buffer number to allocate / 確保するバッファ番号
 * @return true if allocated, false if already in use
 */
bool shm_ring_buffer_allocate(shm_ring_buffer_t* rb, int buffer_num);

/**
 * @brief Set timestamp for buffer (marks write as complete)
 *        バッファのタイムスタンプを設定（書き込み完了としてマーク）
 * @param[in] rb Pointer to ring buffer handle / リングバッファハンドルへのポインタ
 * @param[in] buffer_num Buffer number / バッファ番号
 * @param[in] timestamp_us Timestamp in microseconds / マイクロ秒単位のタイムスタンプ
 */
void shm_ring_buffer_set_timestamp(shm_ring_buffer_t* rb, int buffer_num, uint64_t timestamp_us);

/**
 * @brief Get pointer to data buffer
 *        データバッファへのポインタを取得
 * @param[in] rb Pointer to ring buffer handle / リングバッファハンドルへのポインタ
 * @param[in] buffer_num Buffer number / バッファ番号
 * @return Pointer to data buffer / データバッファへのポインタ
 */
unsigned char* shm_ring_buffer_get_data_ptr(const shm_ring_buffer_t* rb, int buffer_num);

/**
 * @brief Signal waiting subscribers (broadcast condition)
 *        待機中のサブスクライバにシグナルを送信
 * @param[in] rb Pointer to ring buffer handle / リングバッファハンドルへのポインタ
 */
void shm_ring_buffer_signal(shm_ring_buffer_t* rb);

/**
 * @brief Set data expiry time
 *        データ有効期限を設定
 * @param[in,out] rb Pointer to ring buffer handle / リングバッファハンドルへのポインタ
 * @param[in] time_us Expiry time in microseconds / 有効期限（マイクロ秒）
 */
void shm_ring_buffer_set_expiry_time(shm_ring_buffer_t* rb, uint64_t time_us);

/**
 * @brief Get element size
 *        要素サイズを取得
 * @param[in] rb Pointer to ring buffer handle / リングバッファハンドルへのポインタ
 * @return Element size in bytes / バイト単位の要素サイズ
 */
size_t shm_ring_buffer_get_element_size(const shm_ring_buffer_t* rb);

/**
 * @brief Get buffer count
 *        バッファ数を取得
 * @param[in] rb Pointer to ring buffer handle / リングバッファハンドルへのポインタ
 * @return Number of buffers / バッファ数
 */
size_t shm_ring_buffer_get_buffer_num(const shm_ring_buffer_t* rb);


#ifdef __cplusplus
}
#endif

#endif /* __SHM_BASE_C_H__ */
