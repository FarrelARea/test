#include "aw9523.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "qmsd_board.h"
#include "qmsd_board_pin.h"

#include "vkey_display.h"
#include "vkey_net.h"
#include "vkey_server.h"

#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 480

static const char *TAG = "vkey_main";

#if CONFIG_VKEY_TOUCH_X_MAX <= CONFIG_VKEY_TOUCH_X_MIN
#error "Touch X maximum must be greater than its minimum"
#endif
#if CONFIG_VKEY_TOUCH_Y_MAX <= CONFIG_VKEY_TOUCH_Y_MIN
#error "Touch Y maximum must be greater than its minimum"
#endif

static qmsd_err_t (*raw_touch_read)(touch_panel_points_t *point);

static uint16_t calibrate_touch_axis(uint16_t raw, uint16_t raw_min, uint16_t raw_max,
                                     uint16_t output_size)
{
    if (raw <= raw_min) {
        return 0;
    }
    if (raw >= raw_max) {
        return output_size - 1;
    }
    return (uint16_t)(((uint32_t)(raw - raw_min) * (output_size - 1)) / (raw_max - raw_min));
}

static qmsd_err_t calibrated_touch_read(touch_panel_points_t *point)
{
    qmsd_err_t error = raw_touch_read(point);
    if (error != QMSD_ERR_OK || point->event != TOUCH_EVT_PRESS || point->point_num == 0) {
        return error;
    }
    point->curx[0] = calibrate_touch_axis(point->curx[0], CONFIG_VKEY_TOUCH_X_MIN,
                                          CONFIG_VKEY_TOUCH_X_MAX, DISPLAY_WIDTH);
    point->cury[0] = calibrate_touch_axis(point->cury[0], CONFIG_VKEY_TOUCH_Y_MIN,
                                          CONFIG_VKEY_TOUCH_Y_MAX, DISPLAY_HEIGHT);
    return error;
}

static void touch_calibration_init(void)
{
    raw_touch_read = touch_ft5x06_driver.read_point_data;
    touch_ft5x06_driver.read_point_data = calibrated_touch_read;
    ESP_LOGI(TAG, "Touch calibration raw X=%d..%d Y=%d..%d -> %dx%d",
             CONFIG_VKEY_TOUCH_X_MIN, CONFIG_VKEY_TOUCH_X_MAX,
             CONFIG_VKEY_TOUCH_Y_MIN, CONFIG_VKEY_TOUCH_Y_MAX,
             DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

/* qmsd_gui_init() calls this hook before starting its LVGL update task. */
void gui_user_init(void)
{
    vkey_display_init();
}

static void board_power_init(void)
{
    aw9523_init(AW9523_I2C_SDA_PIN, AW9523_I2C_SCL_PIN);
    aw9523_set_addr(0x59);

    aw9523_io_set_gpio_or_led(BOARD_RESET_PIN >> 4, BOARD_RESET_PIN & 0x0f, AW9523_MODE_GPIO);
    aw9523_io_set_inout(BOARD_RESET_PIN >> 4, BOARD_RESET_PIN & 0x0f, AW9523_MODE_OUTPUT);
    aw9523_io_set_level(BOARD_RESET_PIN >> 4, BOARD_RESET_PIN & 0x0f, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    aw9523_io_set_level(BOARD_RESET_PIN >> 4, BOARD_RESET_PIN & 0x0f, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    const uint8_t backlights[] = {
        LCD_BL_0_PIN, LCD_BL_1_PIN, LCD_BL_2_PIN,
        LCD_BL_3_PIN, LCD_BL_4_PIN, LCD_BL_5_PIN,
    };
    aw9523_set_led_max_current(AW9523_37mA);
    for (size_t i = 0; i < sizeof(backlights); ++i) {
        aw9523_io_set_gpio_or_led(backlights[i] >> 4, backlights[i] & 0x0f, AW9523_MODE_LED);
        aw9523_led_set_duty(backlights[i] >> 4, backlights[i] & 0x0f, 128);
    }

    aw9523_io_set_gpio_or_led(PA_CTRL_PIN >> 4, PA_CTRL_PIN & 0x0f, AW9523_MODE_GPIO);
    aw9523_io_set_inout(PA_CTRL_PIN >> 4, PA_CTRL_PIN & 0x0f, AW9523_MODE_OUTPUT);
    aw9523_io_set_level(PA_CTRL_PIN >> 4, PA_CTRL_PIN & 0x0f, 0);
}

static void on_net_ready(vkey_net_mode_t mode, const char *ip)
{
    ESP_LOGI(TAG, "network ready: mode=%d ip=%s", (int)mode, ip);
    ESP_ERROR_CHECK(vkey_server_start());
    vkey_display_net_ready(mode, ip);
}

void app_main(void)
{
    board_power_init();
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_SHARED));

    qmsd_board_config_t board = QMSD_BOARD_DEFAULT_CONFIG;
    board.board_dir = BOARD_ROTATION_0;
    board.touch.en = 1;
    board.backlight.value = 0;
    board.gui.buffer_size = 320 * 32 * sizeof(uint16_t);
    board.gui.flags.double_fb = 0;
    board.gui.flags.fb_in_psram = 0;
    board.gui.refresh_task.en = 0;

    touch_calibration_init();
    qmsd_board_init(&board);

    vkey_display_start();
    vkey_net_start(on_net_ready);
}
