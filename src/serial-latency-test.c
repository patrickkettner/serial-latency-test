/* Serial Latency Tester
 *
 * (C) 2013  Jakob Flierl <jakob.flierl@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <math.h>

#ifndef INFINITY
#define INFINITY (1.0/0.0)
#endif 

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

#if defined (HAVE_LINUX_SERIAL_H)
#include <linux/serial.h>
#endif

#include "serial.h"

#define DEBUG 1

#define HISTLEN  10
#define TERMWIDTH 50

#ifndef SQUARE
#define SQUARE(a) ( (a) * (a) )
#endif

#ifndef MIN
#define MIN(a,b) ( (a) < (b) ? (a) : (b) )
#endif

#ifndef MAX
#define MAX(a,b) ( (a) > (b) ? (a) : (b) )
#endif

#ifndef RAIL
#define RAIL(v, min, max) (MIN((max), MAX((min), (v))))
#endif

static int printinterval = 1;

static volatile sig_atomic_t signal_received = 0;

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
           "  -p, --port=port    serial port to run tests on\n"
           "  -b, --baud=baud    baud rate (default: 9600)\n"
#if defined (HAVE_SCHED_H)
           "  -R, --realtime     use realtime scheduling (default: no)\n"
           "  -P, --priority=n   scheduling priority, use with -R\n"
           "                     (default: 99)\n\n"
#endif
           "  -S, --samples=n    to take for the measurement (default: 10000)\n"
           "  -c, --count=n      number of bytes to send per sample (default: 1)\n"
           "  -w, --wait=ms      time interval between measurements (default: 0)\n"
           "  -r, --random-wait  use random interval between wait and 2*wait\n\n"
#if defined (HAVE_LINUX_SERIAL_H)
#if defined (ASYNC_LOW_LATENCY)
           "  -a, --async        set ASYNC_LOW_LATENCY flag (default: no)\n"
#endif
           "  -x  --xmit=n       set xmit_fifo_size to given number (default: 0)\n"
#endif
           "  -o, --output=file  write the output to file\n\n"
           "  -h, --help         this help\n"
           "  -V, --version      print current version\n\n"
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
	if (!signal_received)
		fprintf(stderr,"\n\n> caught signal - shutting down.\n");
    signal_received = 1;
}

typedef struct {
    PORTTYPE fd;
    int  baud;
    char port[PATH_MAX];
#if defined (HAVE_TERMIOS_H)
    struct termios opts;
#endif
} serial_t;

int digits(double number) {
    int digits = 1, pten = 10;

    while (pten <= number) {
        digits++; pten *= 10;
    }

    return digits;
}

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
        {"random-wait", no_argument, NULL, 'r'},
#if defined (HAVE_LINUX_SERIAL_H)
#if defined (ASYNC_LOW_LATENCY)
        {"async", no_argument, NULL, 'a'},
#endif
        {"xmit", required_argument, NULL, 'x'},
#endif
        {"output", required_argument, NULL, 'o'},
        {}
    };

#if defined (HAVE_SCHED_H)
    int do_realtime = 0;
    int rt_prio = sched_get_priority_max(SCHED_FIFO);
#endif
#if defined (HAVE_LINUX_SERIAL_H)
#if defined (ASYNC_LOW_LATENCY)
    int async_low_latency = 0;
#endif
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
                            "r"   /* random-wait */
#if defined (HAVE_LINUX_SERIAL_H)
#if defined (ASYNC_LOW_LATENCY)
                            "a"   /* async */
#endif
                            "x:"  /* xmit */
