// SPDX-License-Identifier: GPL-2.0-or-later
/* SHPI atmega / attiny kernel driver
 *
 * Copyright (C) 2020 Lutz Harder, SHPI GmbH
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/leds.h>
#include <linux/of.h>



/* I2C read 1byte commands */

#define SHPI_READ_RELAY1	0x0D
#define SHPI_READ_RELAY2        0x0E
#define SHPI_READ_RELAY3        0x0F
#define SHPI_READ_D13	        0x10	// used for HW RPI reset
#define SHPI_READ_HWB	        0x11	// used for GAS heater
#define SHPI_READ_BUZ	        0x12
#define SHPI_READ_VENT_PWM      0x13
#define SHPI_READ_LED_R		0x14	// read RED value from LED on LED_POS
#define SHPI_READ_LED_G      	0x15
#define SHPI_READ_LED_B	        0x16
#define SHPI_READ_DISP_C 	0x18
#define SHPI_READ_DISP_AP       0x19
#define SHPI_READ_WATCHDOG      0x20
#define SHPI_READ_LED_POS	0x21
#define SHPI_READ_FW_VERS	0x7F
#define SHPI_READ_CRC           0x7E
#define SHPI_READ_BACKLIGHT	0x07


/* I2C read 2byte commands */
#define SHPI_READ_A0	        0x00
#define SHPI_READ_A1            0x01
#define SHPI_READ_A2            0x02
#define SHPI_READ_A3            0x03
#define SHPI_READ_A4            0x04
#define SHPI_READ_A5            0x05 // prepared for NTC10k
#define SHPI_READ_A7            0x06 // only useful for DC supply
#define SHPI_READ_VENT_RPM      0x08
#define SHPI_READ_VCC           0x09
#define SHPI_READ_TEMP          0x0A
#define SHPI_READ_RAM	        0x0B
#define SHPI_READ_A7_AVG	0x17 // used for calculate AC current


/* I2C write 1byte commands */

#define SHPI_WRITE_BACKLIGHT	0x87
#define SHPI_WRITE_RELAY1	0x8D
#define SHPI_WRITE_RELAY2	0x8E
#define SHPI_WRITE_RELAY3	0x8F
#define SHPI_WRITE_D13		0x90
#define SHPI_WRITE_HWB		0x91
#define SHPI_WRITE_BUZ		0x92
#define SHPI_WRITE_VENT_PWM     0x93
#define SHPI_WRITE_LED_R        0x94    // write RED value from LED on LED_POS
#define SHPI_WRITE_LED_G        0x95
#define SHPI_WRITE_LED_B        0x96

#define SHPI_WRITE_DISP_C       0x98
#define SHPI_WRITE_DISP_AP      0x99
#define SHPI_WRITE_WATCHDOG     0xA0
#define SHPI_WRITE_LED_POS      0xA1
#define SHPI_WRITE_CRC		0xFE


static uint8_t CRC8_LOOKUP[] = {
    0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15, 0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
    0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65, 0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d,
    0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5, 0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
    0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85, 0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd,
    0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2, 0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea,
    0xb7, 0xb0, 0xb9, 0xbe, 0xab, 0xac, 0xa5, 0xa2, 0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
    0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32, 0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
    0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42, 0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a,
    0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c, 0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
    0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec, 0xc1, 0xc6, 0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4,
    0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c, 0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44,
    0x19, 0x1e, 0x17, 0x10, 0x05, 0x02, 0x0b, 0x0c, 0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
    0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b, 0x76, 0x71, 0x78, 0x7f, 0x6a, 0x6d, 0x64, 0x63,
    0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b, 0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
    0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb, 0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8d, 0x84, 0x83,
    0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb, 0xe6, 0xe1, 0xe8, 0xef, 0xfa, 0xfd, 0xf4, 0xf3,
};



struct shpi_platform_data {
	struct device *fbdev;
	unsigned int def_value;
};



struct shpi_led {
        struct led_classdev ldev;
        int     id;
        char    name[sizeof("ws2812-red-00")]; //red grn bl
        struct shpi *shpi;
};



struct shpi {
	struct i2c_client *client;
	struct mutex lock;
	unsigned long last_update;
	uint16_t a4;
	uint16_t vcc;
	char valid;
	struct shpi_platform_data *pdata;
	struct backlight_device *bl;
	uint8_t last_brightness;
	uint8_t blpower;
	int	num_leds;
	struct shpi_led leds[];
};




