/** @file wifi_manager.c
 * @brief Wi-Fi connection manager + SNTP time sync.
 *
 * Reads [wifi_N] entries from the loaded RecorderConfig (Task 5),
 * connects in order, auto-reconnects on drop, runs SNTP on first
 * connect, applies timezone, and emits connected/disconnected events.
 */

#include "wifi_manager.h"
#include "net_selection.h"
#include "config.h"
#include "device_events.h"

#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "recorder.h"

/* ── Log tag ─────────────────────────────────────────────────────────── */

static const char *TAG = "wifi_mgr";

/* ── Static state ────────────────────────────────────────────────────── */

static const RecorderConfig *s_cfg        = NULL;
static TaskHandle_t           s_task       = NULL;
static volatile bool          s_running    = false;
static volatile bool          s_connected  = false;
static bool                   s_time_synced = false;
static int                    s_try_index  = -1;

/* Event group for connection status */
static EventGroupHandle_t     s_wifi_event_group = NULL;

/* ── Forward declarations ────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);

/* ── SNTP callback ───────────────────────────────────────────────────── */

static void on_sntp_sync(struct timeval *tv)
{
    (void)tv;
    ESP_LOGI(TAG, "SNTP time synced");

    if (!s_cfg) return;

    /* Apply [device].timezone via setenv/tzset */
    if (s_cfg->timezone[0] != '\0') {
        setenv("TZ", s_cfg->timezone, 1);
        tzset();
        ESP_LOGI(TAG, "Timezone set to %s", s_cfg->timezone);
    }

    s_time_synced = true;

    /* Notify recorder so subsequent recording IDs use wall-clock timestamps */
    recorder_set_time_synced(true);
}

/* ── Connect to a specific network ───────────────────────────────────── */

static bool connect_to_network(int index)
{
    if (!s_cfg || index < 0 || index >= s_cfg->wifi_count) {
        return false;
    }

    const wifi_network_t *net = &s_cfg->wifi_networks[index];

    if (net->ssid[0] == '\0') {
        ESP_LOGW(TAG, "Network %d has empty SSID — skipping", index + 1);
        return false;
    }

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, net->ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    if (net->password[0] != '\0') {
        strncpy((char *)wifi_cfg.sta.password, net->password,
                sizeof(wifi_cfg.sta.password) - 1);
        wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_LOGI(TAG, "Connecting to %s (network %d/%d)...",
             net->ssid, index + 1, s_cfg->wifi_count);

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

/* ── Disconnect from current AP ──────────────────────────────────────── */

static void disconnect_current(void)
{
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGE(TAG, "esp_wifi_disconnect failed: %s", esp_err_to_name(err));
    }
    s_connected = false;
}

/* ── Wi-Fi event handler ─────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started");
            /* Don't connect here — the task loop handles the connect sequence */
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "STA connected to AP");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *ev =
                (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "STA disconnected (reason: %d)", ev->reason);

            bool was_connected = s_connected;
            s_connected = false;

            if (s_wifi_event_group) {
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
            }

            if (was_connected) {
                esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_WIFI_DISCONNECTED,
                               NULL, 0, 0);
            }
            break;
        }

        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));

        s_connected = true;

        if (s_wifi_event_group) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }

        /* First successful connect → run SNTP */
        if (!s_time_synced) {
            ESP_LOGI(TAG, "First connect — starting SNTP sync");
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            sntp_set_time_sync_notification_cb(on_sntp_sync);
            esp_sntp_init();
        }

        /* Emit connected event for UI + upload task */
        esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_WIFI_CONNECTED,
                       NULL, 0, 0);
    }
}

/* ── Main Wi-Fi manager task ─────────────────────────────────────────── */

