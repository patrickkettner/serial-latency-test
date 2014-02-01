#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <limits.h>
#include <float.h>
#include <signal.h>

#if defined (HAVE_SCHED_H)
#include <sched.h>
#endif

#include <getopt.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#if defined (HAVE_SYS_UTSNAME_H)
#include <sys/utsname.h>
#endif

#include "hr_timer.h"
#include "serial.h"

#define DEBUG 1

static volatile sig_atomic_t signal_received = 0;

/* prints an error message to stderr, and dies */
static void fatal(const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	fprintf(stderr, ". Exiting.\n\n");
	exit(EXIT_FAILURE);
}

/* memory allocation error handling */
static void check_mem(void *p)
{
	if (!p)
		fatal("out of memory");
}

#if defined (HAVE_SYS_UTSNAME_H)
void print_uname()
{
	struct utsname u;
	uname (&u);
	printf ("> running on %s release %s (version %s) on %s\n",
			u.sysname, u.release, u.version, u.machine);
}
#endif

/* sets the process to "policy" policy at given priority */
static int set_realtime_priority(int policy, int prio)
{
#if defined (HAVE_SCHED_H)
	struct sched_param schp;
	memset(&schp, 0, sizeof(schp));

	schp.sched_priority = prio;
	if (sched_setscheduler(0, policy, &schp) != 0) {
		perror("sched_setscheduler");
		return -1;
	}
#endif

	return 0;
}

static void usage(const char *argv0)
{
	printf("Usage: %s -p <port> ...\n\n"
		   "  -p, --port=port	 serial port to run tests on\n"
		   "  -b, --baud=baud	 baud rate (default: 9600)\n"
#if defined (HAVE_SCHED_H)
		   "  -R, --realtime	 use realtime scheduling (default: no)\n"
		   "  -P, --priority=int scheduling priority, use with -R\n"
		   "					 (default: 99)\n\n"
#endif
		   "  -S, --samples=<int> to take for the measurement (default: 10000)\n"
           "  -c, --count=<int>  number of bytes to send per sample (default: 1)\n"
		   "  -w, --wait=ms		 time interval between measurements\n"
#if defined (HAVE_LINUX_SERIAL_H)
		   "  -x  --xmit=<int>   set xmit_fifo_size to given number (default: 0)\n"
#endif
		   "  -r, --random-wait	 use random interval between wait and 2*wait\n\n"
           "  -o, --output=file  write the output to file\n\n"
		   "  -h, --help		 this help\n"
		   "  -V, --version		 print current version\n\n"
		   "Report bugs to Jakob Flierl <jakob.flierl@gmail.com>\n"
		   "Website and manual: https://github.com/koppi/serial-latency-test\n"
		   "\n", argv0);
}

static void print_version(void)
{
	printf("%s version %s\n", PACKAGE, VERSION);
}

#if defined (_WIN32)
static void wait_ms(double t)
{
	Sleep(t);
}
#else
static void wait_ms(double t)
{
	struct timespec ts;

	ts.tv_sec = t / 1000;
	ts.tv_nsec = (t - ts.tv_sec * 1000) * 1000000;
	nanosleep(&ts, NULL);
}
#endif

/* many thanks to Randall Munroe (http://xkcd.com/221/) */
static int getRandomNumber(void)
{
	return 4; // chosen by fair dice roll.
			  // guaranteed to be random.
}

static void sighandler(int sig)
{
	fprintf(stderr,"caught signal - shutting down.\n");
	signal_received = 1;
}

typedef struct {
	PORTTYPE fd;
	int	 baud;
	char port[PATH_MAX];
#if defined (HAVE_TERMIOS_H)
	struct termios opts;
#endif
} serial_t;