static inline uint8_t crc8(uint8_t byte, uint8_t init)

{

	return CRC8_LOOKUP[init ^ byte];
}


static uint8_t shpi_read_one_byte(struct i2c_client *client, uint8_t adress, uint8_t *buffer)
{
	int ret = 0;
	uint8_t crc = 0;
	unsigned char rbuf[2] = {0,};

	crc = crc8(adress,crc);

	ret = i2c_master_send(client, &adress, 1);

	if (ret <= 0)
		return -EIO;

        ret = i2c_master_recv(client, &rbuf[0], 2);
        if (ret <= 0)
                return -EIO;

	crc =	 crc8(rbuf[0], crc);

	if (crc == rbuf[1]) {

		*buffer = rbuf[0];

	} else return -ECOMM; /*communication error, crc error */

	return ret;
}



static uint8_t shpi_read_two_bytes(struct i2c_client *client, uint8_t adress, uint16_t *buffer)
{
        int ret = 0;
        uint8_t crc = 0;
        unsigned char rbuf[3] = {0,};
        crc = crc8(adress,crc);
        ret = i2c_master_send(client, &adress, 1);
        if (ret <= 0)
                return -EIO;

        ret = i2c_master_recv(client, &rbuf[0], 3);

        if (ret <= 0)
                return -EIO;

        crc =    crc8(rbuf[0], crc);
        crc =    crc8(rbuf[1], crc);

        if (crc != rbuf[2])
		 return -ECOMM; /*communication error, crc error */

	*buffer = rbuf[0] | (rbuf[1] << 8);


        return ret;
}




static uint8_t shpi_write_one_byte(struct i2c_client *client, uint8_t adress, uint8_t byte)
{

	int ret = 0;
	uint8_t crc = 0;

        crc = crc8(adress, 0);
        crc = crc8(byte, crc);

	unsigned char wbuf[3] = {adress, byte, crc};
	unsigned char rbuf[1];



	ret = i2c_master_send(client, wbuf, 3);

        if (ret <= 0)
                return -EIO;

	ret = i2c_master_recv(client, &rbuf[0], 1);

        if (ret <= 0)
                return -EIO;


	if (rbuf[0] != crc)

		return -ECOMM;


	return ret;
}


static int shpi_set_brightness_blocking(struct led_classdev *ldev,
                                      enum led_brightness brightness)
{
        struct shpi_led *led = container_of(ldev, struct shpi_led, ldev);
	struct i2c_client *client = led->shpi->client;
	struct shpi	*shpi 	= led->shpi;
        int ret;
	int id;

	id = led->id;
	mutex_lock(&shpi->lock);
	ret = shpi_write_one_byte(client, SHPI_WRITE_LED_POS,  (id/3));
	if (id % 3 == 0) ret = shpi_write_one_byte(client, SHPI_WRITE_LED_R, (char)ldev->brightness );
	if (id % 3 == 1) ret = shpi_write_one_byte(client, SHPI_WRITE_LED_G, (char)ldev->brightness );
	if (id % 3 == 2) ret = shpi_write_one_byte(client, SHPI_WRITE_LED_B, (char)ldev->brightness );
	mutex_unlock(&shpi->lock);

	if (ret <0)
		return -EIO;

        return ret;
}

static int shpi_backlight_set_value(struct backlight_device *bl)
{
        struct shpi *shpi = bl_get_data(bl);
	struct i2c_client *client = shpi->client;
	int ret;
	mutex_lock(&shpi->lock);
	ret = shpi_write_one_byte(client, SHPI_WRITE_BACKLIGHT, bl->props.brightness);
	mutex_unlock(&shpi->lock);

	if (ret < 0)
		return -EIO;

	if (bl->props.brightness > 0)
		shpi->last_brightness = bl->props.brightness;

	return bl->props.brightness;
}


