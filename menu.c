/*
    menu - handle showing to display, menus for basic channel settings
    Copyright (C) 2011 Pavel Semerad

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/



#include <string.h>
#include "menu.h"
#include "config.h"
#include "calc.h"
#include "timer.h"
#include "ppm.h"
#include "lcd.h"
#include "buzzer.h"
#include "input.h"






// variables to be used in CALC task
u8  menu_force_value_channel;	// set PPM value for this channel
s16 menu_force_value;		//   to this value (-500..500)



//
u8  menu_lap_count;		// lap count



// flags for wakeup after each ADC measure
_Bool menu_wants_adc;
// don't stop main loop and check keys
u8 menu_check_keys;







// show model number, extra function to handle more than 10 models
static void show_model_number(u8 model) {
    lcd_7seg((u8)(model % 10));
    lcd_segment(LS_SYM_RIGHT, (u8)((u8)(model / 10) & 1));
    lcd_segment(LS_SYM_LEFT, (u8)((u8)(model / 20) & 1));
}


// show main screen (model number and name/battery/...)
static void main_screen(u8 item) {
    lcd_segment(LS_SYM_MODELNO, LS_ON);
    lcd_segment(LS_SYM_CHANNEL, LS_OFF);
    lcd_segment(LS_SYM_PERCENT, LS_OFF);
    show_model_number(cg.model);

    menu_wants_adc = 0;

    // chars is item dependent
    if (item == MS_NAME) {
	// model name
	lcd_segment(LS_SYM_DOT, LS_OFF);
	lcd_segment(LS_SYM_VOLTS, LS_OFF);
	lcd_chars(cm.name);
    }
    else if (item == MS_BATTERY) {
	static u16 bat_val;
	static u16 bat_time;

	// battery voltage
	lcd_segment(LS_SYM_DOT, LS_ON);
	lcd_segment(LS_SYM_VOLTS, LS_ON);
	// calculate voltage from current raw value and calib value
	if (time_sec >= bat_time) {
	    bat_time = time_sec + 2;
	    bat_val = (u16)(((u32)adc_battery * 100 + 300) / cg.battery_calib);
	}
	lcd_char_num3(bat_val);
	menu_wants_adc = 1;
    }
    else if (item == MS_LAP_COUNT) {
	lcd_segment(LS_SYM_DOT, LS_OFF);
	lcd_segment(LS_SYM_VOLTS, LS_OFF);
	lcd_segment(LS_SYM_PERCENT, LS_ON);
	lcd_char_num3(menu_lap_count);
    }
    lcd_update();
}






// common menu processing, selecting channel and then changing values
static _Bool menu_adc_direction;
static void menu_set_adc_direction(u8 channel) {
    u16 adc, calm;

    // for channel 2 use throttle, for others steering
    if (channel == 2) {
	adc = adc_throttle_ovs;
	calm = cg.calib_throttle_mid;
    }
    else {
	adc = adc_steering_ovs;
	calm = cg.calib_steering_mid;
    }

    // if over threshold to one side, set menu_adc_direction
    if (adc < ((calm - 40) << ADC_OVS_SHIFT)) {
	if (menu_adc_direction) {
	    menu_adc_direction = 0;
	    lcd_segment(LS_SYM_LEFT, LS_ON);
	    lcd_segment(LS_SYM_RIGHT, LS_OFF);
	}
    }
    else if (adc > ((calm + 40) << ADC_OVS_SHIFT)) {
	if (!menu_adc_direction) {
	    menu_adc_direction = 1;
	    lcd_segment(LS_SYM_LEFT, LS_OFF);
	    lcd_segment(LS_SYM_RIGHT, LS_ON);
	}
    }
    else if (channel > 2 && btn(BTN_CH3)) {
	// use CH3 button to toggle also
	menu_adc_direction ^= 1;
	lcd_segment(LS_SYM_LEFT, (u8)(menu_adc_direction ? LS_OFF : LS_ON));
	lcd_segment(LS_SYM_RIGHT, (u8)(menu_adc_direction ? LS_ON : LS_OFF));
    }

    // if this channel is using forced values, set it to left/right
    if (menu_force_value_channel)
	menu_force_value = menu_adc_direction ? PPM(500) : PPM(-500);
}

static _Bool menu_set_adc(u8 channel, u8 use_adc, u8 force_values) {
    // check if force servos to positions (left/center/right)
    if ((u8)(force_values & (1 << (channel - 1)))) {
	menu_force_value_channel = channel;
	menu_force_value = 0;	// to center by default
    }
    else menu_force_value_channel = 0;

    // check use of ADC
    if ((u8)(use_adc & (1 << (channel - 1)))) {
	// use ADC
	if (menu_adc_direction) {
	    lcd_segment(LS_SYM_LEFT, LS_OFF);
	    lcd_segment(LS_SYM_RIGHT, LS_ON);
	}
	else {
	    lcd_segment(LS_SYM_LEFT, LS_ON);
	    lcd_segment(LS_SYM_RIGHT, LS_OFF);
	}
	menu_wants_adc = 1;
	menu_set_adc_direction(channel);
	return 1;
    }
    else {
	// don't use ADC
	lcd_segment(LS_SYM_LEFT,    LS_OFF);
	lcd_segment(LS_SYM_RIGHT,   LS_OFF);
	menu_wants_adc = 0;
	return 0;
    }
}

static void menu_channel(u8 end_channel, u8 use_adc, u8 forced_values,
			 void (*subfunc)(u8, u8)) {
    u8 channel = 1;
    _Bool chan_val = 0;			// now in channel
    _Bool adc_active;
    u8 last_direction;

    // show CHANNEL
    lcd_segment(LS_SYM_MODELNO, LS_OFF);
    lcd_segment(LS_SYM_CHANNEL, LS_ON);

    // show channel number and possible direction
    menu_adc_direction = 0;
    lcd_7seg(channel);
    lcd_set_blink(L7SEG, LB_SPC);
    adc_active = menu_set_adc(channel, use_adc, forced_values);
    subfunc((u8)(channel - 1), 0);	// show current value
    lcd_update();

    while (1) {
	btnra();
	menu_stop();

	if (btn(BTN_BACK | BTN_END) || btnl(BTN_ENTER))  break;

	last_direction = menu_adc_direction;
	if (adc_active)  menu_set_adc_direction(channel);

	if (btn(BTN_ROT_ALL)) {
	    if (chan_val) {
		// change value
		subfunc((u8)(channel - 1), 1);
		lcd_chars_blink(LB_SPC);
	    }
	    else {
		// change channel number
		if (btn(BTN_ROT_L)) {
		    if (!--channel)  channel = end_channel;
		}
		else {
		    if (++channel > end_channel)  channel = 1;
		}
		lcd_7seg(channel);
		lcd_set_blink(L7SEG, LB_SPC);
		adc_active = menu_set_adc(channel, use_adc, forced_values);
		subfunc((u8)(channel - 1), 0);
	    }
	    lcd_update();
	    last_direction = menu_adc_direction;  // was already showed
	}

	else if (btn(BTN_ENTER)) {
	    // switch channel/value
	    key_beep();
	    if (chan_val) {
		// switch to channel number
		lcd_set_blink(L7SEG, LB_SPC);
		lcd_chars_blink(LB_OFF);
		chan_val = 0;
	    }
	    else {
		// switch to value
		lcd_set_blink(L7SEG, LB_OFF);
		lcd_chars_blink(LB_SPC);
		chan_val = 1;
	    }
	}

	if (last_direction != menu_adc_direction) {
	    // show other dir value
	    subfunc((u8)(channel - 1), 0);
	    lcd_update();
	    if (chan_val) {
		lcd_chars_blink(LB_SPC);
	    }
	}
    }

    menu_wants_adc = 0;
    menu_force_value_channel = 0;
    key_beep();
    config_model_save();
}







// *************************** MODEL MENUS *******************************

// select model/save model as (to selected model position)
#define MIN(a, b)  (a < b ? a : b)
static void menu_model(u8 saveas) {
    u8 model = cg.model;

    if (saveas)  lcd_set_blink(LMENU, LB_SPC);
    lcd_set_blink(L7SEG, LB_SPC);

    while (1) {
	btnra();
	menu_stop();

	if (btn(BTN_BACK | BTN_END | BTN_ENTER))  break;
	if (btn(BTN_ROT_ALL)) {
	    model = (u8)menu_change_val((s16)model, 0,
					MIN(CONFIG_MODEL_MAX, 40) - 1,
					MODEL_FAST, 1);
	    show_model_number(model);
	    lcd_set_blink(L7SEG, LB_SPC);
	    lcd_chars(config_model_name(model));
	    lcd_update();
	}
    }

    key_beep();
    // if new model choosed, save it
    if (model != cg.model) {
	cg.model = model;
	config_global_save();
	if (saveas) {
	    // save to new model position
	    config_model_save();
	}
	else {
	    // load selected model
	    menu_load_model();
	}
    }
    if (saveas)  lcd_set_blink(LMENU, LB_OFF);
}


// change model name
static void menu_name(void) {
    u8 pos = LCHR1;
    u8 letter = cm.name[0];

    lcd_set_blink(LCHR1, LB_SPC);

    while (1) {
	btnra();
	menu_stop();

	if (btn(BTN_BACK | BTN_END) || btnl(BTN_ENTER))  break;
	if (btn(BTN_ENTER)) {
	    key_beep();
	    // to next char
	    lcd_set_blink(pos, LB_OFF);
	    if (++pos > LCHR3)  pos = LCHR1;
	    lcd_set_blink(pos, LB_SPC);
	    lcd_update();
	    letter = cm.name[pos];
	}
	else if (btn(BTN_ROT_ALL)) {
	    // change letter
	    if (btn(BTN_ROT_L)) {
		// lower
		if (letter == '0')      letter = 'Z';
		else if (letter == 'A')	letter = '9';
		else                    letter--;
	    }
	    else {
		// upper
		if (letter == '9')      letter = 'A';
		else if (letter == 'Z')	letter = '0';
		else                    letter++;
	    }
	    cm.name[pos] = letter;
	    lcd_char(pos, letter);
	    lcd_set_blink(pos, LB_SPC);
	    lcd_update();
	}
    }

    key_beep();
    config_model_save();
}


// reset model to default values
static void menu_reset_model(void) {
    config_model_set_default();
    config_model_save();
    apply_model_config();
}


// set reverse
void sf_reverse(u8 channel, u8 change) {
    u8 bit = (u8)(1 << channel);
    if (change)  cm.reverse ^= bit;
    if (cm.reverse & bit)  lcd_chars("REV");
    else                   lcd_chars("NOR");
}
@inline static void menu_reverse(void) {
    menu_channel(MAX_CHANNELS, 0, 0, sf_reverse);
}


// set endpoints
void sf_endpoint(u8 channel, u8 change) {
    u8 *addr = &cm.endpoint[channel][menu_adc_direction];
    if (change)  *addr = (u8)menu_change_val(*addr, 0, cg.endpoint_max,
                                             ENDPOINT_FAST, 0);
    lcd_char_num3(*addr);
}
static void menu_endpoint(void) {
    lcd_segment(LS_SYM_PERCENT, LS_ON);
    menu_channel(MAX_CHANNELS, 0xff, 0xfc, sf_endpoint);
    lcd_segment(LS_SYM_PERCENT, LS_OFF);
}


// set trims
static void sf_trim(u8 channel, u8 change) {
    s8 *addr = &cm.trim[channel];
    if (change)  *addr = (s8)menu_change_val(*addr, -TRIM_MAX, TRIM_MAX,
                                             TRIM_FAST, 0);
    if (channel == 0)  lcd_char_num2_lbl(*addr, "LNR");
    else               lcd_char_num2_lbl(*addr, "FNB");
}
@inline static void menu_trim(void) {
    menu_channel(2, 0, 0, sf_trim);
}


// set subtrims
static void sf_subtrim(u8 channel, u8 change) {
    s8 *addr = &cm.subtrim[channel];
    if (change)
	*addr = (s8)menu_change_val(*addr, -SUBTRIM_MAX, SUBTRIM_MAX,
	                            SUBTRIM_FAST, 0);
    lcd_char_num3(*addr);
}
static void menu_subtrim(void) {
    lcd_set_blink(LMENU, LB_SPC);
    menu_channel(MAX_CHANNELS, 0, 0xfc, sf_subtrim);
    lcd_set_blink(LMENU, LB_OFF);
}


// set dualrate
static void sf_dualrate(u8 channel, u8 change) {
    u8 *addr = &cm.dualrate[channel];
    if (channel == 1 && menu_adc_direction)  addr = &cm.dualrate[2];
    if (change)  *addr = (u8)menu_change_val(*addr, 0, 100, DUALRATE_FAST, 0);
    lcd_char_num3(*addr);
}
static void menu_dualrate(void) {
    lcd_segment(LS_SYM_PERCENT, LS_ON);
    menu_channel(2, 0x2, 0, sf_dualrate);
    lcd_segment(LS_SYM_PERCENT, LS_OFF);
}


// set servo speed
static void sf_speed(u8 channel, u8 change) {
    u8 *addr = &cm.speed[channel];
    u8 thfwdonly = (u8)(channel == 1 && menu_adc_direction ? 1 : 0);
    if (channel == 0 && menu_adc_direction)  addr = &cm.stspd_return;
    if (change) {
	if (thfwdonly)
	    // throttle forward only setting
	    cm.thspd_onlyfwd ^= 1;
	else *addr = (u8)menu_change_val(*addr, 1, 100, SPEED_FAST, 0);
    }
    if (thfwdonly)  lcd_chars(cm.thspd_onlyfwd ? "OFF" : "ON ");
    else	    lcd_char_num3(*addr);
}
static void menu_speed(void) {
    lcd_set_blink(LMENU, LB_SPC);
    lcd_segment(LS_SYM_PERCENT, LS_ON);
    menu_channel(MAX_CHANNELS, 0x3, 0, sf_speed);
    lcd_segment(LS_SYM_PERCENT, LS_OFF);
    lcd_set_blink(LMENU, LB_OFF);
}


// set expos
static void sf_expo(u8 channel, u8 change) {
    s8 *addr = &cm.expo[channel];
    if (channel == 1 && menu_adc_direction)  addr = &cm.expo[2];
    if (change)  *addr = (s8)menu_change_val(*addr, -EXPO_MAX, EXPO_MAX,
                                             EXPO_FAST, 0);
    lcd_char_num3(*addr);
}
static void menu_expo(void) {
    lcd_segment(LS_SYM_PERCENT, LS_ON);
    menu_channel(2, 0x2, 0, sf_expo);
    lcd_segment(LS_SYM_PERCENT, LS_OFF);
}


// set abs: OFF, SLO(6), NOR(4), FAS(3)
// pulses between full brake and 1/2 brake and only when enought brake applied
static const u8 abs_labels[][4] = {
    "OFF", "SLO", "NOR", "FAS"
};
#define ABS_LABEL_SIZE  (sizeof(abs_labels) / 4)
static void menu_abs(void) {
    lcd_segment(LS_SYM_MODELNO, LS_OFF);
    lcd_segment(LS_SYM_LEFT, LS_OFF);
    lcd_segment(LS_SYM_RIGHT, LS_OFF);
    lcd_segment(LS_SYM_CHANNEL, LS_ON);

    lcd_7seg(2);
    lcd_chars(abs_labels[cm.abs_type]);
    lcd_chars_blink(LB_SPC);
    lcd_update();

    while (1) {
	btnra();
	menu_stop();

	if (btn(BTN_BACK | BTN_END | BTN_ENTER))  break;

	if (btn(BTN_ROT_ALL)) {
	    cm.abs_type = (u8)menu_change_val(cm.abs_type, 0, ABS_LABEL_SIZE-1,
	                                      1, 1);
	    lcd_chars(abs_labels[cm.abs_type]);
	    lcd_chars_blink(LB_SPC);
	    lcd_update();
	}
    }

    key_beep();
    config_model_save();
}







// choose from menu items
static void select_menu(void) {
    u8 menu = LM_MODEL;
    lcd_menu(menu);
    main_screen(MS_NAME);	// show model number and name

    while (1) {
	btnra();
	menu_stop();

	// Back/End key to end this menu
	if (btn(BTN_BACK | BTN_END))  break;

	// Enter key - goto submenu
	if (btn(BTN_ENTER)) {
	    key_beep();
	    if (menu == LM_MODEL) {
		if (btnl(BTN_ENTER))	menu_model(1);
		else {
					menu_model(0);
					break;
		}
	    }
	    else if (menu == LM_NAME) {
		if (btnl(BTN_ENTER))	menu_reset_model();
		else			menu_name();
	    }
	    else if (menu == LM_REV) {
		if (btnl(BTN_ENTER))	menu_key_mapping();
		else			menu_reverse();
	    }
	    else if (menu == LM_EPO) {
		if (btnl(BTN_ENTER))	menu_mix();
		else			menu_endpoint();
	    }
	    else if (menu == LM_TRIM) {
		if (btnl(BTN_ENTER))	menu_subtrim();
		else			menu_trim();
	    }
	    else if (menu == LM_DR) {
		if (btnl(BTN_ENTER))	menu_speed();
		else			menu_dualrate();
	    }
	    else if (menu == LM_EXP)	menu_expo();
	    else {
		if (btnl(BTN_ENTER))	break;
		else			menu_abs();
	    }
	    main_screen(MS_NAME);	// show model number and name
	    // exit when BACK
	    if (btn(BTN_BACK))  break;
	}

	// rotate keys
	else if (btn(BTN_ROT_ALL)) {
	    if (btn(BTN_ROT_R)) {
		menu >>= 1;
		if (!menu)  menu = LM_MODEL;
	    }
	    else {
		menu <<= 1;
		if (!menu)  menu = LM_ABS;
	    }
	    lcd_menu(menu);
	    lcd_update();
	}
    }

    key_beep();
    lcd_menu(0);
}








// ****************** MAIN LOOP and init *********************************

// main menu loop, shows main screen and handle keys
u8 menu_main_screen;
static void menu_loop(void) {
    menu_main_screen = MS_NAME;

    lcd_clear();

    while (1) {
	if (!menu_check_keys) {
	    main_screen(menu_main_screen);
	    btnra();
	    menu_stop();
	}
	else  menu_check_keys = 0;

	// don't wanted in submenus, will be set back in main_screen()
	menu_wants_adc = 0;

	// Enter long key - global/calibrate/key-test
	if (btnl(BTN_ENTER)) {
	    if (adc_steering_ovs > (CALIB_ST_MID_HIGH << ADC_OVS_SHIFT))
		menu_calibrate(0);
	    else if (adc_steering_ovs < (CALIB_ST_LOW_MID << ADC_OVS_SHIFT))
		menu_key_test();
	    else menu_global_setup();
	}

	// Enter key - menu
	else if (btn(BTN_ENTER)) {
	    key_beep();
	    select_menu();
	    btnra();
	}

	// electronic trims
	else if (menu_electronic_trims())
	    menu_check_keys = 1;

	// buttons (CH3, Back, End)
	else if (menu_buttons())
	    menu_check_keys = 1;

	// rotate encoder - change model name/battery/...
	else if (btn(BTN_ROT_ALL)) {
	    if (btn(BTN_ROT_L)) {
		if (menu_main_screen)  menu_main_screen--;
		else		       menu_main_screen = MS_MAX - 1;
	    }
	    else {
		if (++menu_main_screen >= MS_MAX)  menu_main_screen = 0;
	    }
	}
    }
}



// read config from eeprom, initialize and call menu loop
void menu_init(void) {
    // variables
    menu_key_mapping_prepare();

    // wait for some time to read ADC/buttons several times and get
    //   stable values
    while (time_sec == 0 && time_5ms < 10)  pause();

    // read global config from eeprom, if calibrate values was set to defaults,
    //   call calibrate
    if (config_global_read()) {
	menu_calibrate(1);
	btnra();
	reset_inactivity_timer();
    }
    else {
	apply_global_config();
	reset_inactivity_timer();
	if (cg.poweron_beep) {
	    u16 steering = ADC_OVS(steering);
	    u16 throttle = ADC_OVS(throttle);
	    // if actual steering/throttle value is not in dead zone, beep 3 times
	    if (steering < (cg.calib_steering_mid - cg.steering_dead_zone) ||
		steering > (cg.calib_steering_mid + cg.steering_dead_zone) ||
		throttle < (cg.calib_throttle_mid - cg.throttle_dead_zone) ||
		throttle > (cg.calib_throttle_mid + cg.throttle_dead_zone))
		    buzzer_on(30, 30, 3);
	    // else beep 1 times
	    else beep(30);
	}
    }

    // read model config from eeprom
    menu_load_model();

    // and main loop
    menu_loop();
}

