#include "serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#if defined (HAVE_TERMIOS_H)
#include <termios.h>
#endif

#if defined (HAVE_SYS_IOCTL_H)
#include <sys/ioctl.h>
#endif

#if defined (HAVE_LINUX_SERIAL_H)
#include <linux/serial.h>
#endif

#include <errno.h>

#if defined (HAVE_LINUX_IOCTL_H)
#include <linux/ioctl.h>
#endif

#if defined (HAVE_ASM_IOCTLS_H)
#include <asm/ioctls.h>
#endif

#define DBG 0

ssize_t serial_writebyte(PORTTYPE fd, uint8_t byte)
{
	return serial_write(fd, &byte, 1);
}

ssize_t serial_write(PORTTYPE fd, const uint8_t *buf, size_t len)
{
#if defined (_WIN32)
	DWORD n;
	BOOL r;
	r = WriteFile(fd, buf, len, &n, NULL);
	if (!r) return 0;
	return n;
#else
	ssize_t n = write(fd, buf, len);

	if (n != len) {
		if (DBG) fprintf(stderr, "serial_write() len=%ld, n=%ld, error=%d %s\n", len, n, errno, strerror(errno));
	}

	if (n != len) return -1;

	return n;
#endif
}

ssize_t serial_read(PORTTYPE fd, uint8_t *buf, size_t len)
{
	ssize_t count = 0;

#if defined (_WIN32)
	COMMTIMEOUTS timeout;
	DWORD n;
	BOOL r;
	int waiting=0;

	GetCommTimeouts(fd, &timeout);
	timeout.ReadIntervalTimeout = MAXDWORD; // non-blocking
	timeout.ReadTotalTimeoutMultiplier = 0;
	timeout.ReadTotalTimeoutConstant = 0;
	SetCommTimeouts(fd, &timeout);
	while (count < len) {
		r = ReadFile(fd, buf + count, len - count, &n, NULL);
		if (!r) {
			fprintf(stderr, "serial_read() read error r=%d count=%ld len=%ld\n", r, count, len);
			return 0;
		}
		if (n > 0) count += n;
		else {
			if (waiting) break;  // 1 sec timeout
			timeout.ReadIntervalTimeout = MAXDWORD;
			timeout.ReadTotalTimeoutMultiplier = MAXDWORD;
			timeout.ReadTotalTimeoutConstant = 1000;
			SetCommTimeouts(fd, &timeout);
			waiting = 1;
		}
	}

#else
	int r;
	int retry = 0;

	if (len > sizeof(buf) || len < 1) return -1;

	// non-blocking read mode
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

	while (count < len) {
		r = read(fd, buf + count, len - count);
		//printf("read, r = %d\n", r);
		if (r < 0 && errno != EAGAIN && errno != EINTR) return -1;
		else if (r > 0) count += r;
		else {
			// no data available right now, must wait
			fd_set fds;
			struct timeval t;
			FD_ZERO(&fds);
			FD_SET(fd, &fds);
			t.tv_sec = 1;
			t.tv_usec = 0;
			r = select(fd+1, &fds, NULL, NULL, &t);
			//printf("select, r = %d\n", r);
			if (r < 0) return -1;
			if (r == 0) return count; // timeout
		}
		retry++;
		if (retry > 1000) return -100; // no input
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
#endif

	if (count != len) {
		if (DBG) fprintf(stderr, "serial_read() len=%ld, n=%ld, error=%d %s\n", len, count, errno, strerror(errno));
	}

	return count;
}

#if defined (HAVE_TERMIOS_H)
PORTTYPE serial_open(const char *port, int baud, struct termios *opts)
#else
PORTTYPE serial_open(const char *port, int baud)
#endif
{
	PORTTYPE fd;

	if (DBG) fprintf(stderr, "serial_open: opening port %s @ %d bps\n", port, baud);

	if (!strlen(port)) {
		fprintf(stderr, "serial_open: invalid port name.\n");
		return 0;
	}

#if defined (_WIN32)
	COMMCONFIG cfg;
	COMMTIMEOUTS timeout;
	DWORD n;
	char portname[256];
	int num;
	if (sscanf(port, "COM%d", &num) == 1) {
		sprintf(portname, "\\\\.\\COM%d", num); // Microsoft KB115831
	} else {
		strncpy(portname, port, sizeof(portname)-1);
		portname[n-1] = 0;
	}
	fd = CreateFile(portname, GENERIC_READ | GENERIC_WRITE,
					0, 0, OPEN_EXISTING, 0, NULL);
	if (fd == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "unable to open port %s\n", port);
		return 0;
	}

	GetCommConfig(fd, &cfg, &n);

	cfg.dcb.BaudRate = baud;
	cfg.dcb.fBinary = TRUE;
	cfg.dcb.fParity = FALSE;
	cfg.dcb.fOutxCtsFlow = FALSE;
	cfg.dcb.fOutxDsrFlow = FALSE;
	cfg.dcb.fOutX = FALSE;
	cfg.dcb.fInX = FALSE;
	cfg.dcb.fErrorChar = FALSE;
	cfg.dcb.fNull = FALSE;
	cfg.dcb.fRtsControl = RTS_CONTROL_ENABLE;
	cfg.dcb.fAbortOnError = FALSE;
	cfg.dcb.ByteSize = 8;
	cfg.dcb.Parity = NOPARITY;
	cfg.dcb.StopBits = ONESTOPBIT;
	cfg.dcb.fDtrControl = DTR_CONTROL_ENABLE;
	SetCommConfig(fd, &cfg, n);
	GetCommTimeouts(fd, &timeout);
	timeout.ReadIntervalTimeout = 0;
	timeout.ReadTotalTimeoutMultiplier = 0;
	timeout.ReadTotalTimeoutConstant = 1000;
	timeout.WriteTotalTimeoutConstant = 0;
	timeout.WriteTotalTimeoutMultiplier = 0;
	SetCommTimeouts(fd, &timeout);

	return fd;
#else
	fd = open(port, O_RDWR | O_NOCTTY);
	//fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);

	if (fd == -1)  {
		fprintf(stderr, "serial_open open() failed: %d: %s\n", errno, strerror(errno));
		return 0;
	}

#if defined (HAVE_TERMIOS_H)
	if (tcgetattr(fd, opts) < 0) {
		fprintf(stderr, "serial_open: tcgetattr() failed %d: %s\n", errno, strerror(errno));
		return 0;
	}

	struct termios toptions;

	if (tcgetattr(fd, &toptions) < 0) {
		fprintf(stderr, "serial_open: tcgetattr() failed %d: %s\n", errno, strerror(errno));
		return 0;
	}

	speed_t brate = baud; // let you override switch below if needed

	switch(baud) {
#define B(x) case x: brate=B##x; break;
		B(50);		B(75);		B(110);		B(134);		B(150);
		B(200);		B(300);		B(600);		B(1200);	B(1800);
		B(2400);	B(4800);	B(9600);	B(19200);	B(38400);
		B(57600);	B(115200);	B(230400);	B(460800);	B(500000);
		B(576000);	B(921600);	B(1000000); B(1152000); B(1500000);
		B(2000000); B(2500000); B(3000000); B(3500000); B(4000000);
#undef B
	default:
		fprintf(stderr, "serial_open: unknown baud rate %d\n", baud);
		return 0;
		break;
	}

	cfsetispeed(&toptions, brate);
	cfsetospeed(&toptions, brate);

	// 8N1
	toptions.c_cflag &= ~PARENB;
	toptions.c_cflag &= ~CSTOPB;
	toptions.c_cflag &= ~CSIZE;
	toptions.c_cflag |= CS8;

	// no flow control
	toptions.c_cflag &= ~CRTSCTS;

	// turn off s/w flow ctrl
	toptions.c_iflag &= ~(IXON | IXOFF | IXANY);

	// turn on READ & ignore ctrl lines
	toptions.c_cflag |= CREAD | CLOCAL;

	// make raw
	toptions.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	toptions.c_oflag &= ~OPOST;

	// see: http://unixwiz.net/techtips/termios-vmin-vtime.html
	toptions.c_cc[VMIN]	 = 0;
	toptions.c_cc[VTIME] = 100;

	if (tcsetattr(fd, TCSANOW, &toptions) < 0) {
		fprintf(stderr, "serial_open: tcsetattr() failed %d: %s\n", errno, strerror(errno));
		return 0;
	}

/*
  int flags = fcntl(fd, F_GETFL);
  if (fcntl(fd, F_SETFL, flags & (~O_NONBLOCK))) {
  fprintf(stderr, "serial_open: fcntl() failed %d: %s\n", errno, strerror(errno));
  return 0;
  }
*/

	if (tcflush(fd, TCIOFLUSH) < 0) {
		fprintf(stderr, "serial_open: tcflush() failed %d: %s\n", errno, strerror(errno));
		return 0;
	}
#endif

#if defined (HAVE_LINUX_SERIAL_H)
	struct serial_struct ser_info;

	// Linux-specific: enable low latency mode (FTDI "nagling off")

	if (ioctl(fd, TIOCGSERIAL, &ser_info) < 0) {
		fprintf(stderr, "serial_open: ioctl(TIOCGSERIAL) failed %d: %s\n", errno, strerror(errno));
		return -1;
	}

	ser_info.flags |= ASYNC_LOW_LATENCY;

	if (ioctl(fd, TIOCSSERIAL, &ser_info) < 0) {
		// fprintf(stderr, "serial_open: ioctl(TIOCSSERIAL) failed %d: %s\n", errno, strerror(errno));
	} else {
		fprintf(stderr, "serial_open: set ASYNC_LOW_LATENCY mode.\n");
	}
#endif
#endif
	return fd;
}

#if defined (HAVE_TERMIOS_H)
int serial_close(PORTTYPE fd, struct termios *opts)
#else
int serial_close(PORTTYPE fd)
#endif
{
/*
  if (tcsetattr(fd, TCSANOW, opts) < 0) {
  fprintf(stderr, "serial_close: tcsetattr() failed %d: %s\n", errno, strerror(errno));
  }
*/

#if defined (_WIN32)
	CloseHandle(fd);
#else
	return close(fd);
#endif
}
