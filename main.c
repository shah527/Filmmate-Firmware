#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gatts_api.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ESP_APP_ID 0x55
#define DEVICE_NAME "FilmMate Tripod"
#define SVC_INST_ID 0

// UUIDs in little-endian order.
// Service UUID: 12345678-1234-5678-1234-56789abcdef0
static uint8_t service_uuid[16] = {
    0x78, 0x56, 0x34, 0x12,
    0x34, 0x12,
    0x78, 0x56,
    0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};

// Characteristic UUID: 12345678-1234-5678-1234-56789abcdef1
static uint8_t char_uuid[16] = {
    0x78, 0x56, 0x34, 0x12,
    0x34, 0x12,
    0x78, 0x56,
    0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf1};

static uint8_t char_prop = ESP_GATT_CHAR_PROP_BIT_READ |
                           ESP_GATT_CHAR_PROP_BIT_WRITE |
                           ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static int32_t char_value = 0;  // Current characteristic value
static uint16_t cccd_value = 0; // CCCD for notifications

enum
{
    IDX_SVC,
    IDX_CHAR,
    IDX_CHAR_VAL,
    IDX_CHAR_CFG,
    IDX_NB,
};

static const uint16_t char_decl_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t cccd_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

// GATT attribute database: Service, Characteristic Declaration, Value, and CCCD.
static esp_gatts_attr_db_t gatt_db[IDX_NB] = {
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_128,
            service_uuid,
            ESP_GATT_PERM_READ,
            sizeof(service_uuid),
            sizeof(service_uuid),
            service_uuid,
        }},
    [IDX_CHAR] = {{ESP_GATT_AUTO_RSP}, {
                                           ESP_UUID_LEN_16,
                                           (uint8_t *)&char_decl_uuid,
                                           ESP_GATT_PERM_READ,
                                           sizeof(uint8_t),
                                           sizeof(uint8_t),
                                           (uint8_t *)&char_prop,
                                       }},
    [IDX_CHAR_VAL] = {{ESP_GATT_AUTO_RSP}, {
                                               ESP_UUID_LEN_128,
                                               char_uuid,
                                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                               sizeof(int32_t),
                                               sizeof(int32_t),
                                               (uint8_t *)&char_value,
                                           }},
    [IDX_CHAR_CFG] = {{ESP_GATT_AUTO_RSP}, {
                                               ESP_UUID_LEN_16,
                                               (uint8_t *)&cccd_uuid,
                                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                               sizeof(uint16_t),
                                               sizeof(uint16_t),
                                               (uint8_t *)&cccd_value,
                                           }},
};

struct gatts_profile_inst
{
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
};

static struct gatts_profile_inst profile = {
    .gatts_cb = NULL,
    .gatts_if = ESP_GATT_IF_NONE,
    .app_id = ESP_APP_ID,
};

// GAP callback: Called when advertising data is set.
static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT)
    {
        esp_ble_adv_params_t adv_params = {
            .adv_int_min = 0x20,
            .adv_int_max = 0x20,
            .adv_type = ADV_TYPE_IND,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .channel_map = ADV_CHNL_ALL,
            .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        };
        esp_ble_gap_start_advertising(&adv_params);
    }
}

// GATTS profile callback: Handles registration, service creation, and writes.
static void gatts_profile_cb(esp_gatts_cb_event_t event,
                             esp_gatt_if_t gatts_if,
                             esp_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GATTS_REG_EVT:
        profile.gatts_if = gatts_if;
        profile.service_id.is_primary = true;
        profile.service_id.id.inst_id = SVC_INST_ID;
        profile.service_id.id.uuid.len = ESP_UUID_LEN_128;
        memcpy(profile.service_id.id.uuid.uuid.uuid128, service_uuid, 16);
        esp_ble_gatts_create_service(gatts_if, &profile.service_id, IDX_NB);
        esp_ble_gap_set_device_name(DEVICE_NAME);
        {
            esp_ble_adv_data_t adv_data = {
                .set_scan_rsp = false,
                .include_name = true,
                .include_txpower = true,
                .min_interval = 0x0006,
                .max_interval = 0x0010,
                .appearance = 0x00,
                .manufacturer_len = 0,
                .p_manufacturer_data = NULL,
                .service_data_len = 0,
                .p_service_data = NULL,
                .service_uuid_len = 16,
                .p_service_uuid = service_uuid,
                .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
            };
            esp_ble_gap_config_adv_data(&adv_data);
        }
        break;
    case ESP_GATTS_CREATE_EVT:
        profile.service_handle = param->create.service_handle;
        esp_ble_gatts_start_service(profile.service_handle);
        esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, IDX_NB, SVC_INST_ID);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status == ESP_GATT_OK &&
            param->add_attr_tab.num_handle == IDX_NB)
        {
            profile.char_handle = param->add_attr_tab.handles[IDX_CHAR_VAL];
        }
        break;
    case ESP_GATTS_WRITE_EVT:
        if (!param->write.is_prep && param->write.handle == profile.char_handle)
        {
            if (param->write.len == sizeof(int32_t))
            {
                int32_t received = 0;
                memcpy(&received, param->write.value, sizeof(int32_t));
                int32_t response = received + 1;
                char_value = response;
                esp_ble_gatts_set_attr_value(profile.char_handle, sizeof(int32_t),
                                             (uint8_t *)&char_value);
                esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, profile.char_handle,
                                            sizeof(int32_t), (uint8_t *)&char_value, false);
            }
        }
        break;
    default:
        break;
    }
}

// Dispatch GATTS events to the profile callback.
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT && param->reg.app_id == profile.app_id)
    {
        profile.gatts_cb = gatts_profile_cb;
    }
    if (profile.gatts_cb)
    {
        profile.gatts_cb(event, gatts_if, param);
    }
}

void app_main(void)
{
    // Initialize NVS.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }
    // Release Classic BT memory, we only use BLE.
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init();
    esp_bluedroid_enable();
    // Register GATTS and GAP callbacks.
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_cb);
    esp_ble_gatts_app_register(ESP_APP_ID);
}
