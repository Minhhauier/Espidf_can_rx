#include "isotp.h"
#include "driver/twai.h"
#include "esp_timer.h"
#include "string.h"

/* 1. Hàm gửi một khung CAN (8 bytes) - Thư viện sẽ gọi hàm này */
int isotp_user_send_can(const uint32_t arbitration_id,
                        const uint8_t *data, const uint8_t size) {
    
    twai_message_t tx_msg = {
        .identifier = arbitration_id,
        .data_length_code = size,
        .extd = 0,
        .rtr = 0,
        .ss = 0,
        .self = 0,
        .dlc_non_comp = 0
    };
    
    memcpy(tx_msg.data, data, size);
    
    esp_err_t ret = twai_transmit(&tx_msg, pdMS_TO_TICKS(10));
    
    return (ret == ESP_OK) ? ISOTP_RET_OK : ISOTP_RET_ERROR;
}
/* 2. Hàm lấy thời gian hệ thống tính bằng miligiây */
uint32_t isotp_user_get_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* 3. Hàm debug (tùy chọn) */
void isotp_user_debug(const char* message, ...) {
    // printf hoặc log ở đây
}