#endif
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
                printf("> Warning: Given priority:   %d > sched_get_priority_max(SCHED_FIFO)! ", rt_prio);
                printf("Setting priority to %d.\n", sched_get_priority_max(SCHED_FIFO));
                rt_prio = sched_get_priority_max(SCHED_FIFO);
            } else if (rt_prio < sched_get_priority_min(SCHED_FIFO)) {
                printf("> Warning: Given priority:   %d < sched_get_priority_min(SCHED_FIFO)! ", rt_prio);
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
#if defined (ASYNC_LOW_LATENCY)
        case 'a':
            async_low_latency = 1;
            break;
#endif
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

    printf("> ");
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
#if defined (ASYNC_LOW_LATENCY)
    if (async_low_latency) {
        if (serial_set_low_latency(s.fd) < 0) {
            fatal("Unable to set ASYNC_LOW_LATENCY on %s", s.port);
        } else {
            printf("> set flag ASYNC_LOW_LATENCY to %d on %s\n", async_low_latency, s.port);
        }
    }
#endif

    if (xmit_fifo_size > 0) {
        if (serial_set_xmit_fifo_size(s.fd, xmit_fifo_size) < 0) {
            fatal("Unable to set xmit_fifo_size %d on %s", xmit_fifo_size, s.port);
        } else {
            printf("> set xmit_fifo_size to %d on %s\n", serial_get_xmit_fifo_size(s.fd), s.port);
        }
    }
#endif

    timerStruct begin, end;

    printf("\n> sampling %d latency values - please wait..\n", nr_samples);
    printf("   event     curr      min      max      avg [ms]\n");

    signal(SIGINT,  sighandler);
    signal(SIGTERM, sighandler);

    double *delays = calloc(nr_samples + 1, sizeof *delays);
    check_mem(delays);

    int cnt_a;
    double min_a, max_a;
    double avg_a;
    double var_m, var_s;

    unsigned int *history = calloc(HISTLEN, sizeof(unsigned int));
    unsigned int histsize = 0;
    double bin_width = 0;
    double bin_min = DBL_MIN;
    unsigned int *histogram = NULL;

    cnt_a = 0;
    min_a = DBL_MAX;
    max_a = 0;
    avg_a = 0;
    var_m = var_s = 0;

    uint8_t *buf_rx = calloc(nr_count + 1, sizeof (uint8_t));
    uint8_t *buf_tx = calloc(nr_count + 1, sizeof (uint8_t));

    unsigned int i;

    for (i = 0; i < nr_count; ++i) {
        buf_tx[i] = i % 255;
    }

    time_t last = time(NULL);

    int err = 0;

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
            err = 1;
            signal_received = 1;
        }

        if (signal_received)
            break;

        int rd = 0;

        while(!rd) {
            n = serial_read(s.fd, buf_rx, nr_count); // blocking read using select

            if (n == nr_count) {
                rd = 1;
            } else {
                rd = 1;
                signal_received = 1;
                err = 1;
                fprintf(stderr, "serial_read() n = %d nr_count = %d\n", n, nr_count);
            }
        }

        if (signal_received)
            break;

        GetHighResolutionTime(&end);

        double delay = ConvertTimeDifferenceToSec(&end, &begin) * 1000.0;

        delays[cnt_a] = delay;

        time_t now = time(NULL);

        if (printinterval > 0 && now >= last + printinterval) {
            last = now;
            if (cnt_a > 0)
                printf("\n");
        }

        avg_a += delay;

        if (delay < min_a) min_a = delay;
        if (delay > max_a) max_a = delay;

        printf(" %7d %8.2f %8.2f %8.2f %8.2f\r", cnt_a, delay, min_a, max_a, avg_a / (double)cnt_a);

        /* histogram */
        if (cnt_a < HISTLEN) {
            history[cnt_a] = delay;
        } else if (cnt_a == HISTLEN) {
            int j;
            double stddev = 0;
            const double avg = avg_a / (double)HISTLEN;
            for (j = 0; j < HISTLEN; ++j) {
                stddev += SQUARE((double)history[j] - avg);
            }
            stddev = sqrt(stddev/(double)HISTLEN);
            // Scott's normal reference rule
            bin_width = 3.5 * stddev * pow(HISTLEN, -1.0/3.0);
            int k = ceil((double)(max_a - min_a) / bin_width);

            bin_min = min_a;
            if (bin_min > bin_width) { k++; bin_min -= bin_width; }
            if (bin_min > bin_width) { k++; bin_min -= bin_width; }
            histsize = k+2;

/*
  if (printinterval > 0) {
  printf("\n -- initializing histogram with %d bins (min:%.2f w:%.2f [samples]) --\n", histsize, bin_min, bin_width);
  }
*/
            histogram = calloc(histsize + 1,sizeof(unsigned int));
            for (j = 0; j < HISTLEN; ++j) {
                int bin = RAIL(floor(((double)history[j] - bin_min) / bin_width), 0, histsize);
                histogram[bin]++;
            }
        } else {
            int bin = RAIL(floor(((double)delay - bin_min) / bin_width), 0, histsize);
            histogram[bin]++;
        }

        cnt_a++;
    }

    if (strlen(output)) {
        FILE *fp = fopen(output, "w");

        if (!fp) {
            fatal("unable to open output file '%s'", output);
        }

        for (i = 0; i < cnt_a; ++i) {
            fprintf(fp, "%8.2f\n", delays[i]);
        }

        fclose(fp);
    }

    if (!err) {
        printf("\n> done.\n\n");
    } else {
        printf("\n> done (with errors).\n\n");
    }

    if (histsize > 0) {
        printf("> latency distribution:\n\n");
        int i,j;
        int binlevel = 0;
        for (i = 0; i < histsize; ++i) {
            if (histogram[i] > binlevel) binlevel = histogram[i];
        }

        if (binlevel > 0) {
            int dig = digits(max_a); char fmt[256];
            snprintf(fmt, sizeof(fmt), " %%%d.2f .. %%%d.2f [ms]: %%%dd ", dig + 4, dig + 4, digits(cnt_a));
            for (i = 0; i <= histsize; ++i) {
                double hmin, hmax;
                if (i == 0) {
                    hmin = 0.0;
                    hmax = bin_min;
                } else if (i == histsize) {
                    hmin = bin_min + (double)(i-1) * bin_width;
                    hmax = INFINITY;
                } else {
                    hmin = bin_min + (double)(i-1) * bin_width;
                    hmax = bin_min + (double)(i) * bin_width;
                }
                printf(fmt, hmin, hmax, histogram[i]);
                int bar_width = (histogram[i] * TERMWIDTH ) / binlevel;
                if (bar_width == 0 && histogram[i] > 0) bar_width = 1;
                for (j = 0; j < bar_width; ++j) printf("#");
                printf("\n");
            }
        }

        printf("\n");
        printf(" best    latency was %.2f ms\n", min_a);
        printf(" worst   latency was %.2f ms\n", max_a);
        printf(" average latency was %.2f ms\n", avg_a / (double)cnt_a);
        printf("\n");
    }

    free(histogram);
    free(history);

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
