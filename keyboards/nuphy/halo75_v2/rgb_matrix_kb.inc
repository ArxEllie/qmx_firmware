RGB_MATRIX_EFFECT(game_mode)
RGB_MATRIX_EFFECT(position_mode)
RGB_MATRIX_EFFECT(lesbian_pride)

#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

extern rgb_config_t rgb_matrix_config;

#define HALO75_KEY_LED_COUNT 83

static void set_halo75_key_color(uint8_t index, RGB color) {
    rgb_matrix_set_color(index, color.r, color.g, color.b);
}

static bool pride_flag(effect_params_t* params, const RGB* stripes, uint8_t stripe_count) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    for (uint8_t i = led_min; i < led_max; i++) {
        if (i >= HALO75_KEY_LED_COUNT) {
            continue;
        }

        // Use the physical LED Y coordinate so ANSI/ISO keep the same flag
        // shape without maintaining brittle per-layout LED lists.
        uint8_t stripe = (g_led_config.point[i].y * stripe_count) / 51;
        if (stripe >= stripe_count) {
            stripe = stripe_count - 1;
        }
        set_halo75_key_color(i, stripes[stripe]);
    }

    return rgb_matrix_check_finished_leds(led_max);
}

static bool game_mode(effect_params_t* params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    // These effects intentionally own only the main key LEDs; the side LED
    // driver has its own state machine and should not be blanked here.
    for (uint8_t i = led_min; i < led_max; i++) {
        if (i < 83) {
            rgb_matrix_set_color(i, 0x00, 0x00, 0x00);
        }
    }

    RGB rgb = hsv_to_rgb(rgb_matrix_config.hsv);
    rgb_matrix_set_color(0, rgb.r, rgb.g, rgb.b);       // ESC

    rgb_matrix_set_color(33, rgb.r, rgb.g, rgb.b);      // W
    rgb_matrix_set_color(47, rgb.r, rgb.g, rgb.b);      // D
    rgb_matrix_set_color(48, rgb.r, rgb.g, rgb.b);      // S
    rgb_matrix_set_color(49, rgb.r, rgb.g, rgb.b);      // A

    rgb_matrix_set_color(72, rgb.r, rgb.g, rgb.b);      // up
    rgb_matrix_set_color(80, rgb.r, rgb.g, rgb.b);      // right
    rgb_matrix_set_color(81, rgb.r, rgb.g, rgb.b);      // down
    rgb_matrix_set_color(82, rgb.r, rgb.g, rgb.b);      // left

    return rgb_matrix_check_finished_leds(led_max);
}

static bool position_mode(effect_params_t* params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    // Keep side LEDs outside this effect for the same reason as game_mode:
    // RGB Matrix paints the key field, while side.c owns the side channels.
    for (uint8_t i = led_min; i < led_max; i++) {
        if (i < 83) {
            rgb_matrix_set_color(i, 0x00, 0x00, 0x00);
        }
    }

    RGB rgb = hsv_to_rgb(rgb_matrix_config.hsv);

    rgb_matrix_set_color(50, rgb.r, rgb.g, rgb.b);      // F
    rgb_matrix_set_color(53, rgb.r, rgb.g, rgb.b);      // J
    rgb_matrix_set_color(72, rgb.r, rgb.g, rgb.b);      // up

    return rgb_matrix_check_finished_leds(led_max);
}

static bool lesbian_pride(effect_params_t* params) {
    static const RGB stripes[] = {
        {0xd6, 0x28, 0x00},
        {0xff, 0x9b, 0x55},
        {0xff, 0xff, 0xff},
        {0xd4, 0x62, 0xa6},
        {0xa5, 0x00, 0x62},
    };

    return pride_flag(params, stripes, ARRAY_SIZE(stripes));
}

#endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS
