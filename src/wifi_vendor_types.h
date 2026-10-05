/*
 * src/wifi_vendor_types.h
 *
 * Iron V Native Wi-Fi Blob Interface, Protocol Types & OSAL Binding Definitions
 *
 * Design Choice Attribution:
 * Binary interface structures, Wi-Fi driver parameters, OS abstraction layer (OSAL)
 * dispatch table layouts, and hardware 802.11 descriptor layouts adapted from Espressif Systems'
 * ESP-IDF Wi-Fi and PHY stack architecture (components/esp_wifi and components/esp_phy).
 *
 * Conforms to Iron V Project Standards:
 * - Zero magic numbers: explicit uppercase #define constants for all parameters and bitmasks
 * - Parameterized types: clean, typed definitions matching closed-source binary blob expectations
 * - Freestanding bare-metal OSAL: static structures with zero heap allocation
 * - Clean compilation: -Wall -Wextra -Werror compliant
 */

#ifndef IRON_V_WIFI_VENDOR_TYPES_H
#define IRON_V_WIFI_VENDOR_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include "wifi_regulatory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* System Error & Return Codes                                               */
/* ========================================================================= */
typedef int32_t esp_err_t;

#define ESP_OK                          0
#define ESP_FAIL                        (-1)
#define ESP_ERR_NO_MEM                  0x101
#define ESP_ERR_INVALID_ARG             0x102
#define ESP_ERR_INVALID_STATE           0x103
#define ESP_ERR_INVALID_SIZE            0x104
#define ESP_ERR_NOT_FOUND               0x105
#define ESP_ERR_NOT_SUPPORTED           0x106
#define ESP_ERR_TIMEOUT                 0x107
#define ESP_ERR_INVALID_RESPONSE        0x108
#define ESP_ERR_INVALID_CRC             0x109
#define ESP_ERR_INVALID_VERSION         0x10A
#define ESP_ERR_INVALID_MAC             0x10B

#define ESP_ERR_WIFI_BASE               0x3000
#define ESP_ERR_WIFI_NOT_INIT           (ESP_ERR_WIFI_BASE + 1)
#define ESP_ERR_WIFI_NOT_STARTED        (ESP_ERR_WIFI_BASE + 2)
#define ESP_ERR_WIFI_NOT_STOPPED        (ESP_ERR_WIFI_BASE + 3)
#define ESP_ERR_WIFI_IF                 (ESP_ERR_WIFI_BASE + 4)
#define ESP_ERR_WIFI_MODE               (ESP_ERR_WIFI_BASE + 5)
#define ESP_ERR_WIFI_STATE              (ESP_ERR_WIFI_BASE + 6)
#define ESP_ERR_WIFI_CONN               (ESP_ERR_WIFI_BASE + 7)
#define ESP_ERR_WIFI_NVS                (ESP_ERR_WIFI_BASE + 8)
#define ESP_ERR_WIFI_MAC                (ESP_ERR_WIFI_BASE + 9)
#define ESP_ERR_WIFI_SSID               (ESP_ERR_WIFI_BASE + 10)
#define ESP_ERR_WIFI_PASSWORD           (ESP_ERR_WIFI_BASE + 11)
#define ESP_ERR_WIFI_TIMEOUT            (ESP_ERR_WIFI_BASE + 12)
#define ESP_ERR_WIFI_WAKE_FAIL          (ESP_ERR_WIFI_BASE + 13)
#define ESP_ERR_WIFI_WOULD_BLOCK        (ESP_ERR_WIFI_BASE + 14)
#define ESP_ERR_WIFI_NOT_CONNECT        (ESP_ERR_WIFI_BASE + 15)
#define ESP_ERR_WIFI_POST               (ESP_ERR_WIFI_BASE + 16)
#define ESP_ERR_WIFI_INIT_STATE         (ESP_ERR_WIFI_BASE + 19)
#define ESP_ERR_WIFI_STOP_STATE         (ESP_ERR_WIFI_BASE + 20)

/* ========================================================================= */
/* Freestanding RTOS Shim Types                                              */
/* ========================================================================= */
typedef uint32_t  TickType_t;
typedef uint32_t  UBaseType_t;
typedef int32_t   BaseType_t;
typedef void*     QueueHandle_t;

/* ========================================================================= */
/* OS Abstraction Layer (OSAL) Constants & Enums                             */
/* ========================================================================= */
#define ESP_WIFI_OS_ADAPTER_VERSION     0x00000009
#define ESP_WIFI_OS_ADAPTER_MAGIC       0xDEADBEAF
#define OSI_FUNCS_TIME_BLOCKING         0xFFFFFFFFU

#define OSI_QUEUE_SEND_FRONT            0
#define OSI_QUEUE_SEND_BACK             1
#define OSI_QUEUE_SEND_OVERWRITE        2

/* ========================================================================= */
/* Cryptographic Provider Callback Structure (wpa_crypto_funcs_t)            */
/* ========================================================================= */
#define ESP_WIFI_CRYPTO_VERSION         0x00000001

/* struct os_time from ESP-IDF 66ab063a9a7f wpa_supplicant/port/include/os.h
 * (os_time_t is uint64_t, suseconds_t is long), filled by the _get_time callback */
typedef struct {
    uint64_t sec;
    long     usec;
} wifi_os_time_t;

/* Callback types and order from ESP-IDF 66ab063a9a7f components/esp_wifi/include/esp_wifi_crypto_types.h */
typedef int (*esp_hmac_sha256_vector_t)(const unsigned char *key, int key_len, int num_elem,
                                        const unsigned char *addr[], const int *len, unsigned char *mac);
typedef int (*esp_pbkdf2_sha1_t)(const char *passphrase, const char *ssid, unsigned int ssid_len,
                                 int iterations, unsigned char *buf, unsigned int buflen);