static int shpi_backlight_update_status(struct backlight_device *bl)
{
        struct shpi *shpi = bl_get_data(bl);
        struct i2c_client *client = shpi->client;

        if (bl->props.power != FB_BLANK_UNBLANK ||
            bl->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
        {
                        bl->props.brightness = 0;
                        shpi->blpower = 1;
			shpi_backlight_set_value(bl);
			mutex_lock(&shpi->lock);
			shpi_write_one_byte(client, SHPI_WRITE_DISP_C, 0x00);
			mutex_unlock(&shpi->lock);
                        return 0;
        }

        else
	{
                if (shpi->blpower == 1)
                        {
			mutex_lock(&shpi->lock);
			shpi_write_one_byte(client, SHPI_WRITE_DISP_C, 0xFF);
                        shpi->blpower = bl->props.power;
                        bl->props.brightness = shpi->last_brightness;
	                mutex_unlock(&shpi->lock);
			shpi_backlight_set_value(bl);
                        return 0;
                        }
                else
                {
                return shpi_backlight_set_value(bl);
                }
        }
}


static int shpi_backlight_check_fb(struct backlight_device *bl,
				       struct fb_info *info)
{
	struct shpi *shpi = bl_get_data(bl);

	return shpi->pdata->fbdev == NULL || shpi->pdata->fbdev == info->dev;
}


static const struct backlight_ops shpi_backlight_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= shpi_backlight_update_status,
	.check_fb	= shpi_backlight_check_fb,
};



static ssize_t shpi_relay_show(struct device *dev,
				      struct device_attribute *da,
				      char *buf)
{

	struct shpi *shpi = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
 	struct i2c_client *client = shpi->client;
	uint8_t buffer;
	int ret;
	mutex_lock(&shpi->lock);
	ret = shpi_read_one_byte(client, attr->index, &buffer);
	mutex_unlock(&shpi->lock);
	if (ret < 0)
		return -EIO;

	if (buffer == 0xFF)
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");

}



static ssize_t shpi_a4_label_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "AIR QUALITY VOC\n");
}

static ssize_t shpi_vcc_label_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
        return sprintf(buf, "VCC reference\n");
}



static ssize_t shpi_read_sensor_show(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
        struct shpi *shpi = dev_get_drvdata(dev);
        struct i2c_client *client = shpi->client;
	int ret;
	uint16_t buffer;

	mutex_lock(&shpi->lock);
        ret = shpi_read_two_bytes(client, attr->index, &buffer);

	if (attr->index != SHPI_READ_VCC)	// already scaled with VCC
	{
		ret = shpi_read_two_bytes(client, SHPI_READ_VCC, &shpi->vcc);
		buffer = (shpi->vcc / 1024) * buffer;	// we scale analog inputs to VCC reference
	}
	mutex_unlock(&shpi->lock);

	if (ret < 0)
		return -EIO;

        return sprintf(buf, "%d\n", buffer);

}

static ssize_t shpi_relay_store(struct device *dev, struct device_attribute *da,
			  const char *buf, size_t count)
{
        struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
        struct shpi *shpi = dev_get_drvdata(dev);
        struct i2c_client *client = shpi->client;
        int ret;
	unsigned int readbit;

	ret = kstrtouint(buf, 10, &readbit);

	if (ret >=0) {


	if	(readbit == 1)
	{
  	mutex_lock(&shpi->lock);
	ret = shpi_write_one_byte(client, (attr->index + 0x80), 0xFF);
       	printk(KERN_INFO "SHPI: set %s : ON\n", attr->dev_attr.attr.name);
	mutex_unlock(&shpi->lock);
	}

	else if (readbit == 0)
	{
	 mutex_lock(&shpi->lock);
	ret = shpi_write_one_byte(client, (attr->index + 0x80), 0x00);
	printk(KERN_INFO "SHPI: set %s: OFF\n", attr->dev_attr.attr.name);
	mutex_unlock(&shpi->lock);
	}

	else
		return -EINVAL;

        if (ret < 0)
                return -EIO;


	} else

		return -EINVAL;


	return count;

}



static SENSOR_DEVICE_ATTR_RW(relay1, shpi_relay, SHPI_READ_RELAY1);
static SENSOR_DEVICE_ATTR_RW(relay2, shpi_relay, SHPI_READ_RELAY2);
static SENSOR_DEVICE_ATTR_RW(relay3, shpi_relay, SHPI_READ_RELAY3);
static DEVICE_ATTR(in4_label, 0444, shpi_a4_label_show, NULL);
static DEVICE_ATTR(in8_label, 0444, shpi_vcc_label_show, NULL);

