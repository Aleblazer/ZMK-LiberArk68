/*
 * FT6336 two-contact touchpad driver for the LiberArk68 dongle.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT liberark_ft6336_touchpad

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(ft6336_touchpad, CONFIG_INPUT_LOG_LEVEL);

#define FT6336_REG_TD_STATUS 0x02U
#define FT6336_REG_P1_XH 0x03U
#define FT6336_TOUCH_COUNT_MASK 0x0FU
#define FT6336_TOUCH_ID_INVALID 0x0FU
#define FT6336_TOUCH_ID_SHIFT 4U
#define FT6336_POSITION_HIGH_MASK 0x0FU
#define FT6336_POINT_BYTES 6U
#define FT6336_MAX_POINTS 2U

enum ft6336_gesture_mode {
    FT6336_GESTURE_IDLE,
    FT6336_GESTURE_POINTER,
    FT6336_GESTURE_SCROLL,
    FT6336_GESTURE_WAIT_RELEASE,
};

struct ft6336_touchpad_config {
    struct i2c_dt_spec bus;
    struct gpio_dt_spec int_gpio;
    struct gpio_dt_spec reset_gpio;
    uint16_t pointer_multiplier;
    uint16_t pointer_divisor;
    uint16_t scroll_divisor;
    uint16_t tap_time_ms;
    uint16_t tap_move_threshold;
    uint16_t scroll_start_threshold;
    uint16_t click_release_ms;
    bool invert_x;
};

struct ft6336_point {
    int16_t x;
    int16_t y;
};

struct ft6336_touchpad_data {
    const struct device *dev;
    struct k_work read_work;
    struct k_work_delayable click_release_work;
    struct gpio_callback int_gpio_cb;
    enum ft6336_gesture_mode mode;
    int16_t last_x;
    int16_t last_y;
    int16_t origin_x;
    int16_t origin_y;
    int16_t pointer_x_remainder;
    int16_t pointer_y_remainder;
    int16_t scroll_x_remainder;
    int16_t scroll_y_remainder;
    uint32_t gesture_start_ms;
    uint16_t pressed_button;
    bool tap_candidate;
    bool gesture_used_two_fingers;
    bool scroll_active;
    bool button_down;
};

static int32_t distance_abs(int32_t value) { return value < 0 ? -value : value; }

static int16_t scale_delta(int16_t delta, uint16_t multiplier, uint16_t divisor,
                           int16_t *remainder) {
    int32_t value = (int32_t)delta * multiplier + *remainder;
    int16_t scaled = value / divisor;

    *remainder = value - ((int32_t)scaled * divisor);
    return scaled;
}

static void report_xy(const struct device *dev, uint16_t x_code, int16_t x, uint16_t y_code,
                      int16_t y) {
    if (x != 0 && y != 0) {
        input_report_rel(dev, x_code, x, false, K_FOREVER);
        input_report_rel(dev, y_code, y, true, K_FOREVER);
    } else if (x != 0) {
        input_report_rel(dev, x_code, x, true, K_FOREVER);
    } else if (y != 0) {
        input_report_rel(dev, y_code, y, true, K_FOREVER);
    }
}

static void release_click_work_handler(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct ft6336_touchpad_data *data =
        CONTAINER_OF(delayable, struct ft6336_touchpad_data, click_release_work);

    if (data->button_down) {
        input_report_key(data->dev, data->pressed_button, 0, true, K_FOREVER);
        data->button_down = false;
    }
}

static void report_click(const struct device *dev, uint16_t button) {
    const struct ft6336_touchpad_config *config = dev->config;
    struct ft6336_touchpad_data *data = dev->data;

    if (data->button_down) {
        input_report_key(dev, data->pressed_button, 0, true, K_FOREVER);
        k_work_cancel_delayable(&data->click_release_work);
    }

    data->pressed_button = button;
    data->button_down = true;
    input_report_key(dev, button, 1, true, K_FOREVER);
    k_work_reschedule(&data->click_release_work, K_MSEC(config->click_release_ms));
}

static int read_points(const struct device *dev, struct ft6336_point *points) {
    const struct ft6336_touchpad_config *config = dev->config;
    uint8_t point_count;
    uint8_t raw[FT6336_POINT_BYTES * FT6336_MAX_POINTS];
    int ret;

    ret = i2c_reg_read_byte_dt(&config->bus, FT6336_REG_TD_STATUS, &point_count);
    if (ret < 0) {
        LOG_ERR("failed to read touch count: %d", ret);
        return ret;
    }

    point_count = MIN(point_count & FT6336_TOUCH_COUNT_MASK, FT6336_MAX_POINTS);
    if (point_count == 0) {
        return 0;
    }

    ret = i2c_burst_read_dt(&config->bus, FT6336_REG_P1_XH, raw, point_count * FT6336_POINT_BYTES);
    if (ret < 0) {
        LOG_ERR("failed to read touch coordinates: %d", ret);
        return ret;
    }

    uint8_t valid_points = 0;
    for (uint8_t i = 0; i < point_count; i++) {
        const uint8_t *point = &raw[i * FT6336_POINT_BYTES];
        uint8_t touch_id = point[2] >> FT6336_TOUCH_ID_SHIFT;

        if (touch_id == FT6336_TOUCH_ID_INVALID) {
            continue;
        }

        uint16_t raw_x = ((point[0] & FT6336_POSITION_HIGH_MASK) << 8U) | point[1];
        uint16_t raw_y = ((point[2] & FT6336_POSITION_HIGH_MASK) << 8U) | point[3];

        /* Match Zephyr's FT5336 landscape mapping: logical X=raw Y, Y=raw X. */
        points[valid_points].x = raw_y;
        points[valid_points].y = raw_x;
        valid_points++;
    }

    return valid_points;
}