typedef int (*esp_aes_128_encrypt_t)(const unsigned char *key, const unsigned char *iv, unsigned char *data, int data_len);
typedef int (*esp_aes_128_decrypt_t)(const unsigned char *key, const unsigned char *iv, unsigned char *data, int data_len);
typedef int (*esp_omac1_aes_128_t)(const uint8_t *key, const uint8_t *data, size_t data_len, uint8_t *mic);
typedef uint8_t *(*esp_ccmp_decrypt_t)(const uint8_t *tk, const uint8_t *ieee80211_hdr, const uint8_t *data,
                                       size_t data_len, size_t *decrypted_len, bool espnow_pkt);
typedef uint8_t *(*esp_ccmp_encrypt_t)(const uint8_t *tk, uint8_t *frame, size_t len, size_t hdrlen,
                                       uint8_t *pn, int keyid, size_t *encrypted_len);
typedef int (*esp_aes_gmac_t)(const uint8_t *key, size_t keylen, const uint8_t *iv, size_t iv_len,
                              const uint8_t *aad, size_t aad_len, uint8_t *mic);
typedef int (*esp_sha256_vector_t)(size_t num_elem, const uint8_t *addr[], const size_t *len, uint8_t *buf);
typedef int (*esp_aes_wrap_t)(const unsigned char *kek, size_t kek_len, int n, const unsigned char *plain, unsigned char *cipher);
typedef int (*esp_aes_unwrap_t)(const unsigned char *kek, size_t kek_len, int n, const unsigned char *cipher, unsigned char *plain);

/* All callbacks are left NULL (g_wifi_default_wpa_crypto_funcs). The supplicant is
 * src/wpa2_client.c and PMF, WPA3/SAE, FT and ESP-NOW are not used, so WPA2-PSK is not
 * expected to reach them; not yet verified on a real join (REV-10/11). A call would
 * fault on address 0 and show in the trap dump. */
typedef struct wpa_crypto_funcs_t {
    uint32_t size;
    uint32_t version;
    esp_hmac_sha256_vector_t hmac_sha256_vector;
    esp_pbkdf2_sha1_t pbkdf2_sha1;
    esp_aes_128_encrypt_t aes_128_encrypt;
    esp_aes_128_decrypt_t aes_128_decrypt;
    esp_omac1_aes_128_t omac1_aes_128;
    esp_ccmp_decrypt_t ccmp_decrypt;
    esp_ccmp_encrypt_t ccmp_encrypt;
    esp_aes_gmac_t aes_gmac;
    esp_sha256_vector_t sha256_vector;
    esp_aes_wrap_t aes_wrap;
    esp_aes_unwrap_t aes_unwrap;
} wpa_crypto_funcs_t;

extern const wpa_crypto_funcs_t g_wifi_default_wpa_crypto_funcs;

