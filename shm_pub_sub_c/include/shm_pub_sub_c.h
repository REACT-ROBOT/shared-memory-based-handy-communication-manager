/**
 * @file shm_pub_sub_c.h
 * @brief C language library for shared memory based publish/subscribe communication
 *        共有メモリベースの出版/購読通信のためのC言語ライブラリ
 *
 * @note This library uses shm_base_c for memory layout, which is compatible
 *       with the C++ shm_base library. C publishers can communicate with
 *       C++ subscribers and vice versa.
 *       このライブラリはメモリレイアウトにshm_base_cを使用しており、
 *       C++版shm_baseライブラリと互換性があります。
 *       CのパブリッシャとC++のサブスクライバ間で通信可能です。
 */

#ifndef __SHM_PUB_SUB_C_H__
#define __SHM_PUB_SUB_C_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "shm_base_c.h"

/**
 * @brief Default number of ring buffers
 *        リングバッファのデフォルト数
 */
#define SHM_DEFAULT_BUFFER_NUM 3

/**
 * @brief Default data expiry time in microseconds (2 seconds)
 *        データ有効期限のデフォルト値（2秒）
 */
#define SHM_DEFAULT_EXPIRY_TIME_US 2000000


/**
 * @brief Publisher handle structure
 *        パブリッシャハンドル構造体
 */
typedef struct {
    shm_shared_memory_t shm;       /**< Shared memory handle / 共有メモリハンドル */
    shm_ring_buffer_t rb;          /**< Ring buffer handle / リングバッファハンドル */
    size_t data_size;              /**< Size of data type / データ型のサイズ */
    int buffer_num;                /**< Number of buffers / バッファ数 */
} shm_publisher_t;

/**
 * @brief Subscriber handle structure
 *        サブスクライバハンドル構造体
 */
typedef struct {
    shm_shared_memory_t shm;       /**< Shared memory handle / 共有メモリハンドル */
    shm_ring_buffer_t rb;          /**< Ring buffer handle / リングバッファハンドル */
    size_t data_size;              /**< Size of data type / データ型のサイズ */
    bool is_connected;             /**< Connection status / 接続状態 */
} shm_subscriber_t;


/* ============================================================================
 * Publisher Functions / パブリッシャ関数
 * ============================================================================ */

/**
 * @brief Create a new publisher
 *        新しいパブリッシャを作成
 * @param[out] pub Pointer to publisher handle / パブリッシャハンドルへのポインタ
 * @param[in] name Shared memory name (must start with '/' or will be prefixed)
 *                 共有メモリ名（'/'で始まるか、プレフィックスが付加される）
 * @param[in] data_size Size of the data type to publish / パブリッシュするデータ型のサイズ
 * @param[in] buffer_num Number of ring buffers (0 for default=3)
 *                       リングバッファの数（0でデフォルト=3）
 * @return SHM_SUCCESS on success, error code on failure
 */
int shm_publisher_create(shm_publisher_t* pub, const char* name,
                         size_t data_size, int buffer_num);

/**
 * @brief Destroy a publisher (does not unlink shared memory)
 *        パブリッシャを破棄（共有メモリはアンリンクしない）
 * @param[in,out] pub Pointer to publisher handle / パブリッシャハンドルへのポインタ
 */
void shm_publisher_destroy(shm_publisher_t* pub);

/**
 * @brief Publish data to shared memory
 *        データを共有メモリにパブリッシュ
 * @param[in] pub Pointer to publisher handle / パブリッシャハンドルへのポインタ
 * @param[in] data Pointer to data to publish / パブリッシュするデータへのポインタ
 * @return SHM_SUCCESS on success, error code on failure
 */
int shm_publish(shm_publisher_t* pub, const void* data);


/* ============================================================================
 * Subscriber Functions / サブスクライバ関数
 * ============================================================================ */

/**
 * @brief Create a new subscriber
 *        新しいサブスクライバを作成
 * @param[out] sub Pointer to subscriber handle / サブスクライバハンドルへのポインタ
 * @param[in] name Shared memory name / 共有メモリ名
 * @param[in] data_size Size of the data type to subscribe / サブスクライブするデータ型のサイズ
 * @return SHM_SUCCESS on success, error code on failure
 * @note The subscriber will try to connect on first subscribe call
 *       サブスクライバは最初のsubscribe呼び出し時に接続を試みます
 */