static void reset_gesture(struct ft6336_touchpad_data *data) {
    data->mode = FT6336_GESTURE_IDLE;
    data->tap_candidate = false;
    data->gesture_used_two_fingers = false;
    data->scroll_active = false;
    data->pointer_x_remainder = 0;
    data->pointer_y_remainder = 0;
    data->scroll_x_remainder = 0;
    data->scroll_y_remainder = 0;
}

static void start_pointer(struct ft6336_touchpad_data *data, const struct ft6336_point *point) {
    data->mode = FT6336_GESTURE_POINTER;
    data->last_x = data->origin_x = point->x;
    data->last_y = data->origin_y = point->y;
    data->gesture_start_ms = k_uptime_get_32();
    data->tap_candidate = true;
    data->gesture_used_two_fingers = false;
    data->scroll_active = false;
    data->pointer_x_remainder = 0;
    data->pointer_y_remainder = 0;
}

static void start_scroll(struct ft6336_touchpad_data *data, const struct ft6336_point *points,
                         bool new_gesture) {
    int16_t center_x = (points[0].x + points[1].x) / 2;
    int16_t center_y = (points[0].y + points[1].y) / 2;

    data->mode = FT6336_GESTURE_SCROLL;
    data->last_x = data->origin_x = center_x;
    data->last_y = data->origin_y = center_y;
    if (new_gesture) {
        data->gesture_start_ms = k_uptime_get_32();
        data->tap_candidate = true;
    }
    data->gesture_used_two_fingers = true;
    data->scroll_active = false;
    data->scroll_x_remainder = 0;
    data->scroll_y_remainder = 0;
}

static void finish_gesture(const struct device *dev) {
    const struct ft6336_touchpad_config *config = dev->config;
    struct ft6336_touchpad_data *data = dev->data;
    uint32_t elapsed_ms = k_uptime_get_32() - data->gesture_start_ms;

    if (data->mode != FT6336_GESTURE_IDLE && data->tap_candidate &&
        elapsed_ms <= config->tap_time_ms) {
        report_click(dev, data->gesture_used_two_fingers ? INPUT_BTN_1 : INPUT_BTN_0);
    }

    reset_gesture(data);
}

static void process_pointer(const struct device *dev, const struct ft6336_point *point) {
    const struct ft6336_touchpad_config *config = dev->config;
    struct ft6336_touchpad_data *data = dev->data;
    int16_t delta_x = point->x - data->last_x;
    int16_t delta_y = point->y - data->last_y;

    data->last_x = point->x;
    data->last_y = point->y;

    if (distance_abs(point->x - data->origin_x) > config->tap_move_threshold ||
        distance_abs(point->y - data->origin_y) > config->tap_move_threshold) {
        data->tap_candidate = false;
    }

    if (config->invert_x) {
        delta_x = -delta_x;
    }

    int16_t report_x = scale_delta(delta_x, config->pointer_multiplier, config->pointer_divisor,
                                   &data->pointer_x_remainder);
    int16_t report_y = scale_delta(delta_y, config->pointer_multiplier, config->pointer_divisor,
                                   &data->pointer_y_remainder);
    report_xy(dev, INPUT_REL_X, report_x, INPUT_REL_Y, report_y);
}

static void process_scroll(const struct device *dev, const struct ft6336_point *points) {
    const struct ft6336_touchpad_config *config = dev->config;
    struct ft6336_touchpad_data *data = dev->data;
    int16_t center_x = (points[0].x + points[1].x) / 2;
    int16_t center_y = (points[0].y + points[1].y) / 2;
    int16_t delta_x = center_x - data->last_x;
    int16_t delta_y = center_y - data->last_y;

    data->last_x = center_x;
    data->last_y = center_y;

    if (!data->scroll_active &&
        (distance_abs(center_x - data->origin_x) > config->scroll_start_threshold ||
         distance_abs(center_y - data->origin_y) > config->scroll_start_threshold)) {
        data->scroll_active = true;
        data->tap_candidate = false;
    }

    if (!data->scroll_active) {
        return;
    }

    /* Use the calibrated physical X direction and conventional wheel direction. */
    if (config->invert_x) {
        delta_x = -delta_x;
    }

    int16_t report_h = scale_delta(delta_x, 1, config->scroll_divisor, &data->scroll_x_remainder);
    int16_t report_v = scale_delta(-delta_y, 1, config->scroll_divisor, &data->scroll_y_remainder);
    report_xy(dev, INPUT_REL_HWHEEL, report_h, INPUT_REL_WHEEL, report_v);
}

