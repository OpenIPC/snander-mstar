#include <stdio.h>
#include <string.h>
#include <libusb.h>
#include "spi_controller.h"

#define CH341A_USB_VID 0x1A86
#define CH341A_USB_PID 0x5512

#define USB_EP_WRITE 0x02
#define USB_EP_READ 0x82

#define CH341A_CMD_I2C_STREAM 0xAA
#define CH341A_CMD_I2C_STM_END 0x00
#define CH341A_CMD_I2C_STM_SET 0x60
#define CH341A_CMD_I2C_STM_STA 0x74
#define CH341A_CMD_I2C_STM_STO 0x75
#define CH341A_CMD_I2C_STM_OUT 0x80
#define CH341A_CMD_I2C_STM_IN 0xC0

#define MSTAR_WRITE 0x10
#define MSTAR_READ 0x11
#define MSTAR_END 0x12
#define MSTAR_PORT 0x49
#define MSTAR_DEBUG 0x59

static struct libusb_device_handle *handle = NULL;

static int ch341a_i2c_transfer(uint8_t type, uint8_t *buf, int len) {
	int size = 0;

	int ret = libusb_bulk_transfer(handle, type, buf, len, &size, 1000);
	if (ret < 0) {
		printf("Failed to %s %d bytes: %s\n", (type == USB_EP_READ) ?
			"read" : "write", len, libusb_error_name(ret));
		return -1;
	}

	return size;
}

static int ch341a_i2c_read(uint8_t *data, uint32_t len, uint32_t addr) {
	uint8_t buf[32];
	uint8_t sz = 0;
	int ret = 0;

	buf[sz++] = CH341A_CMD_I2C_STREAM;
	buf[sz++] = CH341A_CMD_I2C_STM_STA;
	buf[sz++] = CH341A_CMD_I2C_STM_OUT;
	buf[sz++] = (addr << 1) | 1;

	if (len > 1) {
		buf[sz++] = CH341A_CMD_I2C_STM_IN | (len - 1);
	}

	if (len) {
		buf[sz++] = CH341A_CMD_I2C_STM_IN;
	}

	buf[sz++] = CH341A_CMD_I2C_STM_STO;
	buf[sz++] = CH341A_CMD_I2C_STM_END;

	ret = ch341a_i2c_transfer(USB_EP_WRITE, buf, sz);
	if (ret < 0) {
		return -1;
	}

	ret = ch341a_i2c_transfer(USB_EP_READ, buf, len + 1);
	if (ret < 0) {
		return -1;
	}

	if (*buf & 0x80) {
		return -1;
	}

	memcpy(data, buf + 1, len);

	return 0;
}

static int ch341a_i2c_write(uint8_t *data, uint32_t len, uint32_t addr) {
	uint8_t buf[32];
	uint8_t sz = 0;
	int ret = 0;

	buf[sz++] = CH341A_CMD_I2C_STREAM;
	buf[sz++] = CH341A_CMD_I2C_STM_STA;
	buf[sz++] = CH341A_CMD_I2C_STM_OUT | (len + 1);
	buf[sz++] = addr << 1;

	memcpy(&buf[sz], data, len);
	sz += len;
	buf[sz++] = CH341A_CMD_I2C_STM_STO;
	buf[sz++] = CH341A_CMD_I2C_STM_END;

	ret = ch341a_i2c_transfer(USB_EP_WRITE, buf, sz);
	if (ret < 0) {
		return -1;
	}

	return 0;
}

static int ch341a_i2c_send_command(uint32_t writecnt, uint32_t readcnt,
		const uint8_t *writearr, uint8_t *readarr) {
	if (readcnt) {
		uint8_t cmd = MSTAR_READ;
		if (ch341a_i2c_write(&cmd, 1, MSTAR_PORT) < 0) {
			return -1;
		}

		if (ch341a_i2c_read(readarr, readcnt, MSTAR_PORT) < 0) {
			return -1;
		}
	}

	if (writecnt) {
		uint8_t buffer[32];
		buffer[0] = MSTAR_WRITE;
		memcpy(buffer + 1, writearr, writecnt);
		if (ch341a_i2c_write(buffer, writecnt + 1, MSTAR_PORT) < 0) {
			return -1;
		}
	}

	return 0;
}

static int ch341a_i2c_detect(void) {
	printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
	for (int addr = 0; addr < 0x80; addr++) {
		if (addr % 16 == 0) {
			printf("\n%02x: ", addr);
		}

		if (ch341a_i2c_read(NULL, 0, addr) < 0) {
			printf("-- ");
		} else {
			printf("%02x ", addr);
		}
	}

	printf("\n\n");

	return 0;
}