int shm_subscriber_create(shm_subscriber_t* sub, const char* name, size_t data_size);

/**
 * @brief Destroy a subscriber
 *        サブスクライバを破棄
 * @param[in,out] sub Pointer to subscriber handle / サブスクライバハンドルへのポインタ
 */
void shm_subscriber_destroy(shm_subscriber_t* sub);

/**
 * @brief Set data expiry time
 *        データ有効期限を設定
 * @param[in,out] sub Pointer to subscriber handle / サブスクライバハンドルへのポインタ
 * @param[in] time_us Expiry time in microseconds (0 to disable expiry check)
 *                    有効期限（マイクロ秒、0で期限チェック無効）
 */
void shm_subscriber_set_expiry_time(shm_subscriber_t* sub, uint64_t time_us);

/**
 * @brief Subscribe (read) the latest data from shared memory
 *        共有メモリから最新データを購読（読み込み）
 * @param[in] sub Pointer to subscriber handle / サブスクライバハンドルへのポインタ
 * @param[out] data Pointer to buffer to store read data / 読み込んだデータを格納するバッファへのポインタ
 * @param[out] is_success Pointer to success flag (can be NULL) / 成功フラグへのポインタ（NULLでも可）
 * @return SHM_SUCCESS on success, error code on failure
 */
int shm_subscribe(shm_subscriber_t* sub, void* data, bool* is_success);

/**
 * @brief Check if shared memory is connected
 *        共有メモリに接続されているか確認
 * @param[in] sub Pointer to subscriber handle / サブスクライバハンドルへのポインタ
 * @return true if connected, false otherwise
 */
bool shm_subscriber_is_connected(const shm_subscriber_t* sub);

/**
 * @brief Try to connect to shared memory
 *        共有メモリへの接続を試みる
 * @param[in,out] sub Pointer to subscriber handle / サブスクライバハンドルへのポインタ
 * @return SHM_SUCCESS on success, error code on failure
 */
int shm_subscriber_connect(shm_subscriber_t* sub);

/**
 * @brief Get the timestamp of the last successfully read data
 *        最後に正常に読み込んだデータのタイムスタンプを取得
 * @param[in] sub Pointer to subscriber handle / サブスクライバハンドルへのポインタ
 * @return Timestamp in microseconds / マイクロ秒単位のタイムスタンプ
 */
uint64_t shm_subscriber_get_timestamp(const shm_subscriber_t* sub);


/* ============================================================================
 * Convenience Macros / 便利なマクロ
 * ============================================================================ */

/**
 * @brief Create a typed publisher
 *        型付きパブリッシャを作成
 * @param pub Publisher handle variable / パブリッシャハンドル変数
 * @param name Shared memory name / 共有メモリ名
 * @param type Data type / データ型
 */
#define SHM_PUBLISHER_CREATE(pub, name, type) \
    shm_publisher_create(&(pub), (name), sizeof(type), SHM_DEFAULT_BUFFER_NUM)

/**
 * @brief Create a typed subscriber
 *        型付きサブスクライバを作成
 * @param sub Subscriber handle variable / サブスクライバハンドル変数
 * @param name Shared memory name / 共有メモリ名
 * @param type Data type / データ型
 */
#define SHM_SUBSCRIBER_CREATE(sub, name, type) \
    shm_subscriber_create(&(sub), (name), sizeof(type))

/**
 * @brief Publish typed data
 *        型付きデータをパブリッシュ
 * @param pub Publisher handle / パブリッシャハンドル
 * @param data_ptr Pointer to data / データへのポインタ
 */
#define SHM_PUBLISH(pub, data_ptr) \
    shm_publish(&(pub), (data_ptr))

/**
 * @brief Subscribe typed data
 *        型付きデータをサブスクライブ
 * @param sub Subscriber handle / サブスクライバハンドル
 * @param data_ptr Pointer to data buffer / データバッファへのポインタ
 * @param success_ptr Pointer to success flag / 成功フラグへのポインタ
 */
#define SHM_SUBSCRIBE(sub, data_ptr, success_ptr) \
    shm_subscribe(&(sub), (data_ptr), (success_ptr))

#ifdef __cplusplus
}
#endif

#endif /* __SHM_PUB_SUB_C_H__ */