static int ft6336_touchpad_process(const struct device *dev) {
    struct ft6336_touchpad_data *data = dev->data;
    struct ft6336_point points[FT6336_MAX_POINTS];
    int point_count = read_points(dev, points);

    if (point_count < 0) {
        return point_count;
    }

    if (point_count == 0) {
        finish_gesture(dev);
        return 0;
    }

    switch (data->mode) {
    case FT6336_GESTURE_IDLE:
        if (point_count == 1) {
            start_pointer(data, &points[0]);
        } else {
            start_scroll(data, points, true);
        }
        break;

    case FT6336_GESTURE_POINTER:
        if (point_count == 1) {
            process_pointer(dev, &points[0]);
        } else {
            start_scroll(data, points, false);
        }
        break;

    case FT6336_GESTURE_SCROLL:
        if (point_count == 2) {
            process_scroll(dev, points);
        } else {
            /* Do not turn the remaining finger into a pointer jump. */
            data->mode = FT6336_GESTURE_WAIT_RELEASE;
        }
        break;

    case FT6336_GESTURE_WAIT_RELEASE:
        break;
    }

    return 0;
}

static void read_work_handler(struct k_work *work) {
    struct ft6336_touchpad_data *data = CONTAINER_OF(work, struct ft6336_touchpad_data, read_work);

    ft6336_touchpad_process(data->dev);
}

static void int_gpio_handler(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    struct ft6336_touchpad_data *data = CONTAINER_OF(cb, struct ft6336_touchpad_data, int_gpio_cb);

    ARG_UNUSED(port);
    ARG_UNUSED(pins);
    k_work_submit(&data->read_work);
}

static int ft6336_touchpad_init(const struct device *dev) {
    const struct ft6336_touchpad_config *config = dev->config;
    struct ft6336_touchpad_data *data = dev->data;
    int ret;

    if (!i2c_is_ready_dt(&config->bus)) {
        LOG_ERR("I2C controller is not ready");
        return -ENODEV;
    }
    if (!gpio_is_ready_dt(&config->int_gpio) || !gpio_is_ready_dt(&config->reset_gpio)) {
        LOG_ERR("touch GPIO controller is not ready");
        return -ENODEV;
    }
    if (config->pointer_divisor == 0 || config->scroll_divisor == 0) {
        LOG_ERR("touchpad divisors must be nonzero");
        return -EINVAL;
    }

    data->dev = dev;
    data->pressed_button = INPUT_BTN_0;
    reset_gesture(data);
    k_work_init(&data->read_work, read_work_handler);
    k_work_init_delayable(&data->click_release_work, release_click_work_handler);

    ret = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        return ret;
    }
    k_sleep(K_MSEC(5));
    ret = gpio_pin_set_dt(&config->reset_gpio, 0);
    if (ret < 0) {
        return ret;
    }

    ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
    if (ret < 0) {
        return ret;
    }
    ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        return ret;
    }

    gpio_init_callback(&data->int_gpio_cb, int_gpio_handler, BIT(config->int_gpio.pin));
    ret = gpio_add_callback(config->int_gpio.port, &data->int_gpio_cb);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

#define FT6336_TOUCHPAD_INIT(inst)                                                                 \
    static const struct ft6336_touchpad_config ft6336_touchpad_config_##inst = {                   \
        .bus = I2C_DT_SPEC_INST_GET(inst),                                                         \
        .int_gpio = GPIO_DT_SPEC_INST_GET(inst, int_gpios),                                        \
        .reset_gpio = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),                                    \
        .pointer_multiplier = DT_INST_PROP(inst, pointer_multiplier),                              \
        .pointer_divisor = DT_INST_PROP(inst, pointer_divisor),                                    \
        .scroll_divisor = DT_INST_PROP(inst, scroll_divisor),                                      \
        .tap_time_ms = DT_INST_PROP(inst, tap_time_ms),                                            \
        .tap_move_threshold = DT_INST_PROP(inst, tap_move_threshold),                              \
        .scroll_start_threshold = DT_INST_PROP(inst, scroll_start_threshold),                      \
        .click_release_ms = DT_INST_PROP(inst, click_release_ms),                                  \
        .invert_x = DT_INST_PROP(inst, invert_x),                                                  \
    };                                                                                             \
    static struct ft6336_touchpad_data ft6336_touchpad_data_##inst;                                \
    DEVICE_DT_INST_DEFINE(inst, ft6336_touchpad_init, NULL, &ft6336_touchpad_data_##inst,          \
                          &ft6336_touchpad_config_##inst, POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(FT6336_TOUCHPAD_INIT)
