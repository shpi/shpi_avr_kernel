# shpi_2a_kernel
SHPI ATmega/ATtiny Kernel driver


in progress


## backlight interface

/sys/class/backlight/2-002a $

actual_brightness   (read only 0-31)

bl_power            (0 on, 4 off)

brightness          (rw 0-31)

max_brightness      (31)


## led interface

/sys/class/leds/ws2818-red-0/brightness (0-255)

/sys/class/leds/ws2818-grn-0/brightness (0-255)

/sys/class/leds/ws2818-blu-0/brightness (0-255) 

/sys/class/leds/ws2818-red-1/brightness

/sys/class/leds/ws2818-grn-1/brightness

/sys/class/leds/ws2818-blu-1/brightness


## hwmon interface

/sys/class/hwmon/hwmon*/

in1_input  in3_input  in4_label  in6_input  in8_input  name	   relay2
in0_input  in2_input  in4_input  in5_input  in7_input  in8_label  relay1  relay3


