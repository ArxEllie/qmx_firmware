#include QMK_KEYBOARD_H

// Restore status LEDs (83-87) after built-in RGB matrix effects
/*
extern void bat_led_show(void);
extern void sys_led_show(void);
extern void sys_sw_led_show(void);
extern void sleep_sw_led_show(void);
extern void rf_led_show(void);
*/

// Directly restore status LED colors based on current state (bypass timing logic)
/*
extern struct {
    uint8_t sys_sw_state;
    uint8_t rf_baterry;
} dev_info;

extern uint8_t colour_lib[][3];
#define SIDE_INDEX 83

void restore_status_leds(void) {
    // System switch indicator (MAC vs WIN)
    if (dev_info.sys_sw_state == 1) { // MAC
        rgb_matrix_set_color(SIDE_INDEX + 0, colour_lib[7][0], colour_lib[7][1], colour_lib[7][2]);
    } else { // WIN
        rgb_matrix_set_color(SIDE_INDEX + 0, colour_lib[5][0], colour_lib[5][1], colour_lib[5][2]);
    }

    // Battery indicator on remaining status LEDs
    uint8_t bat_r, bat_g, bat_b;
    if (dev_info.rf_baterry <= 20) {
        bat_r = colour_lib[3][0]; bat_g = colour_lib[3][1]; bat_b = colour_lib[3][2];
    } else if (dev_info.rf_baterry <= 50) {
        bat_r = colour_lib[2][0]; bat_g = colour_lib[2][1]; bat_b = colour_lib[2][2];
    } else if (dev_info.rf_baterry <= 80) {
        bat_r = colour_lib[1][0]; bat_g = colour_lib[1][1]; bat_b = colour_lib[1][2];
    } else {
        bat_r = colour_lib[0][0]; bat_g = colour_lib[0][1]; bat_b = colour_lib[0][2];
    }

    // Set battery indicator on status LEDs 1-4
    for (int i = 1; i < 5; i++) {
        rgb_matrix_set_color(SIDE_INDEX + i, bat_r, bat_g, bat_b);
    }
}

bool rgb_matrix_indicators_user(void) {
    // Restore status LED colors after built-in RGB matrix effects
    restore_status_leds();
    return false;
}
*/

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

// layer Mac
[0] = LAYOUT(
	KC_ESC, 	KC_SCRL,	KC_PAUSE,	MAC_TASK,	MAC_SEARCH,	MAC_VOICE,	MAC_DND,	KC_MPRV,	KC_MPLY,	KC_MNXT,	KC_MUTE,	KC_VOLD,	KC_VOLU,	MAC_PRTA,	KC_DEL,		KC_INS,
	KC_GRV,	    KC_1,   	KC_2,   	KC_3,  		KC_4,   	KC_5,   	KC_6,   	KC_7,   	KC_8,   	KC_9,  		KC_0,   	KC_MINS,	KC_EQL, 	KC_BSPC,	KC_HOME,
	KC_TAB, 	KC_Q,   	KC_W,   	KC_E,  		KC_R,   	KC_T,   	KC_Y,   	KC_U,   	KC_I,   	KC_O,  		KC_P,   	KC_LBRC,	KC_RBRC,	KC_BSLS,	KC_END,
	KC_CAPS,	KC_A,   	KC_S,   	KC_D,  		KC_F,   	KC_G,   	KC_H,   	KC_J,   	KC_K,   	KC_L,  		KC_SCLN,	KC_QUOT,	KC_NUHS,	KC_ENT, 	KC_PGUP,
	KC_LSFT,	KC_NUBS, 	KC_Z,   	KC_X,   	KC_C,  		KC_V,   	KC_B,   	KC_N,   	KC_M,   	KC_COMM,	KC_DOT,		KC_SLSH,	KC_RSFT,	KC_UP,		KC_PGDN,
	KC_LCTL,	KC_LOPT,	KC_LCMD,	KC_SPC, 	KC_RCMD,	MO(1),		KC_LEFT,	KC_DOWN,	KC_RIGHT),
