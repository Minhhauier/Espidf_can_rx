#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Cấu trúc để định nghĩa thông tin gói tin CAN
typedef struct {
    uint32_t id;              // ID của tin nhắn CAN
    uint8_t data[8];          // Dữ liệu (tối đa 8 byte)
    uint8_t data_length;      // Số byte dữ liệu
    bool is_extended_id;      // Sử dụng ID mở rộng (29-bit)
} can_message_t;

/**
 * Khởi tạo bộ điều khiển CAN
 * @return ESP_OK nếu thành công, ngược lại trả về mã lỗi
 */
esp_err_t can_init(void)
{
    // Cấu hình pins CAN (TX: GPIO17, RX: GPIO16 - giống như bên TX)
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(27, 36, TWAI_MODE_NORMAL);
    
    // Cấu hình timing - 500kbps baud rate (giống như bên TX)
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    
    // Cấu hình bộ lọc để nhận tất cả các gói tin
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // Cài đặt driver TWAI
    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        printf("Lỗi: Không thể cài đặt driver CAN\n");
        return ESP_FAIL;
    }

    // Khởi động driver
    if (twai_start() != ESP_OK) {
        printf("Lỗi: Không thể khởi động CAN\n");
        return ESP_FAIL;
    }

    printf("✓ CAN được khởi tạo thành công (500kbps, GPIO17/16)\n");
    return ESP_OK;
}
/**
 * Hàm nhận gói tin CAN
 * @param msg Con trỏ đến cấu trúc chứa thông tin gói tin nhận
 * @param timeout_ms Thời gian chờ (ms), 0 = không chờ, -1 = chờ vô hạn
 * @return ESP_OK nếu nhận thành công, ngược lại trả về mã lỗi
 */
esp_err_t can_receive_message(can_message_t *msg, uint32_t timeout_ms)
{
    if (msg == NULL) {
        printf("Lỗi: Thông tin gói tin không hợp lệ\n");
        return ESP_ERR_INVALID_ARG;
    }

    twai_message_t rx_frame = {0};

    // Nhận frame từ CAN bus với timeout
    esp_err_t result = twai_receive(&rx_frame, pdMS_TO_TICKS(timeout_ms));

    if (result == ESP_OK) {
        // Copy dữ liệu từ frame vào structure
        msg->id = rx_frame.identifier;
        msg->is_extended_id = rx_frame.extd;
        msg->data_length = rx_frame.data_length_code;  // DLC (Data Length Code)

        // Copy dữ liệu
        for (int i = 0; i < msg->data_length && i < 8; i++) {
            msg->data[i] = rx_frame.data[i];
        }

        return ESP_OK;
    } else if (result == ESP_ERR_TIMEOUT) {
        // Timeout - không có gói tin
        return ESP_ERR_TIMEOUT;
    } else {
        printf("✗ Lỗi nhận CAN: %d\n", result);
        return result;
    }
}

/**
 * Hàm nhận nhiều bytes từ CAN bus (ghép từ nhiều gói tin)
 * Tương tự như uart_read_bytes - blocking với timeout
 * 
 * @param id CAN ID cần lọc (chỉ nhận gói tin từ ID này)
 * @param buffer Buffer để lưu dữ liệu nhận
 * @param length Số bytes cần nhận
 * @param timeout_ms Timeout tổng (ms) - thời gian chờ tối đa cho toàn bộ quá trình
 * @return Số bytes đã nhận được, hoặc -1 nếu lỗi
 */
int can_read_bytes(uint32_t id, uint8_t *buffer, uint32_t length, uint32_t timeout_ms)
{
    if (buffer == NULL || length == 0) {
        printf("Lỗi: Buffer hoặc length không hợp lệ\n");
        return -1;
    }

    int total_received = 0;
    uint32_t start_time = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (total_received < length) {
        // Kiểm tra timeout tổng
        uint32_t elapsed = xTaskGetTickCount() - start_time;
        if (elapsed >= timeout_ticks) {
            buffer[total_received] = '\0';  // Kết thúc chuỗi
            return total_received;
        }

        // Tính timeout còn lại
        uint32_t remaining_timeout = timeout_ticks - elapsed;
        uint32_t frame_timeout_ms = pdTICKS_TO_MS(remaining_timeout);

        // Nhận một gói tin CAN
        can_message_t rx_msg = {0};
        esp_err_t result = can_receive_message(&rx_msg, frame_timeout_ms);

        if (result == ESP_OK) {
            // Kiểm tra ID có khớp không
            if (rx_msg.id == id) {
                // Tính số bytes cần copy
                int bytes_to_copy = (total_received + rx_msg.data_length <= length) 
                                     ? rx_msg.data_length 
                                     : (length - total_received);

                // Copy dữ liệu vào buffer
                memcpy(&buffer[total_received], rx_msg.data, bytes_to_copy);
                total_received += bytes_to_copy;
            }
        } else if (result == ESP_ERR_TIMEOUT) {
            buffer[total_received] = '\0';  // Kết thúc chuỗi
            return total_received;
        } else {
            return -1;
        }
    }

    buffer[total_received] = '\0';  // Kết thúc chuỗi
    return total_received;
}

void app_main(void)
{
    printf("CAN RX - Nhận gói tin CAN từ TJA1040\n");

    if (can_init() != ESP_OK) {
        printf("Lỗi khởi tạo CAN\n");
        return;
    }

    printf("Chờ nhận gói tin...\n\n");

    uint8_t rx_buffer[256];  // Buffer để lưu chuỗi hoàn chỉnh

    while (1) {
        int bytes_received = can_read_bytes(0x789, rx_buffer, 8, 5000);
        if (bytes_received > 0) {
            // In chuỗi hoàn chỉnh đã ghép
            printf(" Received( %d/76 bytes)\n", bytes_received);
            printf("Data received: %s\n", (char*)rx_buffer);
        } 
    }
}
