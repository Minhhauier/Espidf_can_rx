// ISO-TP + TWAI integration demo for multi-byte messages - RECEIVER
#include "isotp.h"
#include "isotp_handler.c"
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ISO-TP-RX";

// Global ISO-TP link and buffers
static volatile IsoTpLink g_link;
static volatile  uint8_t g_isotpSendBuf[1024];
static volatile  uint8_t g_isotpRecvBuf[1024];

// Configure and start TWAI (CAN) at 500kbps using GPIO27 (TX) / GPIO36 (RX)
static esp_err_t twai_init(void) {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(27, 36, TWAI_MODE_NORMAL);
    twai_timing_config_t  t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t  f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_LOGI(TAG, "Installing TWAI driver on GPIO27(TX)/GPIO36(RX)...");
    esp_err_t r = twai_driver_install(&g_config, &t_config, &f_config);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TWAI: %s", esp_err_to_name(r));
        return r;
    }

    ESP_LOGI(TAG, "Starting TWAI...");
    r = twai_start();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TWAI: %s", esp_err_to_name(r));
        return r;
    }
    
    ESP_LOGI(TAG, "✓ TWAI initialized successfully");
    return r;
}

void app_main(void) {
    // Init TWAI

    if (twai_init() != ESP_OK) {
        ESP_LOGE(TAG, "TWAI init failed");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));


    ESP_LOGI(TAG, "=== INIT ISO-TP ===");
    isotp_init_link(&g_link, 0x787, 
                    g_isotpSendBuf, sizeof(g_isotpSendBuf),
                    g_isotpRecvBuf, sizeof(g_isotpRecvBuf));

    ESP_LOGI(TAG, "✓ ISO-TP Receiver ready");
    ESP_LOGI(TAG, "  - Nhận data từ TX (ID=0x789)");
    ESP_LOGI(TAG, "  - Gửi FC trả lại (ID=0x788)");

    // Buffer to print full reassembled message
    uint8_t app_buf[512];
    uint16_t app_len = 0;
    int msg_count = 0;
    int frame_count = 0;
    static uint32_t last_fc_time = 0;
    //esp_log_level_set("*", ESP_LOG_NONE);
    while (1) {
        // 1) Read raw CAN frames and feed ISO-TP (tăng timeout để catch tất cả frames)
        twai_message_t rx_msg;
        esp_err_t r = twai_receive(&rx_msg, pdMS_TO_TICKS(10));
        if (r == ESP_OK) {
            //  vTaskDelay(pdMS_TO_TICKS(2));
            frame_count++;
            // ESP_LOGI(TAG, "[#%d] RX CAN: ID=0x%lX, DLC=%d, Data=[%02X %02X %02X %02X %02X %02X %02X %02X]", 
            //          frame_count, rx_msg.identifier, rx_msg.data_length_code,
            //          rx_msg.data[0], rx_msg.data[1], rx_msg.data[2], rx_msg.data[3],
            //          rx_msg.data[4], rx_msg.data[5], rx_msg.data[6], rx_msg.data[7]);
            // printf("[#%d] RX CAN: ID=0x%lX, DLC=%d, Data=[%02X %02X %02X %02X %02X %02X %02X %02X]\n", 
            //          frame_count, rx_msg.identifier, rx_msg.data_length_code,
            //          rx_msg.data[0], rx_msg.data[1], rx_msg.data[2], rx_msg.data[3],
            //          rx_msg.data[4], rx_msg.data[5], rx_msg.data[6], rx_msg.data[7]);
            if(rx_msg.identifier != 0x789) {
                ESP_LOGW(TAG, "  - Bỏ qua frame không đúng ID (chỉ nhận ID=0x789)");
                continue;
            }
            isotp_on_can_message(&g_link, rx_msg.data, rx_msg.data_length_code);
        }
        // esp_log_level_set("*", ESP_LOG_NONE);
        // 2) Check if a complete ISO-TP message is available
        if (isotp_receive(&g_link, app_buf, sizeof(app_buf), &app_len) == ISOTP_RET_OK) {
            msg_count++;
            
            // Ensure printable string
            uint16_t n = (app_len < sizeof(app_buf) - 1) ? app_len : (sizeof(app_buf) - 1);
            app_buf[n] = '\0';
    
  
            printf("════════════════════════════════════════════════\n");
            printf("Nội dung: %s\n", (char*)app_buf);
        }
//        vTaskDelay(pdMS_TO_TICKS(2));
        // 3) Poll the ISO-TP stack to send Flow Control & manage timeouts
           uint32_t now = isotp_user_get_ms();
        if(now - last_fc_time > 10) {  // Chỉ poll mỗi 10ms
            isotp_poll(&g_link);
            last_fc_time = now;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