/* ========================================================================= */
/* Full OSAL Function Pointer Dispatch Table (wifi_osi_funcs_t)              */
/* ========================================================================= */
typedef struct wifi_osi_funcs_t {
    int32_t  _version;
    bool     (*_env_is_chip)(void);
    void     (*_set_intr)(int32_t cpu_no, uint32_t intr_source, uint32_t intr_num, int32_t intr_prio);
    void     (*_clear_intr)(uint32_t intr_source, uint32_t intr_num);
    void     (*_set_isr)(int32_t n, void *f, void *arg);
    void     (*_ints_on)(uint32_t mask);
    void     (*_ints_off)(uint32_t mask);
    bool     (*_is_from_isr)(void);
    void    *(*_spin_lock_create)(void);
    void     (*_spin_lock_delete)(void *lock);
    uint32_t (*_wifi_int_disable)(void *wifi_int_mux);
    void     (*_wifi_int_restore)(void *wifi_int_mux, uint32_t tmp);
    void     (*_task_yield_from_isr)(void);
    void    *(*_semphr_create)(uint32_t max, uint32_t init);
    void     (*_semphr_delete)(void *semphr);
    int32_t  (*_semphr_take)(void *semphr, uint32_t block_time_tick);
    int32_t  (*_semphr_give)(void *semphr);
    void    *(*_wifi_thread_semphr_get)(void);
    void    *(*_mutex_create)(void);
    void    *(*_recursive_mutex_create)(void);
    void     (*_mutex_delete)(void *mutex);
    int32_t  (*_mutex_lock)(void *mutex);
    int32_t  (*_mutex_unlock)(void *mutex);
    void    *(*_queue_create)(uint32_t queue_len, uint32_t item_size);
    void     (*_queue_delete)(void *queue);
    int32_t  (*_queue_send)(void *queue, void *item, uint32_t block_time_tick);
    int32_t  (*_queue_send_from_isr)(void *queue, void *item, void *hptw);
    int32_t  (*_queue_send_to_back)(void *queue, void *item, uint32_t block_time_tick);
    int32_t  (*_queue_send_to_front)(void *queue, void *item, uint32_t block_time_tick);
    int32_t  (*_queue_recv)(void *queue, void *item, uint32_t block_time_tick);
    uint32_t (*_queue_msg_waiting)(void *queue);
    void    *(*_event_group_create)(void);
    void     (*_event_group_delete)(void *event);
    uint32_t (*_event_group_set_bits)(void *event, uint32_t bits);
    uint32_t (*_event_group_clear_bits)(void *event, uint32_t bits);
    uint32_t (*_event_group_wait_bits)(void *event, uint32_t bits_to_wait_for, int clear_on_exit, int wait_for_all_bits, uint32_t block_time_tick);
    int32_t  (*_task_create_pinned_to_core)(void *task_func, const char *name, uint32_t stack_depth, void *param, uint32_t prio, void *task_handle, uint32_t core_id);
    int32_t  (*_task_create)(void *task_func, const char *name, uint32_t stack_depth, void *param, uint32_t prio, void *task_handle);
    void     (*_task_delete)(void *task_handle);
    void     (*_task_delay)(uint32_t tick);
    int32_t  (*_task_ms_to_tick)(uint32_t ms);
    void    *(*_task_get_current_task)(void);
    int32_t  (*_task_get_max_priority)(void);
    void    *(*_malloc)(size_t size);
    void     (*_free)(void *p);
    int32_t  (*_event_post)(const char *event_base, int32_t event_id, void *event_data, size_t event_data_size, uint32_t ticks_to_wait);
    uint32_t (*_get_free_heap_size)(void);
    uint32_t (*_rand)(void);
    void     (*_dport_access_stall_other_cpu_start_wrap)(void);
    void     (*_dport_access_stall_other_cpu_end_wrap)(void);
    void     (*_wifi_pm_sleep_lock_acquire)(void);
    void     (*_wifi_pm_sleep_lock_release)(void);
    void     (*_phy_disable)(void);
    void     (*_phy_enable)(void);
    int      (*_phy_update_country_info)(const char *country);
    int      (*_read_mac)(uint8_t *mac, unsigned int type);
    void     (*_timer_arm)(void *timer, uint32_t tmout, bool repeat);
    void     (*_timer_disarm)(void *timer);
    void     (*_timer_done)(void *ptimer);
    void     (*_timer_setfn)(void *ptimer, void *pfunction, void *parg);
    void     (*_timer_arm_us)(void *ptimer, uint32_t us, bool repeat);
    void     (*_wifi_reset_mac)(void);
    void     (*_wifi_clock_enable)(void);
    void     (*_wifi_clock_disable)(void);
    void     (*_wifi_rtc_enable_iso)(void);
    void     (*_wifi_rtc_disable_iso)(void);
    int64_t  (*_esp_timer_get_time)(void);
    int      (*_nvs_set_i8)(uint32_t handle, const char *key, int8_t value);
    int      (*_nvs_get_i8)(uint32_t handle, const char *key, int8_t *out_value);
    int      (*_nvs_set_u8)(uint32_t handle, const char *key, uint8_t value);
    int      (*_nvs_get_u8)(uint32_t handle, const char *key, uint8_t *out_value);
    int      (*_nvs_set_u16)(uint32_t handle, const char *key, uint16_t value);
    int      (*_nvs_get_u16)(uint32_t handle, const char *key, uint16_t *out_value);
    int      (*_nvs_open)(const char *name, unsigned int open_mode, uint32_t *out_handle);
    void     (*_nvs_close)(uint32_t handle);
    int      (*_nvs_commit)(uint32_t handle);
    int      (*_nvs_set_blob)(uint32_t handle, const char *key, const void *value, size_t length);
    int      (*_nvs_get_blob)(uint32_t handle, const char *key, void *out_value, size_t *length);
    int      (*_nvs_erase_key)(uint32_t handle, const char *key);
    int      (*_get_random)(uint8_t *buf, size_t len);
    int      (*_get_time)(void *t);
    unsigned long (*_random)(void);
    uint32_t (*_slowclk_cal_get)(void);
    void     (*_log_write)(unsigned int level, const char *tag, const char *format, ...);
    void     (*_log_writev)(unsigned int level, const char *tag, const char *format, va_list args);
    uint32_t (*_log_timestamp)(void);
    void    *(*_malloc_internal)(size_t size);
    void    *(*_realloc_internal)(void *ptr, size_t size);
    void    *(*_calloc_internal)(size_t n, size_t size);
    void    *(*_zalloc_internal)(size_t size);
    void    *(*_wifi_malloc)(size_t size);
    void    *(*_wifi_realloc)(void *ptr, size_t size);
    void    *(*_wifi_calloc)(size_t n, size_t size);
    void    *(*_wifi_zalloc)(size_t size);
    void    *(*_wifi_create_queue)(int queue_len, int item_size);
    void     (*_wifi_delete_queue)(void *queue);
    int      (*_coex_init)(void);
    void     (*_coex_deinit)(void);
    int      (*_coex_enable)(void);
    void     (*_coex_disable)(void);
    uint32_t (*_coex_status_get)(void);
    void     (*_coex_condition_set)(uint32_t type, bool dissatisfy);
    int      (*_coex_wifi_request)(uint32_t event, uint32_t latency, uint32_t duration);
    int      (*_coex_wifi_release)(uint32_t event);
    int      (*_coex_wifi_channel_set)(uint8_t primary, uint8_t secondary);
    int      (*_coex_event_duration_get)(uint32_t event, uint32_t *duration);
    int      (*_coex_pti_get)(uint32_t event, uint8_t *pti);
    void     (*_coex_schm_status_bit_clear)(uint32_t type, uint32_t status);
    void     (*_coex_schm_status_bit_set)(uint32_t type, uint32_t status);
    int      (*_coex_schm_interval_set)(uint32_t interval);
    uint32_t (*_coex_schm_interval_get)(void);
    uint8_t  (*_coex_schm_curr_period_get)(void);
    void    *(*_coex_schm_curr_phase_get)(void);
    int      (*_coex_schm_process_restart)(void);
    int      (*_coex_schm_register_cb)(int, int (*cb)(int));
    int      (*_coex_register_start_cb)(int (*cb)(void));
    void     (*_regdma_link_set_write_wait_content)(void *, uint32_t, uint32_t);
    void    *(*_sleep_retention_find_link_by_id)(int);
    int      (*_coex_schm_flexible_period_set)(uint8_t);
    uint8_t  (*_coex_schm_flexible_period_get)(void);
    void    *(*_coex_schm_get_phase_by_idx)(int);
    bool     (*_wifi_disable_ac_ax)(void);
    int32_t  (*_wifi_bb_sleep_retention_attach)(void);
    int32_t  (*_wifi_bb_sleep_retention_detach)(void);
    int32_t  (*_wifi_mac_sleep_retention_attach)(void);
    int32_t  (*_wifi_mac_sleep_retention_detach)(void);
    int32_t  _magic;
} wifi_osi_funcs_t;