int main(int argc, char *argv[])
{
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	static struct option long_options[] = {
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
#if defined (HAVE_SCHED_H)
		{"realtime", no_argument, NULL, 'R'},
		{"priority", required_argument, NULL, 'P'},
#endif
		{"port", required_argument, NULL, 'p'},
		{"baud", required_argument, NULL, 'b'},
		{"samples", required_argument, NULL, 'S'},
		{"count", required_argument, NULL, 'c'},
		{"wait", required_argument, NULL, 'w'},
#if defined (HAVE_LINUX_SERIAL_H)
		{"xmit", required_argument, NULL, 'x'},
#endif
		{"random-wait", no_argument, NULL, 'r'},
		{"output", required_argument, NULL, 'o'},
		{}
	};

#if defined (HAVE_SCHED_H)
	int do_realtime = 0;
	int rt_prio = sched_get_priority_max(SCHED_FIFO);
#endif
#if defined (HAVE_LINUX_SERIAL_H)
	int xmit_fifo_size = 0;
#endif
	int nr_samples = 10000;
	int nr_count = 1;
	int random_wait = 0;
	double wait = 0.0;
	char output[PATH_MAX];

	serial_t s;

	s.fd   = 0;
	s.baud = 9600;
	snprintf(s.port, sizeof s.port, "%s", "");

	snprintf(output, sizeof output, "%s", "");

	int c;

	while ((c = getopt_long(argc, argv,
							"h"   /* help */
							"V"   /* version */
#if defined (HAVE_SCHED_H)
							"R"   /* realtime */
							"P:"  /* priority */
#endif
							"p:"  /* port */
							"b:"  /* baud */
							"S:"  /* samples */
							"c:"  /* count */
							"w:"  /* wait */
#if defined (HAVE_LINUX_SERIAL_H)
							"x:"  /* xmit */
#endif
							"r"   /* random-wait */
							"o:", /* output */
							long_options, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage(argv[0]);
			return EXIT_SUCCESS;
		case 'V':
			print_version();
			return EXIT_SUCCESS;
#if defined (HAVE_SCHED_H)
		case 'R':
			do_realtime = 1;
			break;
#endif
		case 'p':
			strncpy(s.port, optarg, sizeof(s.port));
			break;
		case 'b':
			s.baud = strtol(optarg, NULL, 10);
			break;
#if defined (HAVE_SCHED_H)
		case 'P':
			rt_prio = atoi(optarg);
			if (rt_prio > sched_get_priority_max(SCHED_FIFO)) {
				printf("> Warning: Given priority:	 %d > sched_get_priority_max(SCHED_FIFO)! ", rt_prio);
				printf("Setting priority to %d.\n", sched_get_priority_max(SCHED_FIFO));
				rt_prio = sched_get_priority_max(SCHED_FIFO);
			} else if (rt_prio < sched_get_priority_min(SCHED_FIFO)) {
				printf("> Warning: Given priority:	 %d < sched_get_priority_min(SCHED_FIFO)! ", rt_prio);
				printf("Setting priority to %d.\n", sched_get_priority_min(SCHED_FIFO));
				rt_prio = sched_get_priority_min(SCHED_FIFO);
			}
			break;
#endif
		case 'S':
			nr_samples = atoi(optarg);
			if (nr_samples <= 0) {
				printf("> Warning: Given number of samples to take is less or equal zero! ");
				printf("Setting nr of samples to take to 1.\n");
				nr_samples = 1;
			}
			break;
		case 'c':
			nr_count = atoi(optarg);
			break;
		case 'w':
			wait = atof(optarg);
			if (wait < 0) {
				printf("> Warning: Wait time is negative; using zero.\n");
				wait = 0;
			}
			break;
#if defined(HAVE_LINUX_SERIAL_H)
		case 'x':
			xmit_fifo_size = atoi(optarg);
			break;
#endif
		case 'r':
			random_wait = 1;
			break;
		case 'o':
			strncpy(output, optarg, sizeof(output));
			break;
		default:
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (argc == 1 || argv[optind]) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	print_version();

#if defined (HAVE_SYS_UTSNAME_H)
	print_uname();
#endif

	if (random_wait)
		srand(getRandomNumber());

#if defined (HAVE_SCHED_H)
	if (do_realtime) {
		printf("> set_realtime_priority(SCHED_FIFO, %d).. ", rt_prio);
		set_realtime_priority(SCHED_FIFO, rt_prio);
		printf("done.\n");
	}
#endif

#if defined (HAVE_TERMIOS_H)
	s.fd = serial_open(s.port, s.baud, &s.opts);
#else
	s.fd = serial_open(s.port, s.baud);
#endif

	if (!s.fd) {
		fatal("Unable to open %s", s.port);
	}

#if defined (HAVE_LINUX_SERIAL_H)
	if (xmit_fifo_size > 0) {
		if (serial_set_xmit_fifo_size(s.fd, xmit_fifo_size) < 0) {
			fatal("Unable to set xmit_fifo_size %d on %s", xmit_fifo_size, s.port);
		} else {
			printf("> set xmit_fifo_size to %d\n", serial_get_xmit_fifo_size(s.fd));
		}
	}
#endif

	timerStruct begin, end;

	printf("\n> sampling %d latency values - please wait ...\n", nr_samples);
	printf("> press Ctrl+C to end test.\n");

	signal(SIGINT,	sighandler);
	signal(SIGTERM, sighandler);

	double *delays = calloc(nr_samples + 1, sizeof *delays);
	check_mem(delays);

	unsigned int sample_nr = 0;
	double min_delay = DBL_MAX, max_delay = 0;

	uint8_t *buf_rx = calloc(nr_count + 1, sizeof (uint8_t));
	uint8_t *buf_tx = calloc(nr_count + 1, sizeof (uint8_t));

	unsigned int i;

	for (i = 0; i < nr_count; ++i) {
		buf_tx[i] = i % 255;
	}

	for (c = 0; c < nr_samples; ++c) {
		if (wait) {
			if (random_wait)
				wait_ms(wait + rand() * wait / RAND_MAX);
			else
				wait_ms(wait);
			if (signal_received)
				break;
		}

		int n = 0;

		GetHighResolutionTime(&begin);

		n = serial_write(s.fd, buf_tx, nr_count);

		if (n != nr_count) {
			fprintf(stderr, "serial_write() n = %d nr_count = %d\n", n, nr_count);
		}

		int rd = 0;

		while(!rd) {
			n = serial_read(s.fd, buf_rx, nr_count); // blocking read using select

			if (n == nr_count) {
				rd = 1;
			} else {
				rd = 1;
				signal_received = 1;
				fprintf(stderr, "serial_read() n = %d nr_count = %d\n", n, nr_count);
			}
		}

		if (signal_received)
			break;

		GetHighResolutionTime(&end);

		double delay_ns = ConvertTimeDifferenceToSec(&end, &begin) * 1000000000.0;
		if (delay_ns > max_delay) {
			max_delay = delay_ns;
			if (DEBUG)
				printf("%6u; %10.2f; %10.2f		\n",
					   sample_nr, delay_ns / 1000000.0, max_delay / 1000000.0);
		} else {
			if (DEBUG)
				printf("%6u; %10.2f; %10.2f		\r",
					   sample_nr, delay_ns / 1000000.0, max_delay / 1000000.0);
		}
		if (delay_ns < min_delay)
			min_delay = delay_ns;

		delays[sample_nr++] = delay_ns;
	}

	printf("\n> done.\n\n> latency distribution:\n");

	if (!max_delay) {
		fatal("No delay was measured; clock has too low resolution");
	}

	if (strlen(output)) {
		FILE *fp = fopen(output, "w");

		if (!fp) {
			fatal("unable to open output file '%s'", output);
		}

		for (i = 0; i < sample_nr; ++i) {
			fprintf(fp, "%10.2f\n", delays[i] / 1000000.0);
		}

		fclose(fp);
	}

	unsigned int j;
	unsigned int delay_hist[1000];

#define ARRAY_SIZE(a) (sizeof(a) / sizeof *(a))

	for (i = 0; i < ARRAY_SIZE(delay_hist); ++i)
		delay_hist[i] = 0;
	for (i = 0; i < sample_nr; ++i) {
		unsigned int index = (delays[i] + 50000.0) / 100000.0;
		if (index >= ARRAY_SIZE(delay_hist))
			index = ARRAY_SIZE(delay_hist) - 1;
		delay_hist[index]++;
	}

	unsigned int max_samples = 0;
	for (i = 0; i < ARRAY_SIZE(delay_hist); ++i) {
		if (delay_hist[i] > max_samples)
			max_samples = delay_hist[i];
	}

	if (!max_samples) {
		fatal("(no measurements)");
	}

	// plot ascii bars
	int skipped = 0;
	for (i = 0; i < ARRAY_SIZE(delay_hist); ++i) {
		if (delay_hist[i] > 0) {
			if (skipped) {
				puts("...");
				skipped = 0;
			}
			printf("%5.1f -%5.1f ms: %8u ", i/10.0, i/10.0 + 0.09, delay_hist[i]);
			unsigned int bar_width = (delay_hist[i] * 50 + max_samples / 2) / max_samples;
			if (!bar_width && delay_hist[i])
				bar_width = 1;
			for (j = 0; j < bar_width; ++j)
				printf("#");
			puts("");
		} else {
			skipped = 1;
		}
	}

	printf("\n  best latency was %.2f msec\n", min_delay / 1000000.0);
	printf(" worst latency was %.2f msec\n\n", max_delay / 1000000.0);

	free(delays);
	free(buf_rx);
	free(buf_tx);

#if defined(HAVE_TERMIOS_H)
	serial_close(s.fd, &s.opts);
#else
	serial_close(s.fd);
#endif

	return EXIT_SUCCESS;
}
