#pragma once

#include "driver/gpio.h"

#define BOARD_SPK_DIN_IO        GPIO_NUM_4
#define BOARD_SPK_BCLK_IO       GPIO_NUM_5
#define BOARD_SPK_LRCLK_IO      GPIO_NUM_6

#define BOARD_MIC_CLK_IO        GPIO_NUM_7
#define BOARD_MIC_DOUT_IO       GPIO_NUM_15

#define BOARD_LCD_SCL_IO        GPIO_NUM_36
#define BOARD_LCD_SDA_IO        GPIO_NUM_37
#define BOARD_LCD_RST_IO        GPIO_NUM_40
#define BOARD_LCD_DC_IO         GPIO_NUM_39
#define BOARD_LCD_CS_IO         GPIO_NUM_38
#define BOARD_LCD_WIDTH         128
#define BOARD_LCD_HEIGHT        64

// Note: the joystick orientation had to be rotated 45 degrees.
// Adjust the orientation in software as needed.
#define BOARD_KEY_UP_IO         GPIO_NUM_9
#define BOARD_KEY_DOWN_IO       GPIO_NUM_11
#define BOARD_KEY_LEFT_IO       GPIO_NUM_12
#define BOARD_KEY_RIGHT_IO      GPIO_NUM_10
// Center is not connected.
// Can be connected with a jumper wire to a pin if needed.
#define BOARD_KEY_CENTER_IO     GPIO_NUM_13
#define BOARD_KEY_1_IO          GPIO_NUM_14
#define BOARD_KEY_2_IO          GPIO_NUM_21
#define BOARD_KEY_3_IO          GPIO_NUM_47
#define BOARD_KEY_4_IO          GPIO_NUM_48

#define BOARD_I2C_SDA_IO        GPIO_NUM_1
#define BOARD_I2C_SCL_IO        GPIO_NUM_2