extern wifi_osi_funcs_t g_wifi_osi_funcs;

/* ========================================================================= */
/* Wi-Fi Stack Initialization Configuration Structure                        */
/* ========================================================================= */
#define WIFI_INIT_CONFIG_MAGIC          0x1F2F3F4F
#define WIFI_TASK_CORE_ID               0
#define WIFI_SOFTAP_BEACON_MAX_LEN      752
#define WIFI_MGMT_SBUF_NUM              32
#define WIFI_TASK_STACK_SIZE            3584
#define WIFI_STATIC_RX_BUFFER_NUM       10
#define WIFI_DYNAMIC_RX_BUFFER_NUM      32
#define WIFI_TX_BUFFER_TYPE             1
#define WIFI_STATIC_TX_BUFFER_NUM       0
#define WIFI_DYNAMIC_TX_BUFFER_NUM      32
#define WIFI_DYNAMIC_RX_MGMT_BUF        1
#define WIFI_RX_MGMT_BUF_NUM_DEF        5
#define WIFI_CACHE_TX_BUFFER_NUM        4
#define WIFI_AMPDU_TX_ENABLED           0
#define WIFI_AMPDU_RX_ENABLED           0
#define WIFI_DEFAULT_RX_BA_WIN          4
#define WIFI_ESPNOW_MAX_ENCRYPT_NUM     0
#define WIFI_TX_HETB_QUEUE_NUM          1
#define WIFI_CSI_DISABLED               0
#define WIFI_AMSDU_TX_DISABLED          0
#define WIFI_NVS_DISABLED               0
#define WIFI_NANO_DISABLED              0
#define WIFI_RMAC_AUTO_RESET_INT_DEF    0

#define CONFIG_FEATURE_FTM_INITIATOR_BIT (1ULL << 2)
#define CONFIG_FEATURE_FTM_RESPONDER_BIT (1ULL << 3)
#define CONFIG_FEATURE_CACHE_TX_BUF_BIT  (1ULL << 1)
#define WIFI_FEATURE_CAPS (CONFIG_FEATURE_FTM_INITIATOR_BIT | CONFIG_FEATURE_CACHE_TX_BUF_BIT)

typedef struct {
    wifi_osi_funcs_t*      osi_funcs;
    wpa_crypto_funcs_t     wpa_crypto_funcs;
    int                    static_rx_buf_num;
    int                    dynamic_rx_buf_num;
    int                    tx_buf_type;
    int                    static_tx_buf_num;
    int                    dynamic_tx_buf_num;
    int                    rx_mgmt_buf_type;
    int                    rx_mgmt_buf_num;
    int                    cache_tx_buf_num;
    int                    csi_enable;
    int                    ampdu_rx_enable;
    int                    ampdu_tx_enable;
    int                    amsdu_tx_enable;
    int                    nvs_enable;
    int                    nano_enable;
    int                    rx_ba_win;
    int                    wifi_task_core_id;
    int                    beacon_max_len;
    int                    mgmt_sbuf_num;
    uint64_t               feature_caps;
    bool                   sta_disconnected_pm;
    int                    espnow_max_encrypt_num;
    int                    tx_hetb_queue_num;
    bool                   dump_hesigb_enable;
    bool                   privacy_enhancements;
    uint8_t                rmac_auto_reset_int;
    int                    wifi_task_stack_size;
    int                    magic;
} wifi_init_config_t;

/* ABI layout checks against ESP-IDF 66ab063a9a7f (pins the blobs in libs/esp32c6/VERSION).
 * Expected values are the RV32 ILP32 layout of the upstream definitions. */
#if defined(__riscv)
#include <stddef.h>
_Static_assert(sizeof(wifi_os_time_t) == 16U && offsetof(wifi_os_time_t, usec) == 8U, "struct os_time layout");
_Static_assert(sizeof(wpa_crypto_funcs_t) == 52U, "wpa_crypto_funcs_t: 2 words + 11 callbacks");
_Static_assert(sizeof(wifi_osi_funcs_t) == 508U, "wifi_osi_funcs_t: _version + 125 callbacks + _magic");
_Static_assert(offsetof(wifi_osi_funcs_t, _magic) == 504U, "wifi_osi_funcs_t: _magic is the last word");
_Static_assert(offsetof(wifi_init_config_t, wpa_crypto_funcs) == 4U, "wifi_init_config_t layout");
_Static_assert(offsetof(wifi_init_config_t, static_rx_buf_num) == 56U, "wifi_init_config_t layout");
_Static_assert(offsetof(wifi_init_config_t, feature_caps) == 128U, "wifi_init_config_t layout");
_Static_assert(offsetof(wifi_init_config_t, wifi_task_stack_size) == 152U, "wifi_init_config_t layout");
_Static_assert(offsetof(wifi_init_config_t, magic) == 156U, "wifi_init_config_t layout");
_Static_assert(sizeof(wifi_init_config_t) == 160U, "wifi_init_config_t size");
#endif

