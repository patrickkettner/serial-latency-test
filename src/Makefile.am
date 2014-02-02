LDADD = -lm @CLOCK_LIB@

bin_PROGRAMS = serial-latency-test
man_MANS=serial-latency-test.1

EXTRA_DIST = serial-latency-test.1

serial_latency_test_SOURCES = serial-latency-test.c serial.c serial.h hr_timer.h

serial-latency-test.1: serial-latency-test.c $(top_srcdir)/configure.ac
	help2man -N -n 'Serial Port Latency Measurement Tool' -o $@ ./serial-latency-test$(EXEEXT)