static int ch341a_i2c_set_uart(uint32_t addr) {
	uint8_t dbg[] = {'S', 'E', 'R', 'D', 'B'};
	if (ch341a_i2c_write(dbg, 5, addr) < 0) {
		return -1;
	}

	uint8_t init[] = {0x81, 0x83, 0x84, 0x53, 0x7F, 0x35, 0x71};
	for (int i = 0; i < sizeof(init); i++) {
		if (ch341a_i2c_write(&init[i], 1, addr) < 0) {
			return -1;
		}
	}

	uint8_t uart[] = {0x10, 0x00, 0x00, 0x0E, 0x12, 0x00, 0x00};
	if (ch341a_i2c_write(uart, sizeof(uart), addr) < 0) {
		return -1;
	}

	if (ch341a_i2c_write(uart, 6, addr) < 0) {
		return -1;
	}

	uint8_t reg[1];
	if (ch341a_i2c_read(reg, 1, addr) < 0) {
		return -1;
	}

	if (*reg != 0) {
		return -1;
	}

	uint8_t exit[] = {0x34, 0x45};
	for (int j = 0; j < sizeof(exit); j++) {
		if (ch341a_i2c_write(&exit[j], 1, addr) < 0) {
			return -1;
		}
	}

	return 0;
}

static int ch341a_i2c_release(bool enable) {
	uint8_t cmd = MSTAR_END;
	if (ch341a_i2c_write(&cmd, 1, MSTAR_PORT) < 0) {
		return -1;
	}

	return 0;
}

static int ch341a_i2c_init(const char *options) {
	int ret = libusb_init(NULL);
	if (ret < 0) {
		printf("Could not initialize libusb\n");
		return -1;
	}

	handle = libusb_open_device_with_vid_pid(NULL, CH341A_USB_VID, CH341A_USB_PID);
	if (handle == NULL) {
		printf("Error opening usb device [%X:%X]\n", CH341A_USB_VID, CH341A_USB_PID);
		return -1;
	}

	printf("Found programmer device: WinChipHead (WCH) - CH341A\n");

#ifdef __gnu_linux__
	if (libusb_kernel_driver_active(handle, 0)) {
		if (libusb_detach_kernel_driver(handle, 0)) {
			printf("Failed to detach kernel driver: %s\n", libusb_error_name(ret));
			return -1;
		}
	}
#endif

	ret = libusb_claim_interface(handle, 0);
	if (ret) {
		printf("Failed to claim interface: %s\n", libusb_error_name(ret));
		goto close_handle;
	}

	bool speed = strstr(options, "speed");
	printf("Using I2C %s speed mode\n\n", speed ? "fast" : "default");

	uint8_t buf[] = {
		CH341A_CMD_I2C_STREAM,
		CH341A_CMD_I2C_STM_SET | speed ? 2 : 1,
		CH341A_CMD_I2C_STM_END
	};

	ret = ch341a_i2c_transfer(USB_EP_WRITE, buf, 3);
	if (ret < 0) {
		printf("Failed to configure stream interface\n");
		goto close_handle;
	}

	if (strstr(options, "query")) {
		ch341a_i2c_detect();
	}

	if (ch341a_i2c_read(NULL, 0, MSTAR_PORT) < 0) {
		printf("Failed to detect i2c device: 0x%X\n", MSTAR_PORT);
		goto close_handle;
	}

	if (ch341a_i2c_read(NULL, 0, MSTAR_DEBUG) < 0) {
		if (ch341a_i2c_set_uart(MSTAR_DEBUG) < 0) {
			printf("Failed to clear pm_uart bit!\n\n");
		}
	}

	uint8_t cmd[] = {'M', 'S', 'T', 'A', 'R'};
	if (ch341a_i2c_write(cmd, 5, MSTAR_PORT) < 0) {
		if (ch341a_i2c_release(false) < 0) {
			goto close_handle;
		}
	}

	return 0;

close_handle:
	libusb_close(handle);
	libusb_exit(NULL);
	handle = NULL;

	return -1;
}

static int ch341a_i2c_shutdown(void) {
	libusb_release_interface(handle, 0);
	libusb_close(handle);
	libusb_exit(NULL);
	handle = NULL;

	return 0;
}

const struct spi_controller ch341a_i2c_ctrl = {
	.name = CH341A_I2C_DEVICE,
	.init = ch341a_i2c_init,
	.shutdown = ch341a_i2c_shutdown,
	.send_command = ch341a_i2c_send_command,
	.cs_release = ch341a_i2c_release,
};