#define WIFI_INIT_CONFIG_DEFAULT() { \
    .osi_funcs = &g_wifi_osi_funcs, \
    .wpa_crypto_funcs = g_wifi_default_wpa_crypto_funcs, \
    .static_rx_buf_num = WIFI_STATIC_RX_BUFFER_NUM, \
    .dynamic_rx_buf_num = WIFI_DYNAMIC_RX_BUFFER_NUM, \
    .tx_buf_type = WIFI_TX_BUFFER_TYPE, \
    .static_tx_buf_num = WIFI_STATIC_TX_BUFFER_NUM, \
    .dynamic_tx_buf_num = WIFI_DYNAMIC_TX_BUFFER_NUM, \
    .rx_mgmt_buf_type = WIFI_DYNAMIC_RX_MGMT_BUF, \
    .rx_mgmt_buf_num = WIFI_RX_MGMT_BUF_NUM_DEF, \
    .cache_tx_buf_num = WIFI_CACHE_TX_BUFFER_NUM, \
    .csi_enable = WIFI_CSI_DISABLED, \
    .ampdu_rx_enable = WIFI_AMPDU_RX_ENABLED, \
    .ampdu_tx_enable = WIFI_AMPDU_TX_ENABLED, \
    .amsdu_tx_enable = WIFI_AMSDU_TX_DISABLED, \
    .nvs_enable = WIFI_NVS_DISABLED, \
    .nano_enable = WIFI_NANO_DISABLED, \
    .rx_ba_win = WIFI_DEFAULT_RX_BA_WIN, \
    .wifi_task_core_id = WIFI_TASK_CORE_ID, \
    .beacon_max_len = WIFI_SOFTAP_BEACON_MAX_LEN, \
    .mgmt_sbuf_num = WIFI_MGMT_SBUF_NUM, \
    .feature_caps = WIFI_FEATURE_CAPS, \
    .sta_disconnected_pm = false, \
    .espnow_max_encrypt_num = WIFI_ESPNOW_MAX_ENCRYPT_NUM, \
    .tx_hetb_queue_num = WIFI_TX_HETB_QUEUE_NUM, \
    .dump_hesigb_enable = false, \
    .privacy_enhancements = false, \
    .rmac_auto_reset_int = WIFI_RMAC_AUTO_RESET_INT_DEF, \
    .wifi_task_stack_size = WIFI_TASK_STACK_SIZE, \
    .magic = WIFI_INIT_CONFIG_MAGIC \
}

/* ========================================================================= */
/* Operating Modes, Interfaces, Storage, and Power-Save Enums               */
/* ========================================================================= */
typedef enum {
    WIFI_MODE_NULL = 0,
    WIFI_MODE_STA,
    WIFI_MODE_AP,
    WIFI_MODE_APSTA,
    WIFI_MODE_MAX
} wifi_mode_t;

typedef enum {
    WIFI_IF_STA = 0,
    WIFI_IF_AP  = 1,
} wifi_interface_t;

typedef enum {
    WIFI_STORAGE_FLASH,
    WIFI_STORAGE_RAM,
} wifi_storage_t;

typedef enum {
    WIFI_PS_NONE,
    WIFI_PS_MIN_MODEM,
    WIFI_PS_MAX_MODEM,
} wifi_ps_type_t;

typedef enum {
    WIFI_SECOND_CHAN_NONE = 0,
    WIFI_SECOND_CHAN_ABOVE,
    WIFI_SECOND_CHAN_BELOW,
} wifi_second_chan_t;

typedef enum {
    WIFI_BW20      = 1,
    WIFI_BW40      = 2,
    WIFI_BW80      = 3,
    WIFI_BW160     = 4,
    WIFI_BW80_BW80 = 5,
} wifi_bandwidth_t;

typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WEP,
    WIFI_AUTH_WPA_PSK,
    WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_ENTERPRISE,
    WIFI_AUTH_WPA2_ENTERPRISE = WIFI_AUTH_ENTERPRISE,
    WIFI_AUTH_WPA3_PSK,
    WIFI_AUTH_WPA2_WPA3_PSK,
    WIFI_AUTH_WAPI_PSK,
    WIFI_AUTH_OWE,
    WIFI_AUTH_WPA3_ENT_192,
    WIFI_AUTH_DUMMY_1,
    WIFI_AUTH_DUMMY_2,
    WIFI_AUTH_DPP,
    WIFI_AUTH_WPA3_ENTERPRISE,
    WIFI_AUTH_WPA2_WPA3_ENTERPRISE,
    WIFI_AUTH_WPA_ENTERPRISE,
    WIFI_AUTH_UNKNOWN,
    WIFI_AUTH_MAX
} wifi_auth_mode_t;

typedef enum {
    WIFI_CIPHER_TYPE_NONE = 0,
    WIFI_CIPHER_TYPE_WEP40,
    WIFI_CIPHER_TYPE_WEP104,
    WIFI_CIPHER_TYPE_TKIP,
    WIFI_CIPHER_TYPE_CCMP,
    WIFI_CIPHER_TYPE_TKIP_CCMP,
    WIFI_CIPHER_TYPE_AES_CMAC128,
    WIFI_CIPHER_TYPE_SMS4,
    WIFI_CIPHER_TYPE_GCMP,
    WIFI_CIPHER_TYPE_GCMP256,
    WIFI_CIPHER_TYPE_AES_GMAC128,
    WIFI_CIPHER_TYPE_AES_GMAC256,
    WIFI_CIPHER_TYPE_UNKNOWN,
} wifi_cipher_type_t;

typedef enum {
    WIFI_SCAN_TYPE_ACTIVE = 0,
    WIFI_SCAN_TYPE_PASSIVE,
} wifi_scan_type_t;

typedef enum {
    WIFI_FAST_SCAN = 0,
    WIFI_ALL_CHANNEL_SCAN,
} wifi_scan_method_t;

typedef enum {
    WIFI_CONNECT_AP_BY_SIGNAL = 0,
    WIFI_CONNECT_AP_BY_SECURITY,
} wifi_sort_method_t;

typedef enum {
    WIFI_COUNTRY_POLICY_AUTO,
    WIFI_COUNTRY_POLICY_MANUAL,
} wifi_country_policy_t;

typedef enum {
    WIFI_ANT_ANT0,
    WIFI_ANT_ANT1,
    WIFI_ANT_MAX,
} wifi_ant_t;

/* ========================================================================= */
/* Protocol Bitmaps                                                          */
/* ========================================================================= */
#define WIFI_PROTOCOL_11B               (1U << 0)
#define WIFI_PROTOCOL_11G               (1U << 1)
#define WIFI_PROTOCOL_11N               (1U << 2)
#define WIFI_PROTOCOL_LR                (1U << 3)
#define WIFI_PROTOCOL_11AX              (1U << 4)

