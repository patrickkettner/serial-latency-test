#ifndef SERIAL_H
#define SERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined (HAVE_CONFIG_H)
#include "config.h"
#endif

#if defined (HAVE_TERMIOS_H)
#include <termios.h>
#else
#include <windows.h>
#endif

#include <unistd.h>
#include <stdint.h>

#if defined (HAVE_TERMIOS_H)
#define PORTTYPE int
#else
#define PORTTYPE HANDLE
#endif

#if defined (HAVE_TERMIOS_H)
	PORTTYPE serial_open(const char *port, int baud, struct termios *opts);
	int		 serial_close(PORTTYPE fd, struct termios *opts);
#if defined (HAVE_LINUX_SERIAL_H)
	int      serial_set_xmit_fifo_size(PORTTYPE fd, int size);
	int      serial_get_xmit_fifo_size(PORTTYPE fd);
#if defined (ASYNC_LOW_LATENCY)
	int      serial_set_low_latency(PORTTYPE fd);
#endif
#endif
#else
	PORTTYPE serial_open(const char *port, int baud);
	int		 serial_close(PORTTYPE fd);
#endif
	ssize_t	 serial_writebyte(PORTTYPE fd, uint8_t byte);
	ssize_t	 serial_write(PORTTYPE fd, const uint8_t *buf, size_t len);
	ssize_t	 serial_read(PORTTYPE fd, uint8_t *buf, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