// layer Mac Fn
[1] = LAYOUT(
	_______,	KC_F1,  	KC_F2,  	KC_F3, 		KC_F4,  	KC_F5,  	KC_F6,  	KC_F7,  	KC_F8,  	KC_F9, 		KC_F10, 	KC_F11, 	KC_F12, 	MAC_PRT,	_______,	_______,
	_______,	LNK_BLE1,	LNK_BLE2,	LNK_BLE3,	LNK_RF,		_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,
	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	DEV_RESET,	SLEEP_MODE,	BAT_SHOW,	_______,
	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,
	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	MO(4),		RM_SPDD,	RM_SPDU,	_______,	_______,	RM_VALU,		_______,
	_______,	_______,	_______,	_______,	_______,	MO(1),		RM_NEXT,	RM_VALD,	RM_HUEU),
// layer win
[2] = LAYOUT(
	KC_ESC, 	KC_F1,  	KC_F2,  	KC_F3, 		KC_F4,  	KC_F5,  	KC_F6,  	KC_F7,  	KC_F8,  	KC_F9, 		KC_F10, 	KC_F11, 	KC_F12, 	MAC_PRTA,	KC_DEL,		KC_INS,
	KC_GRV, 	KC_1,   	KC_2,   	KC_3,  		KC_4,   	KC_5,   	KC_6,   	KC_7,   	KC_8,   	KC_9,  		KC_0,   	KC_MINS,	KC_EQL, 	KC_BSPC,	KC_HOME,
	KC_TAB, 	KC_Q,   	KC_W,   	KC_E,  		KC_R,   	KC_T,   	KC_Y,   	KC_U,   	KC_I,   	KC_O,  		KC_P,   	KC_LBRC,	KC_RBRC,	KC_BSLS,	KC_END,
	KC_CAPS,	KC_A,   	KC_S,   	KC_D,  		KC_F,   	KC_G,   	KC_H,   	KC_J,   	KC_K,   	KC_L,  		KC_SCLN,	KC_QUOT,	KC_NUHS,	KC_ENT, 	KC_PGUP,
	KC_LSFT,	KC_NUBS,	KC_Z,   	KC_X,   	KC_C,  		KC_V,   	KC_B,   	KC_N,   	KC_M,   	KC_COMM,	KC_DOT,		KC_SLSH,	KC_RSFT,	KC_UP,		KC_PGDN,
	KC_LCTL,	KC_LWIN,	KC_LALT,	KC_SPC, 	KC_RALT,	MO(3),		KC_LEFT,	KC_DOWN,	KC_RIGHT),
// layer win Fn
[3] = LAYOUT(
	_______,	KC_BRID,	KC_BRIU,	KC_F3, 		KC_F4,  	KC_F5,  	KC_F6,  	KC_MPRV,	KC_MPLY,	KC_MNXT,	KC_MUTE,	KC_VOLD,	KC_VOLU,	KC_PSCR,	_______,	_______,
	_______,	LNK_BLE1,	LNK_BLE2,	LNK_BLE3,	LNK_RF,		_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,
	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	DEV_RESET,	SLEEP_MODE,	BAT_SHOW,	_______,
	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,
	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	MO(4),		RM_SPDD,	RM_SPDU,	_______,	_______,	RM_VALU,	_______,
	_______,	_______,	_______,	_______,	_______,	MO(3),		RM_NEXT,	RM_VALD,	RM_HUEU),
// layer 4
[4] = LAYOUT(
	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,
	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,
	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,
	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,
	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	_______,	SIDE_SPD,	SIDE_SPI,	SIDE_MOD_B,	_______,	SIDE_VAI,	_______,
	_______,	_______,	_______,	_______,	_______,	MO(4),		SIDE_MOD_A,	SIDE_VAD,	SIDE_HUI),
};

