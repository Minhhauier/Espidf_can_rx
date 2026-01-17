#include "isotp.h"
#include "isotp_handler.h"
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

static const char *TAG = "ISO-TP-TX";

// Global ISO-TP link and buffers
static IsoTpLink g_link;
static uint8_t g_isotpSendBuf[1024];
static uint8_t g_isotpRecvBuf[1024];

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

// Task xử lý CAN messages và poll ISO-TP
void isotp_can_handler_task(void *pvParameter) {
    twai_message_t rx_msg;
    int frame_count = 0;
    
    while (1) {
        // Đọc CAN messages từ bus
        esp_err_t r = twai_receive(&rx_msg, pdMS_TO_TICKS(50));
        if (r == ESP_OK) {
            frame_count++;
            printf("[#%d] RX CAN: ID=0x%03lX, DLC=%d, Data=[%02X %02X %02X %02X %02X %02X %02X %02X]\n", 
                   frame_count, rx_msg.identifier, rx_msg.data_length_code,
                   rx_msg.data[0], rx_msg.data[1], rx_msg.data[2], rx_msg.data[3],
                   rx_msg.data[4], rx_msg.data[5], rx_msg.data[6], rx_msg.data[7]);
            
            // ⚠️ CRITICAL FIX: TX must filter to only process Flow Control from RX (ID 0x787)
            // TX sends data on 0x789, RX sends FC on 0x787
            if (rx_msg.identifier == 0x787) {
                printf("  ↳ [FC] Flow Control received, feeding to ISO-TP\n");
                // Feed vào ISO-TP stack
                isotp_on_can_message(&g_link, rx_msg.data, rx_msg.data_length_code);
            }
        }
        
        // Poll ISO-TP stack (send consecutive frames, handle timeouts)
        isotp_poll(&g_link);
        
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// Function to send ISO-TP message (non-blocking)
static esp_err_t isotp_send_message(const char *message) {
    uint16_t len = strlen(message);
    printf("═══ SEND ISO-TP MESSAGE (%u bytes) ═══\n", len);
    printf("Send: %s\n", message);
    
    int ret = isotp_send(&g_link, (const uint8_t *)message, len);
    if (ret != ISOTP_RET_OK) {
        ESP_LOGE(TAG, "✗ isotp_send failed: %d", ret);
        return ESP_FAIL;
    }
    
    printf("✓ Message queued for transmission\n");
    return ESP_OK;
}

void app_main(void) {
    // Init TWAI
    if (twai_init() != ESP_OK) {
        ESP_LOGE(TAG, "TWAI init failed");
        return;
    }
    printf("\n=== INIT ISO-TP ===\n");
    
    // Initialize ISO-TP link
    // TX sends data on 0x789 (First Frame, Consecutive Frames)
    // TX receives Flow Control on 0x787
    isotp_init_link(&g_link, 0x789,  // send_arbitration_id: TX sends on 0x789
                    g_isotpSendBuf, sizeof(g_isotpSendBuf),
                    g_isotpRecvBuf, sizeof(g_isotpRecvBuf));
    
    // ⚠️ CRITICAL FIX: Set receive ID for Flow Control
    g_link.receive_arbitration_id = 0x787;  // RX sends FC on 0x787
    printf("ISO-TP Config:\n");
    printf("  Send ID (FF/CF):   0x%03lX\n", g_link.send_arbitration_id);
    printf("  Receive ID (FC):   0x%03lX\n", g_link.receive_arbitration_id);
    
    xTaskCreate(isotp_can_handler_task, "isotp_handler", 4096, NULL, 10, NULL);

    vTaskDelay(pdMS_TO_TICKS(1000));

    // Example message to send
    const char *message = "Bro đang định làm ứng dụng gì mà cần gửi cho nhiều Node? Nếu là cập nhật Firmware (FOTA) cho một loạt thiết bị, tôi khuyên nên cân nhắc kỹ vì tính an toàn dữ liệu trên CAN rất quan trọng.";

    printf("\n=== START SENDING ISO-TP MESSAGES ===\n");
    
    while (1) {
        isotp_send_message(message);
        vTaskDelay(pdMS_TO_TICKS(5000));  // Send every 5 seconds
    }
}