static SENSOR_DEVICE_ATTR_RO(in0_input, shpi_read_sensor, SHPI_READ_A0);
static SENSOR_DEVICE_ATTR_RO(in1_input, shpi_read_sensor, SHPI_READ_A1);
static SENSOR_DEVICE_ATTR_RO(in2_input, shpi_read_sensor, SHPI_READ_A2);
static SENSOR_DEVICE_ATTR_RO(in3_input, shpi_read_sensor, SHPI_READ_A3);
static SENSOR_DEVICE_ATTR_RO(in4_input, shpi_read_sensor, SHPI_READ_A4);
static SENSOR_DEVICE_ATTR_RO(in5_input, shpi_read_sensor, SHPI_READ_A5);
static SENSOR_DEVICE_ATTR_RO(in6_input, shpi_read_sensor, SHPI_READ_A7);
static SENSOR_DEVICE_ATTR_RO(in7_input, shpi_read_sensor, SHPI_READ_A7_AVG);
static SENSOR_DEVICE_ATTR_RO(in8_input, shpi_read_sensor, SHPI_READ_VCC);




static struct attribute *shpi_attrs[] = {
	&sensor_dev_attr_relay1.dev_attr.attr,
 	&sensor_dev_attr_relay2.dev_attr.attr,
 	&sensor_dev_attr_relay3.dev_attr.attr,
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&dev_attr_in4_label.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
        &dev_attr_in8_label.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
        &sensor_dev_attr_in8_input.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(shpi);


static int shpi_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct shpi_platform_data *pdata = dev_get_platdata(&client->dev);
	struct backlight_device *backlight;
	struct backlight_properties props;
	struct device *dev = &client->dev;
	struct device	*hwmon_dev;
	struct shpi *shpi;
	uint8_t buf;
	int ret = 0;
	int num_leds;
	int i = 0;
	struct shpi_led *led;

	if (!i2c_check_functionality(client->adapter,
				      I2C_FUNC_I2C)) {
		dev_err(&client->dev,
			"adapter does not support I2C transactions\n");
		return -ENODEV;
	}

        //ret = device_property_read_u32(&client->dev, "num-leds", &num_leds);
        //if (ret < 0)
        num_leds = 2;


	//shpi = devm_kzalloc(dev, sizeof(*shpi), GFP_KERNEL);

	shpi = devm_kzalloc(dev, struct_size(shpi, leds, num_leds*3), GFP_KERNEL);


	if (!shpi)
		return -ENOMEM;

	shpi->client = client;



	for (i = 0; i < (num_leds*3); i++) {

		led		= &shpi->leds[i];
		led->id		= i;
		led->shpi	= shpi;

		if (i % 3 == 0) snprintf(led->name, sizeof(led->name), "ws2812-red-%d", (i/3));
		if (i % 3 == 1) snprintf(led->name, sizeof(led->name), "ws2812-grn-%d", (i/3));
		if (i % 3 == 2) snprintf(led->name, sizeof(led->name), "ws2812-blu-%d", (i/3));


		led->ldev.name = led->name;
		led->ldev.brightness = LED_OFF;
		led->ldev.max_brightness = 0xff;
		led->ldev.brightness_set_blocking = shpi_set_brightness_blocking;
		ret = led_classdev_register(&client->dev, &led->ldev);


	}





	shpi->pdata = pdata;
	shpi->num_leds = num_leds;
	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;

	props.max_brightness = 31;
	props.brightness = 31;
	shpi->blpower = 0;
	shpi->last_brightness = 31;

	backlight = devm_backlight_device_register(&client->dev,
					      dev_name(&client->dev),
					      &shpi->client->dev, shpi,
					      &shpi_backlight_ops, &props);

	if (IS_ERR(backlight)) {
		dev_err(&client->dev, "failed to register backlight\n");
		return PTR_ERR(backlight);
	}


	mutex_init(&shpi->lock);


	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   shpi, shpi_groups);


	mutex_lock(&shpi->lock);
	ret = shpi_read_one_byte(client, SHPI_READ_CRC, &buf);
	if (ret < 0)
		return -EIO;



	if (buf != 0xFF) {
		buf = 0xFF;
		ret = shpi_write_one_byte(client, SHPI_WRITE_CRC,buf);
		if  (ret < 0)
			return -EIO;
		}


	mutex_unlock(&shpi->lock);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

/* Device ID table */
static const struct i2c_device_id shpi_id[] = {
	{ "shpi", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, shpi_id);

static struct i2c_driver shpi_driver = {
	.driver.name = "shpi",
	.probe       = shpi_probe,
	.id_table    = shpi_id,
};

module_i2c_driver(shpi_driver);

MODULE_AUTHOR("Lutz Harder <lh@shpi.de>");
MODULE_DESCRIPTION("SHPI atmega / attiny kernel driver");
MODULE_LICENSE("GPL v2");