/* ========================================================================= */
/* Wi-Fi Configuration Structures                                            */
/* ========================================================================= */
typedef struct {
    char                  cc[3];
    uint8_t               schan;
    uint8_t               nchan;
    int8_t                max_tx_power;
    wifi_country_policy_t policy;
} wifi_country_t;

typedef struct {
    struct {
        uint32_t min;
        uint32_t max;
    } active;
    uint32_t passive;
} wifi_scan_time_t;

typedef struct {
    uint16_t ghz_2_channels;
    uint32_t ghz_5_channels;
} wifi_scan_channel_bitmap_t;

typedef struct {
    int8_t           rssi;
    wifi_auth_mode_t authmode;
    uint8_t          rssi_5g_adjustment;
} wifi_scan_threshold_t;

typedef struct {
    uint8_t                   *ssid;
    uint8_t                   *bssid;
    uint8_t                    channel;
    bool                       show_hidden;
    wifi_scan_type_t           scan_type;
    wifi_scan_time_t           scan_time;
    uint8_t                    home_chan_dwell_time;
    wifi_scan_channel_bitmap_t channel_bitmap;
    bool                       coex_background_scan;
} wifi_scan_config_t;

typedef struct {
    bool capable;
    bool required;
} wifi_pmf_config_t;

typedef enum {
    WPA3_SAE_PWE_UNSPECIFIED,
    WPA3_SAE_PWE_HUNT_AND_PECK,
    WPA3_SAE_PWE_HASH_TO_ELEMENT,
    WPA3_SAE_PWE_BOTH,
} wifi_sae_pwe_method_t;

typedef enum {
    WPA3_SAE_PK_MODE_AUTOMATIC = 0,
    WPA3_SAE_PK_MODE_ONLY = 1,
    WPA3_SAE_PK_MODE_DISABLED = 2,
} wifi_sae_pk_mode_t;

typedef struct {
    uint16_t period;
    bool     protected_keep_alive;
} wifi_bss_max_idle_config_t;

typedef struct {
    uint8_t bss_color : 6;
    uint8_t partial_bss_color : 1;
    uint8_t bss_color_disabled : 1;
    uint8_t bssid_index;
} wifi_he_ap_info_t;

typedef struct {
    uint8_t            bssid[6];
    uint8_t            ssid[33];
    uint8_t            primary;
    wifi_second_chan_t second;
    int8_t             rssi;
    wifi_auth_mode_t   authmode;
    wifi_cipher_type_t pairwise_cipher;
    wifi_cipher_type_t group_cipher;
    wifi_ant_t         ant;
    uint32_t           phy_11b : 1;
    uint32_t           phy_11g : 1;
    uint32_t           phy_11n : 1;
    uint32_t           phy_lr : 1;
    uint32_t           phy_11a : 1;
    uint32_t           phy_11ac : 1;
    uint32_t           phy_11ax : 1;
    uint32_t           wps : 1;
    uint32_t           ftm_responder : 1;
    uint32_t           ftm_initiator : 1;
    uint32_t           akm_dpp : 1;
    uint32_t           reserved : 21;
    wifi_country_t     country;
    wifi_he_ap_info_t  he_ap;
    wifi_bandwidth_t   bandwidth;
    uint8_t            vht_ch_freq1;
    uint8_t            vht_ch_freq2;
} wifi_ap_record_t;

typedef struct {
    uint8_t                    ssid[32];
    uint8_t                    password[64];
    uint8_t                    ssid_len;
    uint8_t                    channel;
    wifi_auth_mode_t           authmode;
    uint8_t                    ssid_hidden;
    uint8_t                    max_connection;
    uint16_t                   beacon_interval;
    uint8_t                    csa_count;
    uint8_t                    dtim_period;
    wifi_cipher_type_t         pairwise_cipher;
    bool                       ftm_responder;
    wifi_pmf_config_t          pmf_cfg;
    wifi_sae_pwe_method_t      sae_pwe_h2e;
    uint8_t                    transition_disable : 1;
    uint8_t                    sae_ext : 1;
    uint8_t                    wpa3_compatible_mode : 1;
    uint8_t                    reserved : 5;
    wifi_bss_max_idle_config_t bss_max_idle_cfg;
    uint16_t                   gtk_rekey_interval;
} wifi_ap_config_t;

typedef struct {
    uint8_t               ssid[32];
    uint8_t               password[64];
    wifi_scan_method_t    scan_method;
    bool                  bssid_set;
    uint8_t               bssid[6];
    uint8_t               channel;
    uint16_t              listen_interval;
    wifi_sort_method_t    sort_method;
    wifi_scan_threshold_t threshold;
    wifi_pmf_config_t     pmf_cfg;
    uint32_t              rm_enabled : 1;
    uint32_t              btm_enabled : 1;
    uint32_t              mbo_enabled : 1;
    uint32_t              ft_enabled : 1;
    uint32_t              owe_enabled : 1;
    uint32_t              transition_disable : 1;
    uint32_t              disable_wpa3_compatible_mode : 1;
    uint32_t              reserved1 : 25;
    wifi_sae_pwe_method_t sae_pwe_h2e;
    wifi_sae_pk_mode_t    sae_pk_mode;
    uint8_t               failure_retry_cnt;
    uint32_t              he_dcm_set : 1;
    uint32_t              he_dcm_max_constellation_tx : 2;
    uint32_t              he_dcm_max_constellation_rx : 2;
    uint32_t              he_mcs9_enabled : 1;
    uint32_t              he_su_beamformee_disabled : 1;
    uint32_t              he_trig_su_bmforming_feedback_disabled : 1;
    uint32_t              he_trig_mu_bmforming_partial_feedback_disabled : 1;
    uint32_t              he_trig_cqi_feedback_disabled : 1;
    uint32_t              vht_su_beamformee_disabled : 1;
    uint32_t              vht_mu_beamformee_disabled : 1;
    uint32_t              vht_mcs8_enabled : 1;
    uint32_t              max_bandwidth_negotiation_enabled_2g : 1;
    uint32_t              max_bandwidth_negotiation_enabled_5g : 1;
    uint32_t              reserved2 : 17;
    uint8_t               sae_h2e_identifier[32];
} wifi_sta_config_t;

