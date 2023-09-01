/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/lora.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <errno.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <app_version.h>

#include <zephyr/logging/log.h>
#include "FRST_Boot_Screen_1.h"

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#define MAX_DATA_LEN 10

char data[MAX_DATA_LEN] = {'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd'};

#define DELAY_TIME K_MSEC(50)

#define RGB(_r, _g, _b) { .r = (_r), .g = (_g), .b = (_b) }

static const struct led_rgb colors[] = {
	RGB(0x0f, 0x00, 0x00), /* red */
	RGB(0x00, 0x0f, 0x00), /* green */
	RGB(0x00, 0x00, 0x0f), /* blue */
	RGB(0xff, 0xff, 0xff), /* white */
	RGB(0xa0, 0xa0, 0xa0), /* gray */
};

struct led_rgb pixels[4];

void lora_receive_cb(const struct device *dev, uint8_t *data, uint16_t size, int16_t rssi, int8_t snr) {
	static int cnt;

	ARG_UNUSED(dev);
	ARG_UNUSED(size);

	LOG_INF("Received data: %s (RSSI:%ddBm, SNR:%ddBm)",
		data, rssi, snr);

	/* Stop receiving after 10 packets */
    /*
	if (++cnt == 10) {
		LOG_INF("Stopping packet receptions");
		lora_recv_async(dev, NULL);
	}
    */
}

