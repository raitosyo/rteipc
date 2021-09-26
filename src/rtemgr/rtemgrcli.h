// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#ifndef _LIBMGRCLI_H
#define _LIBMGRCLI_H

#include "rteipc.h"

/* Bus type definition */
#define BUS_IPC			1
#define BUS_TTY			2
#define BUS_GPIO		3
#define BUS_SPI			4
#define BUS_I2C			5
#define BUS_SYSFS		6
#define BUS_INET		7

#define DOMAIN_RTEMGR		0

/**
 * Helper macros for a managed interface in the default domain.
 */
#define rtemgr_send(ctx, name, data, len)  \
	rtemgr_send_domain(ctx, DOMAIN_RTEMGR, name, data, len);

#define rtemgr_gpio_send(ctx, name, value)  \
	rtemgr_gpio_send_domain(ctx, DOMAIN_RTEMGR, name, value);

#define rtemgr_spi_send(ctx, name, data, len, rdmode)  \
	rtemgr_spi_send_domain(ctx, DOMAIN_RTEMGR, name, data, len, rdmode);

#define rtemgr_i2c_send(ctx, name, addr, data, wlen, rlen)  \
	rtemgr_i2c_send_domain(ctx, DOMAIN_RTEMGR, name, addr, data, wlen, rlen);

#define rtemgr_sysfs_send(ctx, name, attr, newval)  \
	rtemgr_sysfs_send_domain(ctx, DOMAIN_RTEMGR, name, attr, newval);

#define rtemgr_encode(name, data, len, p_out, p_size)  \
	rtemgr_encode_domain(DOMAIN_RTEMGR, name, data, len, p_out, p_size)

#define rtemgr_gpio_encode(name, val, p_out, p_size)  \
	rtemgr_gpio_encode_domain(DOMAIN_RTEMGR, name, val, p_out, p_size)

#define rtemgr_spi_encode(name, data, len, rdmode, p_out, p_size)  \
	rtemgr_spi_encode_domain(DOMAIN_RTEMGR, name, data, len, rdmode, \
			p_out, p_size)

#define rtemgr_i2c_encode(name, addr, data, wlen, rlen, p_out, p_size)  \
	rtemgr_i2c_encode_domain(DOMAIN_RTEMGR, name, addr, data, \
			wlen, rlen, p_out, p_size)

#define rtemgr_sysfs_encode(name, attr, newval, p_out, p_size)  \
	rtemgr_sysfs_encode_domain(DOMAIN_RTEMGR, name, attr, newval, \
			p_out, p_size)

/**
 * Helper function to send generic data to the rtemgr managed interface.
 * For raw (non-managed) interface, use rteipc_send().
 */
int rtemgr_send_domain(int ctx, int domain, const char *name,
			const void *data, size_t len);

/**
 * Helper function to send data for GPIO to the rtemgr managed interface.
 * For raw (non-managed) interface, use rteipc_gpio_send().
 */
int rtemgr_gpio_send_domain(int ctx, int domain, const char *name,
			uint8_t value);

/**
 * Helper function to send data for SPI to the rtemgr managed interface.
 * For raw (non-managed) interface, use rteipc_spi_send().
 */
int rtemgr_spi_send_domain(int ctx, int domain, const char *name,
			const uint8_t *data, uint16_t len, bool rdmode);

/**
 * Helper function to send data for I2C to the rtemgr managed interface.
 * For raw (non-managed) interface, use rteipc_i2c_send().
 */
int rtemgr_i2c_send_domain(int ctx, int domain, const char *name,
			uint16_t addr, const uint8_t *data, uint16_t wlen,
			uint16_t rlen);

/**
 * Helper function to send data for SYSFS to the rtemgr managed interface.
 * For raw (non-managed) interface, use rteipc_sysfs_send().
 */
int rtemgr_sysfs_send_domain(int ctx, int domain, const char *name,
			const char *attr, const char *newval);

/**
 * Generic function to encode data to the rtemgr managed interface format.
 * Data sent to the managed interface is must be encoded before sending by
 * this function or other rtemgr_*_encode_domain functions depending on its
 * data type.
 */
int rtemgr_encode_domain(int domain, const char *name, const void *data,
			size_t len, void **out, size_t *written);

/**
 * rtemgr_encode_domain for encoding GPIO data type.
 */
int rtemgr_gpio_encode_domain(int domain, const char *name, uint8_t value,
			void **out, size_t *written);

/**
 * rtemgr_encode_domain for encoding SPI data type.
 */
int rtemgr_spi_encode_domain(int domain, const char *name,
			const uint8_t *data, uint16_t len, bool rdmode,
			void **out, size_t *written);

/**
 * rtemgr_encode_domain for encoding I2C data type.
 */
int rtemgr_i2c_encode_domain(int domain, const char *name, uint16_t addr,
			const uint8_t *data, uint16_t wlen, uint16_t rlen,
			void **out, size_t *written);

/**
 * rtemgr_encode_domain for encoding SYSFS data type.
 */
int rtemgr_sysfs_encode_domain(int domain, const char *name, const char *attr,
			const char *newval, void **out, size_t *written);

/* Decode handle for data returned by rtemgr_decode() */
typedef struct rtemgr_managed_data  rtemgr_dh;

/**
 * Function for decoding data in format of the rtemgr managed interface.
 * rtemgr_dh can be passed as the parameter to rtemgr_get_* functions to
 * get a field of managed packet (e.g. bus type, domain, data or length of
 * data).
 *
 * Caller is responsible to take care of rtemgr_dh by rtemgr_put_dh().
 */
rtemgr_dh *rtemgr_decode(const unsigned char *data, size_t len);

/**
 * Free resources related to rtemgr_dh.
 */
void rtemgr_put_dh(rtemgr_dh *h);

/* Return data in the packet */
void *rtemgr_get_data(const rtemgr_dh *d);

/* Return the length of data in the packet */
size_t rtemgr_get_length(const rtemgr_dh *d);

/* Return the name of the sender for data in the packet */
char *rtemgr_get_name(const rtemgr_dh *d);

/* Return the domain of the sender for data in the packet */
int rtemgr_get_domain(const rtemgr_dh *d);

/* Return the bus type of the sender for data in the packet */
int rtemgr_get_type(const rtemgr_dh *d);

#endif /* _LIBMGRCLI_H */