typedef struct {
    uint8_t  op_channel;
    uint8_t  master_pref;
    uint8_t  scan_time;
    uint16_t warm_up_sec;
    bool     disable_random_mac;
    bool     reset_current_nvs_creds;
    bool     use_nvs_for_caching;
    bool     group_mgmt_prot;
} wifi_nan_sync_config_t;

typedef union {
    wifi_ap_config_t       ap;
    wifi_sta_config_t      sta;
    wifi_nan_sync_config_t nan;
} wifi_config_t;

/* ========================================================================= */
/* Promiscuous RX Metadata & Filters                                         */
/* ========================================================================= */
#define WIFI_PROMIS_FILTER_MASK_ALL         (0xFFFFFFFFU)
#define WIFI_PROMIS_FILTER_MASK_MGMT        (1U << 0)
#define WIFI_PROMIS_FILTER_MASK_CTRL        (1U << 1)
#define WIFI_PROMIS_FILTER_MASK_DATA        (1U << 2)
#define WIFI_PROMIS_FILTER_MASK_MISC        (1U << 3)
#define WIFI_PROMIS_FILTER_MASK_DATA_MPDU   (1U << 4)
#define WIFI_PROMIS_FILTER_MASK_DATA_AMPDU  (1U << 5)
#define WIFI_PROMIS_FILTER_MASK_FCSFAIL     (1U << 6)
#define WIFI_PROMIS_CTRL_FILTER_MASK_ALL    (0xFF800000U)

typedef struct {
    uint32_t filter_mask;
} wifi_promiscuous_filter_t;

typedef enum {
    WIFI_PKT_MGMT,
    WIFI_PKT_CTRL,
    WIFI_PKT_DATA,
    WIFI_PKT_MISC,
} wifi_promiscuous_pkt_type_t;

typedef struct {
    signed   rssi : 8;
    unsigned rate : 5;
    unsigned : 1;
    unsigned : 2;
    unsigned : 12;
    unsigned rxmatch0 : 1;
    unsigned rxmatch1 : 1;
    unsigned rxmatch2 : 1;
    unsigned rxmatch3 : 1;
    uint32_t he_siga1;
    unsigned rxend_state : 8;
    uint16_t he_siga2;
    unsigned : 7;
    unsigned is_group : 1;
    unsigned timestamp : 32;
    unsigned : 15;
    unsigned : 15;
    unsigned : 2;
    signed   noise_floor : 8;
    unsigned channel : 4;
    unsigned second : 4;
    unsigned : 8;
    unsigned : 8;
    unsigned : 32;
    unsigned : 32;
    unsigned : 2;
    unsigned : 4;
    unsigned : 2;
    unsigned rx_channel_estimate_len : 10;
    unsigned rx_channel_estimate_info_vld : 1;
    unsigned : 1;
    unsigned : 11;
    unsigned : 1;
    unsigned : 24;
    unsigned cur_bb_format : 4;
    unsigned cur_single_mpdu : 1;
    unsigned : 3;
    unsigned : 32;
    unsigned : 32;
    unsigned : 32;
    unsigned : 32;
    unsigned : 32;
    unsigned : 32;
    unsigned : 32;
    unsigned : 32;
    unsigned : 8;
    unsigned he_sigb_len : 6;
    unsigned : 2;
    unsigned : 8;
    unsigned : 8;
    unsigned : 32;
    unsigned : 7;
    unsigned : 1;
    unsigned : 8;
    unsigned : 16;
    unsigned sig_len : 14;
    unsigned : 2;
    unsigned dump_len : 14;
    unsigned : 2;
    unsigned rx_state : 8;
    unsigned : 24;
} __attribute__((packed)) esp_wifi_rxctrl_t;

typedef esp_wifi_rxctrl_t wifi_pkt_rx_ctrl_t;

typedef struct {
    wifi_pkt_rx_ctrl_t rx_ctrl;
    uint8_t payload[0];
} wifi_promiscuous_pkt_t;

typedef esp_err_t (*wifi_rxcb_t)(void *buffer, uint16_t len, void *eb);
typedef void (*wifi_promiscuous_cb_t)(void *buf, wifi_promiscuous_pkt_type_t type);

/* ========================================================================= */
/* Logging and Event Types                                                   */
/* ========================================================================= */
typedef enum {
    WIFI_LOG_NONE = 0,
    WIFI_LOG_ERROR,
    WIFI_LOG_WARNING,
    WIFI_LOG_INFO,
    WIFI_LOG_DEBUG,
    WIFI_LOG_VERBOSE,
} wifi_log_level_t;

typedef enum {
    WIFI_LOG_MODULE_ALL  = 0,
    WIFI_LOG_MODULE_WIFI,
    WIFI_LOG_MODULE_COEX,
    WIFI_LOG_MODULE_MESH,
} wifi_log_module_t;

#define WIFI_LOG_SUBMODULE_ALL   (0)
#define WIFI_LOG_SUBMODULE_INIT  (1)
#define WIFI_LOG_SUBMODULE_IOCTL (1 << 1)
#define WIFI_LOG_SUBMODULE_CONN  (1 << 2)
#define WIFI_LOG_SUBMODULE_SCAN  (1 << 3)

typedef const char* esp_event_base_t;
extern const char *WIFI_EVENT;