int main(void) {
	size_t x;
	size_t y;
	size_t rect_w;
	size_t rect_h;
	size_t h_step;
	size_t scale;
	size_t grey_count;
	uint8_t *buf;
	struct display_buffer_descriptor buf_desc;
	size_t buf_size = 0;
	
	printk("Zephyr Example Application %s\n", APP_VERSION_STRING);

	const struct device *const lora_dev = DEVICE_DT_GET(DT_NODELABEL(lora0));
	const struct device *const display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	const struct device *const spibb_dev = DEVICE_DT_GET(DT_NODELABEL(spibb0));
    const struct device *const frontlight_dev = DEVICE_DT_GET(DT_NODELABEL(frontlight));
    struct lora_modem_config config;
	struct display_capabilities capabilities;
	int ret, len;
    uint8_t recv_data[MAX_DATA_LEN] = { 0 };
    int16_t rssi;
    int8_t snr;
    
    size_t cursor = 0, color = 0;
	int rc;

	if (device_is_ready(frontlight_dev)) {
		LOG_INF("Found LED strip device %s", frontlight_dev->name);
	} else {
		LOG_ERR("LED strip device %s is not ready", frontlight_dev->name);
		return 0;
	}

	LOG_INF("Displaying pattern on strip");

    memset(&pixels, 0x00, sizeof(pixels));
    memcpy(&pixels[0], &colors[0], sizeof(struct led_rgb));
    memcpy(&pixels[1], &colors[1], sizeof(struct led_rgb));
    memcpy(&pixels[2], &colors[2], sizeof(struct led_rgb));
    memcpy(&pixels[3], &colors[3], sizeof(struct led_rgb));
    rc = led_strip_update_rgb(frontlight_dev, pixels, 4);

    if (rc) {
        LOG_ERR("couldn't update strip: %d", rc);
    }

	if (!device_is_ready(display_dev)) {
		LOG_ERR("device not ready: %s", display_dev->name);
		return 0;
    }

    if (display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO10) != 0) {
		LOG_ERR("Failed to set required pixel format");
        return 0;
    }

    int cfb_ret = cfb_framebuffer_init(display_dev);
	if (cfb_ret != 0) {
		LOG_ERR("cfb init failed! with code: %d", cfb_ret);
    }

	if (!device_is_ready(lora_dev)) {
		LOG_ERR("device not ready: %s", lora_dev->name);
		return 0;
    }

    /*
	config.frequency = 865100000;
	config.bandwidth = BW_125_KHZ;
	config.datarate = SF_10;
	config.preamble_len = 8;
	config.coding_rate = CR_4_5;
	config.iq_inverted = false;
	config.public_network = false;
	config.tx_power = 4;
	config.tx = false;
	//config.tx = true;

	ret = lora_config(lora_dev, &config);
	if (ret < 0) {
		LOG_ERR("LoRa config failed");
		return 0;
	}
    */

	LOG_INF("Display sample for %s", display_dev->name);
	display_get_capabilities(display_dev, &capabilities);
	LOG_INF("width: %d", capabilities.x_resolution);
	LOG_INF("height: %d", capabilities.y_resolution);

	if (capabilities.screen_info & SCREEN_INFO_MONO_VTILED) {
		rect_w = 16;
		rect_h = 8;
	} else {
		rect_w = 2;
		rect_h = 1;
	}

	h_step = rect_h;
	LOG_INF("h_step: %d", h_step);

	scale = (capabilities.x_resolution / 8) / rect_h;

	rect_w *= scale;
	rect_h *= scale;

	buf_size = rect_w * rect_h;

	if (buf_size < (capabilities.x_resolution * h_step)) {
		buf_size = capabilities.x_resolution * h_step;
	}

    // MONO:
    buf_size /= 8;

	buf = k_malloc(buf_size);

	if (buf == NULL) {
		LOG_ERR("Could not allocate memory. Aborting sample.");
		return 0;
	} else {
	    LOG_INF("alloc'd");
    }

	(void)memset(buf, 0xAAu, buf_size / 2);

	buf_desc.buf_size = buf_size;
	buf_desc.pitch = capabilities.x_resolution;
	buf_desc.width = capabilities.x_resolution;
	buf_desc.height = h_step;

    // Fill display with header_data (Boot screen)
    uint8_t *buf_start = buf;
	for (int idx = 0; idx < capabilities.y_resolution; idx += h_step) {
        for (int p = 0; p < capabilities.x_resolution; p = p + 8) {
            uint8_t val = header_data[(idx * capabilities.x_resolution) + p] << 7 | header_data[(idx * capabilities.x_resolution) + p + 1] << 6 | header_data[(idx * capabilities.x_resolution) + p + 2] << 5 | header_data[(idx * capabilities.x_resolution) + p + 3] << 4 | header_data[(idx * capabilities.x_resolution) + p + 4] << 3 | header_data[(idx * capabilities.x_resolution) + p + 5] << 2 | header_data[(idx * capabilities.x_resolution) + p + 6] << 1 | header_data[(idx * capabilities.x_resolution) + p + 7];
            printk("val: %#010x", val);
            *buf++ = val;
        }
        buf = buf_start;
		display_write(display_dev, 0, idx, &buf_desc, buf);
	}

    LOG_INF("buf: %#010x", buf);

	display_blanking_off(display_dev);

	grey_count = 0;
	x = 0;
	y = capabilities.y_resolution - rect_h;

   	/* Receive 4 packets synchronously */
    /*
	LOG_INF("Synchronous reception");
	for (int i = 0; i < 4; i++) {
		// Block until data arrives
		len = lora_recv(lora_dev, data, MAX_DATA_LEN, K_FOREVER,
				&rssi, &snr);
		if (len < 0) {
			LOG_ERR("LoRa receive failed");
			return 0;
		}

		LOG_INF("Received data: %s (RSSI:%ddBm, SNR:%ddBm)",
			data, rssi, snr);
	}
    */

	/* Enable asynchronous reception */
    /*
	LOG_INF("Asynchronous reception");
	lora_recv_async(lora_dev, lora_receive_cb);
	k_sleep(K_FOREVER);
    */

    /*
	while (1) {
		ret = lora_send(lora_dev, data, MAX_DATA_LEN);
		if (ret < 0) {
			LOG_ERR("LoRa send failed");
			return 0;
		}

		LOG_INF("Data sent!");

		// Send data at 1s interval
		k_sleep(K_MSEC(1000));
	}
    */

	return 0;
}