static void wifi_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Wi-Fi task started (prio %d, stack %d)",
             (int)WIFI_MANAGER_TASK_PRIORITY,
             (int)WIFI_MANAGER_TASK_STACK_SIZE);

    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        vTaskDelete(NULL);
        return;
    }

    /* ── Initialise Wi-Fi stack ──────────────────────────────────── */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (!sta_netif) {
        ESP_LOGE(TAG, "Failed to create default STA netif");
        vTaskDelete(NULL);
        return;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* ── Connection loop ──────────────────────────────────────────── */
    s_try_index = -1;
    TickType_t reconnect_delay = pdMS_TO_TICKS(5000); /* 5 s between attempts */

    while (s_running) {
        int next_index = -1;
        net_connect_result_t last_result;

        /* Map the last attempt result for net_selection_next */
        if (s_try_index >= 0) {
            /* We previously tried — was it successful? Check connected state */
            last_result = s_connected ? NET_RESULT_SUCCESS : NET_RESULT_FAILURE;
        } else {
            last_result = NET_RESULT_FAILURE;
        }

        net_next_action_t action = net_selection_next(
            s_cfg ? s_cfg->wifi_count : 0,
            s_try_index,
            last_result,
            s_connected,
            &next_index);

        switch (action) {
        case NET_NEXT_TRY_INDEX:
            if (next_index != s_try_index) {
                /* Disconnect from current (if any) before switching */
                if (s_try_index >= 0) {
                    disconnect_current();
                }
                s_try_index = next_index;
                if (!connect_to_network(next_index)) {
                    /* Connect failed immediately — advance next loop */
                    s_connected = false;
                }
            } else if (!s_connected) {
                /* Retry same network after delay */
                if (!connect_to_network(next_index)) {
                    s_connected = false;
                }
            }
            /* Wait for a connection event or timeout */
            if (!s_connected) {
                EventBits_t bits = xEventGroupWaitBits(
                    s_wifi_event_group,
                    WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT,
                    pdTRUE,  /* clear on exit */
                    pdFALSE, /* wait for any */
                    reconnect_delay);
                /* If timeout, will loop and retry/advance */
                (void)bits;
            } else {
                /* Connected — wait for disconnection */
                xEventGroupWaitBits(
                    s_wifi_event_group,
                    WIFI_DISCONNECTED_BIT,
                    pdTRUE,  /* clear on exit */
                    pdFALSE, /* wait for any */
                    portMAX_DELAY);
            }
            break;

        case NET_NEXT_NO_MORE:
            /* No more networks to try — wait and retry from beginning */
            ESP_LOGW(TAG, "All %d networks exhausted — retrying from start in 30 s",
                     s_cfg ? s_cfg->wifi_count : 0);
            vTaskDelay(pdMS_TO_TICKS(30000));
            disconnect_current();
            s_try_index = -1;
            s_connected = false;
            break;

        case NET_NEXT_ALREADY_CONNECTED:
            /* Stay connected, wait for disconnect */
            xEventGroupWaitBits(
                s_wifi_event_group,
                WIFI_DISCONNECTED_BIT,
                pdTRUE,  /* clear on exit */
                pdFALSE, /* wait for any */
                portMAX_DELAY);
            break;
        }
    }

    /* ── Cleanup ─────────────────────────────────────────────────── */
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                 &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                 &wifi_event_handler);

    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(sta_netif);
    esp_event_loop_delete_default();
    esp_netif_deinit();

    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    s_connected = false;
    ESP_LOGI(TAG, "Wi-Fi task stopped");
    vTaskDelete(NULL);
}

/* ── Public API ──────────────────────────────────────────────────────── */

void wifi_manager_init(const RecorderConfig *cfg)
{
    if (!cfg) {
        ESP_LOGE(TAG, "wifi_manager_init: cfg is NULL");
        return;
    }

    /* If no Wi-Fi networks configured, still create the task — it just
     * immediately enters the NET_NEXT_NO_MORE / retry-from-start loop,
     * which is harmless.  No crash, no blocking. */
    if (cfg->wifi_count == 0) {
        ESP_LOGW(TAG, "No Wi-Fi networks configured — Wi-Fi task will idle");
    }

    s_cfg     = cfg;
    s_running = true;

    BaseType_t created = xTaskCreate(
        wifi_task,
        "wifi_mgr",
        WIFI_MANAGER_TASK_STACK_SIZE,
        NULL,
        WIFI_MANAGER_TASK_PRIORITY,
        &s_task);

    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi manager task");
        s_running = false;
        return;
    }

    ESP_LOGI(TAG, "Wi-Fi manager initialized (%d networks)", cfg->wifi_count);
}

void wifi_manager_deinit(void)
{
    s_running = false;

    /* Signal the event group so the task wakes up if waiting */
    if (s_wifi_event_group) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
    }

    if (s_task) {
        vTaskDelay(pdMS_TO_TICKS(500));
        s_task = NULL;
    }

    s_cfg = NULL;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}