/* Wi-Fi Event Enumerations */
typedef enum {
    WIFI_EVENT_WIFI_READY = 0,
    WIFI_EVENT_SCAN_DONE,
    WIFI_EVENT_STA_START,
    WIFI_EVENT_STA_STOP,
    WIFI_EVENT_STA_CONNECTED,
    WIFI_EVENT_STA_DISCONNECTED,
    WIFI_EVENT_STA_AUTHMODE_CHANGE,
    WIFI_EVENT_STA_WPS_ER_SUCCESS,
    WIFI_EVENT_STA_WPS_ER_FAILED,
    WIFI_EVENT_STA_WPS_ER_TIMEOUT,
    WIFI_EVENT_STA_WPS_ER_PIN,
    WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP,
    WIFI_EVENT_AP_START,
    WIFI_EVENT_AP_STOP,
    WIFI_EVENT_AP_STACONNECTED,
    WIFI_EVENT_AP_STADISCONNECTED,
    WIFI_EVENT_AP_PROBEREQRECVED,
    WIFI_EVENT_FTM_REPORT,
    WIFI_EVENT_STA_BSS_RSSI_LOW,
    WIFI_EVENT_ACTION_TX_STATUS,
    WIFI_EVENT_ROC_DONE,
    WIFI_EVENT_STA_BEACON_TIMEOUT,
    WIFI_EVENT_MAX
} wifi_event_t;

/* Wi-Fi Event Payloads */
typedef struct {
    uint8_t          ssid[32];
    uint8_t          ssid_len;
    uint8_t          bssid[6];
    uint8_t          channel;
    wifi_auth_mode_t authmode;
    uint16_t         aid;
} wifi_event_sta_connected_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t ssid_len;
    uint8_t bssid[6];
    uint8_t reason;
    int8_t  rssi;
} wifi_event_sta_disconnected_t;

typedef struct {
    uint8_t mac[6];
    uint8_t aid;
    bool    is_mesh_child;
} wifi_event_ap_staconnected_t;

typedef struct {
    uint8_t  mac[6];
    uint8_t  aid;
    bool     is_mesh_child;
    uint16_t reason;
} wifi_event_ap_stadisconnected_t;

/* ========================================================================= */
/* Espressif Closed-Source Wi-Fi Library Function Prototypes                */
/* (Implemented by libs/esp32c6/libnet80211.a and libpp.a)                   */
/* ========================================================================= */
esp_err_t esp_wifi_init_internal(const wifi_init_config_t *config);
esp_err_t esp_wifi_deinit_internal(void);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);
esp_err_t esp_wifi_set_storage(wifi_storage_t storage);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type);
esp_err_t esp_wifi_get_inactive_time(wifi_interface_t ifx, uint16_t *sec);
esp_err_t esp_wifi_set_protocol(wifi_interface_t ifx, uint8_t protocol_bitmap);
esp_err_t esp_wifi_set_country(const wifi_country_t *country);
esp_err_t esp_wifi_set_max_tx_power(int8_t power);
esp_err_t esp_wifi_get_max_tx_power(int8_t *power);
esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf);
esp_err_t esp_wifi_get_config(wifi_interface_t interface, wifi_config_t *conf);
esp_err_t esp_wifi_internal_reg_rxcb(wifi_interface_t ifx, wifi_rxcb_t fn);
void      esp_wifi_internal_free_rx_buffer(void *eb);
esp_err_t esp_wifi_internal_set_sta_ip(void);
esp_err_t esp_wifi_internal_tx(wifi_interface_t ifx, void *buffer, uint16_t len);
esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6]);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t second);
esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *filter);
esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t cb);
esp_err_t esp_wifi_set_promiscuous(bool en);
esp_err_t esp_wifi_set_bandwidth(wifi_interface_t ifx, wifi_bandwidth_t bw);
esp_err_t esp_wifi_config_11b_rate(wifi_interface_t ifx, bool disable);
esp_err_t esp_wifi_internal_set_log_level(wifi_log_level_t level);
esp_err_t esp_wifi_internal_set_log_mod(wifi_log_module_t module, uint32_t submodule, bool enable);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_set_sta_key_internal(int alg, const uint8_t *addr, int key_idx, int set_tx,
                                        const uint8_t *seq, size_t seq_len,
                                        const uint8_t *key, size_t key_len, int key_flag);

/* Supplicant-facing blob API (ESP-IDF 66ab063a9a7f esp_wifi_driver.h) */
typedef void (*eapol_txcb_t)(uint8_t *eapol_payload, size_t len, bool tx_failure);
int      esp_wifi_set_appie_internal(uint8_t type, uint8_t *ie, uint16_t len, uint8_t flag);
int      esp_wifi_unset_appie_internal(uint8_t type);
esp_err_t esp_wifi_sta_connect_internal(const uint8_t *bssid);
bool     esp_wifi_auth_done_internal(void);
void     esp_wifi_deauthenticate_internal(uint8_t reason_code);
int      esp_wifi_register_eapol_txdonecb_internal(eapol_txcb_t fn);
bool     esp_wifi_sta_prof_is_rsn_internal(void);
uint8_t  esp_wifi_sta_get_prof_authmode_internal(void);
uint8_t  esp_wifi_sta_get_pairwise_cipher_internal(void);
uint8_t  esp_wifi_sta_get_group_cipher_internal(void);
struct wifi_ssid { int len; uint8_t ssid[32]; };
struct wifi_ssid *esp_wifi_sta_get_prof_ssid_internal(void);
uint8_t *esp_wifi_sta_get_ie(uint8_t *bssid, uint8_t elem_id);
#define WIFI_APPIE_RSN                      4U
#define WIFI_APPIE_BY_REFERENCE             1U

esp_err_t esp_event_send_internal(esp_event_base_t event_base, int32_t event_id, void *event_data, size_t event_data_size, uint32_t ticks_to_wait);

#ifdef __cplusplus
}
#endif

#endif /* IRON_V_WIFI_VENDOR_TYPES_H */
