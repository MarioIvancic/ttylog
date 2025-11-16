/* ttylog - serial port logger
 Copyright (C) 1999-2002  Tibor Koleszar <oldw@debian.org>
 Copyright (C) 2008-2018  Robert James Clay <jame@rocasa.us>
 Copyright (C) 2017-2018  Guy Shapiro <guy.shapiro@mobi-wize.com>
 Copyright (C)      2016  Donald Gordon <donald@tawherotech.nz>
 Copyright (C)      2016  Alexander (MrMontag) Fust <MrMontagOpenDev@gmail.com>
 Copyright (C)      2016  Logan Rosen <loganrosen@gmail.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <sys/stat.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>

#include "config.h"

/* #define DEBUG 1 */


/* Constants for output format. */
enum
{
  FMT_ACSII = 0,  /* Old ttylog ascii output format. */
  FMT_HEX_LC = 1, /* HEX output using lowercase abcdef characters. */
  FMT_HEX_UC = 2, /* HEX output using uppercase ABCDEF characters. */
  FMT_RAW = 3,    /* Raw output format, EOL character is not added by ttylog. */
};


/* Constants for timestamp format. */
enum
{
  FMT_OLD = 1,  /* Old timestamp format, like Mon Oct 20 21:13:53 2025. */
  FMT_ISO = 2,  /* ISO8601 timestamp format, YYYY-MM-DDTHH:mm:ss.sss. */
  FMT_MS = 3,   /* Relative time in milliseconds from program start. */
  FMT_US = 4,   /* Relative time in microseconds from program start. */
};


typedef struct
{
  char* work_buff;
  int line_len_limit;
  int line_len;
} print_data_ctx_t;


/* Function that prints line in specified output format. Timestamp is optional.
   Buffer should be at least 4 times the line length. */
void print_data(const char* raw_data, int raw_data_len, print_data_ctx_t* ctx, const char* time_stamp, int fmt);


/* Function to create timestamp according to timestamp format fmt. */
const char* make_timestamp(int fmt, const struct timespec* start_time);

/* Select baud rate based on user input. */
int select_baud_rate(const char* baud_str);

#ifdef DEBUG
FILE* debug_file;
#endif // DEBUG

int
main (int argc, char *argv[])
{
  FILE *logfile;
  fd_set rfds;
  int retval;
  int i;
  speed_t baud = 0;
  int stamp = 0;
  int fd;
  char raw_data[1024];
  char buffer[4 * sizeof(raw_data)];
  char modem_device[512];
  struct termios oldtio, newtio;
  const char* timestr;
  int output_fmt = FMT_ACSII;
  const char* baud_str = NULL;
  int data_bits = 8;  /* 7 or 8 data bits. */
  int stop_bits = 1;  /* 1 or 2 stop bits. */
  int parity = 'N';   /* No parity (N), Even (E), Odd (O), Mark (M) or Space (S) */
  const char* port_mode = "8N1";
  struct timespec startup_timestamp;
  int rts = -1;
  int dtr = -1;
  int timeout = 0;
  int run_time = 0;
  print_data_ctx_t print_data_ctx;

  print_data_ctx.work_buff = buffer;
  print_data_ctx.line_len_limit = sizeof(raw_data) - 1;
  print_data_ctx.line_len = 0;

  clock_gettime(CLOCK_MONOTONIC, &startup_timestamp);

  memset (modem_device, '\0', sizeof(modem_device));

#ifdef DEBUG
  debug_file = fopen ("debug-out.txt", "a");
#endif // DEBUG

  if (argc < 2)
    {
      fprintf (stderr, "%s: no params. try %s -h\n", argv[0], argv[0]);
      exit (0);
    }

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h") || !strcmp (argv[i], "--help"))
        {
          fprintf (stderr, "ttylog version %s\n", TTYLOG_VERSION);
          fprintf (stderr, "Usage:  ttylog [-b|--baud] [-m|--mode] [-d|--device] [-s|--stamp] [-t|--timeout] [-F|--format] [-l|--limit] [--rts] [--dtr] > /path/to/logfile\n");
          fprintf (stderr, " -h, --help     This help\n");
          fprintf (stderr, " -v, --version  Version number\n");
          fprintf (stderr, " -b, --baud     Baud rate\n");
          fprintf (stderr, " -m, --mode     Serial port mode (default: 8N1)\n");
          fprintf (stderr, " -d, --device   Serial device (eg. /dev/ttyS1)\n");
          fprintf (stderr, " -s, --stamp    Prefix each line with datestamp (old, iso, ms, us)\n");
          fprintf (stderr, " -t, --timeout  How long to run, in seconds.\n");
          fprintf (stderr, " -F, --format   Set output format to one of a[scii] (default), h[ex], H[EX], r[aw].\n");
          fprintf (stderr, " -l, --limit    Limit line length.\n");
          fprintf (stderr, " --rts          Set RTS line state (0 or 1).\n");
          fprintf (stderr, " --dtr          Set DTR line state (0 or 1).\n");
          fprintf (stderr, "ttylog home page: <http://ttylog.sourceforge.net/>\n\n");
          exit (0);
        }
      else if (!strcmp (argv[i], "-v") || !strcmp (argv[i], "--version"))
        {
          fprintf (stderr, "ttylog version %s\n", TTYLOG_VERSION);
          fprintf (stderr, "Copyright (C) 2018 Robert James Clay <jame@rocasa.us>\n");
          fprintf (stderr, "Copyright (C) 2018 Guy Shapiro <guy.shapiro@mobi-wize.com>\n");
          fprintf (stderr, "Copyright (C) 2016 Donald Gordon <donald@tawherotech.nz>\n");
          fprintf (stderr, "Copyright (C) 2016 Logan Rosen <loganrosen@gmail.com>\n");
          fprintf (stderr, "Copyright (C) 2016 Alexander (MrMontag) Fust <alexander.fust.info@gmail.com>\n");
          fprintf (stderr, "Copyright (C) 2002 Tibor Koleszar <oldw@debian.org>\n");
          fprintf (stderr, "License GPLv2+: <http://www.gnu.org/licenses/old-licenses/gpl-2.0.html>\n");
          fprintf (stderr, "This is free software: you are free to change and redistribute it.\n");
          fprintf (stderr, "There is NO WARRANTY, to the extent permitted by law.\n\n");
          exit (0);
        }
      else if (!strcmp (argv[i], "-s") || !strcmp (argv[i], "--stamp"))
        {
          /* Next token is optional. */
          if ((i + 1) >= argc) { stamp = FMT_OLD; }
          else
            {
              const char* fmt = argv[i + 1];

              if (fmt[0] == '-') { stamp = FMT_OLD; }
              else
                {
                  i++;

                  if (!strcmp(fmt, "old")) { stamp = FMT_OLD; }
                  else if (!strcmp(fmt, "iso")) { stamp = FMT_ISO; }
                  else if (!strcmp(fmt, "ms")) { stamp = FMT_MS; }
                  else if (!strcmp(fmt, "us")) { stamp = FMT_US; }
                  else
                    {
                      fprintf (stderr, "%s: invalid timestamp format '%s'\n", argv[0], fmt);
                      exit (0);
                    }
                }
            }
#ifdef DEBUG
          fprintf(debug_file, "Using timestamp format %d\n", stamp);
          fflush(debug_file);
#endif // DEBUG
        }
      else if (!strcmp (argv[i], "-b") || !strcmp (argv[i], "--baud"))
        {
          if ((i + 1) >= argc)
            {
              fprintf (stderr, "%s: baud rate not is specified\n", argv[0]);
              exit (0);
            }

          baud_str = argv[i + 1];
          i++;
          baud = select_baud_rate(baud_str);
#ifdef DEBUG
          fprintf(debug_file, "Using baudrate of %d bps\n", baud);
          fflush(debug_file);
#endif // DEBUG
        }
      else if (!strcmp (argv[i], "-d") || !strcmp (argv[i], "--device"))
        {
          if ((i + 1) >= argc)
            {
              fprintf (stderr, "%s: serial device is not specified\n", argv[0]);
              exit(0);
            }

          strncpy (modem_device, argv[i + 1], sizeof(modem_device) - 1);
          i++;
#ifdef DEBUG
          fprintf(debug_file, "Using serial port %s\n", modem_device);
          fflush(debug_file);
#endif // DEBUG
        }
      else if (!strcmp (argv[i], "-t") || !strcmp (argv[i], "--timeout"))
        {
          if ((i + 1) >= argc)
            {
              fprintf (stderr, "%s: invalid time span\n", argv[0]);
              exit(0);
            }

          timeout = atoi(argv[i + 1]);
          if (!timeout)
            {
              fprintf (stderr, "%s: invalid time span %s\n", argv[0], argv[i + 1]);
              exit(0);
            }

          i++;
#ifdef DEBUG
          fprintf(debug_file, "Using timeout value of %d s\n", timeout);
          fflush(debug_file);
#endif // DEBUG
        }
      else if (!strcmp (argv[i], "-F") || !strcmp (argv[i], "--format"))
        {
          if ((i + 1) >= argc)
          {
            fprintf (stderr, "%s: output format is not specified\n", argv[0]);
            exit(0);
          }

          int f = argv[i + 1][0];
          if(f == 'a') output_fmt = FMT_ACSII;
          else if(f == 'h') { output_fmt = FMT_HEX_LC; }
          else if(f == 'H') { output_fmt = FMT_HEX_UC; }
          else if(f == 'r') { output_fmt = FMT_RAW; }
          else
            {
              fprintf (stderr, "%s: invalid output format '%s'\n", argv[0], argv[i + 1]);
              exit(0);
            }
          i++;

#ifdef DEBUG
          fprintf(debug_file, "Using output format %d s\n", output_fmt);
          fflush(debug_file);
#endif // DEBUG
        }
      else if (!strcmp (argv[i], "-l") || !strcmp (argv[i], "--limit"))
        {
          if ((i + 1) >= argc)
          {
            fprintf (stderr, "%s: line length limit is not specified\n", argv[0]);
            exit(0);
          }

          print_data_ctx.line_len_limit = atoi(argv[i + 1]);
          if (!print_data_ctx.line_len_limit)
          {
            fprintf (stderr, "%s: invalid line length limit %s\n", argv[0], argv[i + 1]);
            exit(0);
          }

          i++;

#ifdef DEBUG
          fprintf(debug_file, "Using line length limit of %d bytes\n", print_data_ctx.line_len_limit);
          fflush(debug_file);
#endif // DEBUG
        }
      else if (!strcmp (argv[i], "-m") || !strcmp (argv[i], "--mode"))
        {
          if ((i + 1) >= argc)
          {
            fprintf (stderr, "%s: serial port mode is not specified\n", argv[0]);
            exit(0);
          }

          port_mode = argv[i + 1];
          i++;

          if(port_mode[0] == '7') { data_bits = 7; }
          else if(port_mode[0] == '8') { data_bits = 8; }
          else
            {
              fprintf (stderr, "%s: invalid serial port mode %s: invalid data bits.\n", argv[0], port_mode);
              exit(0);
            }

          if(port_mode[1] == 'N') { parity = 'N'; }
          else if(port_mode[1] == 'E') { parity = 'E'; }
          else if(port_mode[1] == 'O') { parity = 'O'; }
          else if(port_mode[1] == 'M') { parity = 'M'; }
          else if(port_mode[1] == 'S') { parity = 'S'; }
          else
            {
              fprintf (stderr, "%s: invalid serial port mode %s: invalid parity.\n", argv[0], port_mode);
              exit(0);
            }

          if(port_mode[2] == '1') { stop_bits = 1; }
          else if(port_mode[2] == '2') { stop_bits = 2; }
          else
            {
              fprintf (stderr, "%s: invalid serial port mode %s: invalid stop bits.\n", argv[0], port_mode);
              exit(0);
            }

#ifdef DEBUG
          fprintf(debug_file, "Using serial port mode %s (%d data bits, %d stop bits, parity: %c\n", port_mode, data_bits, stop_bits, parity);
          fflush(debug_file);
#endif // DEBUG
        }
      else if (!strcmp (argv[i], "--rts"))
        {
          if ((i + 1) >= argc)
            {
              fprintf (stderr, "%s: RTS line state is not specified\n", argv[0]);
              exit(0);
            }

          i++;
          if(argv[i][0] == '0') { rts = 0; }
          else if(argv[i][0] == '1') { rts = 1; }
          else
            {
              fprintf (stderr, "%s: invalid RTS line state '%s'\n", argv[0], argv[i]);
              exit(0);
            }

#ifdef DEBUG
          fprintf(debug_file, "Using RTS value %d\n", rts);
          fflush(debug_file);
#endif // DEBUG
        }
      else if (!strcmp (argv[i], "--dtr"))
        {
          if ((i + 1) >= argc)
            {
              fprintf (stderr, "%s: DTR line state is not specified\n", argv[0]);
              exit(0);
            }

          i++;
          if(argv[i][0] == '0') { dtr = 0; }
          else if(argv[i][0] == '1') { dtr = 1; }
          else
            {
              fprintf (stderr, "%s: invalid DTR line state '%s'\n", argv[0], argv[i]);
              exit(0);
            }

#ifdef DEBUG
          fprintf(debug_file, "Using DTR value %d\n", dtr);
          fflush(debug_file);
#endif // DEBUG
        }
    }

  if (baud_str == NULL)
    {
      fprintf (stderr, "%s: baud rate is not specified\n", argv[0]);
      exit (0);
    }

  if (baud == 0)
    {
      fprintf (stderr, "%s: invalid baud rate %s\n", argv[0], baud_str);
      exit (0);
    }

  if (!modem_device[0]) {
    fprintf (stderr, "%s: no device is set. Use %s -h for more information.\n", argv[0], argv[0]);
    exit (0);
  }

  logfile = fopen (modem_device, "rb");
  if (logfile == NULL)
    {
      fprintf (stderr, "%s: invalid device %s\n", argv[0], modem_device);
      exit (0);
    }
  fd = fileno (logfile);

#ifdef DEBUG
  fprintf(debug_file, "Opened serial port %s, file descriptor %d\n", modem_device, fd);
  fflush(debug_file);
#endif // DEBUG

  /* Check are we connected to serial port and if yes, save current serial port settings */
  int serial_port = (0 == tcgetattr (fd, &oldtio));
  if(serial_port)
    {
      memset (&newtio, 0, sizeof (newtio)); /* clear struct for new port settings */

#ifdef DEBUG
      fprintf(debug_file, "Connected to real serial device, not file\n");
      fflush(debug_file);
#endif // DEBUG

      /* Enable RTS/CTS (hardware) flow control. */
      /* newtio.c_cflag |= CRTSCTS; */

      /* Character size mask. */
      if(data_bits == 7) { newtio.c_cflag |= CS7; }
      else { newtio.c_cflag |= CS8; }

      /* Ignore modem control lines. */
      newtio.c_cflag |= CLOCAL;

      /* Enable receiver. */
      newtio.c_cflag |= CREAD;

      /* Set stop bits. */
      if(stop_bits == 2) { newtio.c_cflag |= CSTOPB; }

      if(parity == 'E')
        {
          newtio.c_cflag &= ~(PARODD | CMSPAR);
          newtio.c_cflag |= PARENB;
        }
      else if(parity == 'O')
        {
          newtio.c_cflag |= PARENB | PARODD;
          newtio.c_cflag &= ~CMSPAR;
        }
      else if(parity == 'M')
        {
          newtio.c_cflag |= PARENB | PARODD | CMSPAR;
        }
      else if(parity == 'S')
        {
          newtio.c_cflag |= PARENB | CMSPAR;
          newtio.c_cflag &= ~PARODD;
        }

      /* Ignore framing errors and parity errors. */
      newtio.c_iflag |= IGNPAR;

      if(output_fmt == FMT_ACSII)
        {
          /* Ignore carriage return on input. */
          newtio.c_iflag |= IGNCR;
        }

      /* Ignore BREAK condition on input. */
      newtio.c_iflag |= IGNBRK;

      newtio.c_oflag = 0;

      if(output_fmt == FMT_ACSII)
        {
          /* Enable canonical mode. */
          newtio.c_lflag = ICANON;
        }

      /* Set blocking read, no timeouts. */
      newtio.c_cc[VTIME] = 0;
      newtio.c_cc[VMIN] = 0;

      /* Only truly portable method of setting speed. */
      cfsetispeed (&newtio, baud);
      cfsetospeed (&newtio, baud);

      tcflush (fd, TCIFLUSH);
      tcsetattr (fd, TCSANOW, &newtio);

      if(rts >= 0)
        {
          int flags = TIOCM_RTS;
          if(rts) { ioctl(fd, TIOCMBIS, &flags); }
          else { ioctl(fd, TIOCMBIC, &flags); }
        }

      if(dtr >= 0)
        {
          int flags = TIOCM_DTR;
          if(rts) { ioctl(fd, TIOCMBIS, &flags); }
          else { ioctl(fd, TIOCMBIC, &flags); }
        }

      /* Set low latency flag. Note that not all serial drivers support this feature. */
#if defined(ASYNC_LOW_LATENCY)
      {
        struct serial_struct serial;
        ioctl(fd, TIOCGSERIAL, &serial);
        serial.flags |= ASYNC_LOW_LATENCY;
        ioctl(fd, TIOCSSERIAL, &serial);
      }
#endif // defined

      /* Clear the device */
      {
        int flags = fcntl (fd, F_GETFL, 0);
        fcntl (fd, F_SETFL, flags | O_NONBLOCK);
        while (fread (raw_data, 1, sizeof(raw_data), logfile) > 0 );
        fcntl (fd, F_SETFL, flags);
      }
    }

  struct timeval select_timeout;

  while (1)
    {
      FD_ZERO (&rfds);
      FD_SET (fd, &rfds);
      if(timeout)
        {
          select_timeout.tv_sec = 1;
          select_timeout.tv_usec = 0;
          retval = select (fd + 1, &rfds, NULL, NULL, &select_timeout);
        }
      else
        {
          retval = select (fd + 1, &rfds, NULL, NULL, NULL);
        }

      if (retval > 0)
        {
          ssize_t len = 0;
          if(output_fmt == FMT_ACSII)
            {
              if (!fgets (raw_data, sizeof(raw_data), logfile))
                {
                  raw_data[0] = 0;
                  if (ferror (logfile))
                    {
                      fprintf (stderr, "%s: error reading serial device %s\n", argv[0], modem_device);
                      break;
                    }
                  /* Used with files, for testing. */
                  if (!serial_port && feof (logfile)) { break; }
                }
              len = strlen(raw_data);
            }
          else
            {
              len = read (fd, raw_data, sizeof(raw_data) - 1);
              if (len < 0)
                {
                  int err = errno;
                  raw_data[0] = 0;
                  if (err == EAGAIN || err == EWOULDBLOCK)
                    {
                      continue;
                    }
                  else
                    {
                      fprintf (stderr, "%s: error %d while reading serial device %s\n", argv[0], err, modem_device);
                      break;
                    }
                }
              else if(len > 0)
                {
                  raw_data[len] = 0;
                }
              else
                {
                  /* EOF */
                  break;
                }
            }

          if(len)
            {
              if (stamp) { timestr = make_timestamp(stamp, &startup_timestamp); }
              else { timestr = NULL; }

              print_data(raw_data, len, &print_data_ctx, timestr, output_fmt);
            }
        }
      else if (retval == 0) /* Timeout. */
        {
          if(timeout && (run_time >= timeout) ) break;
          else if(timeout) { run_time++; }
        }
      else
        {
          fprintf (stderr, "%s: error in select call\n", argv[0]);
          break;
        }
    }

  fclose (logfile);
  if(serial_port) { tcsetattr (fd, TCSANOW, &oldtio); }
  return 0;
}


/* Function that prints line in specified output format. Timestamp is optional.
   Buffer should be at least 4 times the line length. */
void print_data(const char* raw_data, int raw_data_len, print_data_ctx_t* ctx, const char* time_stamp, int fmt)
{
  static const char* hex_chars_lc = "0123456789abcdef";
  static const char* hex_chars_uc = "0123456789ABCDEF";

  int offset = 0;

#ifdef DEBUG
  fprintf(debug_file, "print_data(len=%d, line_len=%d, line_len_limit=%d)\n", raw_data_len, ctx->line_len, ctx->line_len_limit);
  fprintf(debug_file, "data: '%s'\n", raw_data);
  fflush(debug_file);
#endif // DEBUG

  if(fmt == FMT_ACSII)
    {
      while(raw_data_len)
        {
          if (time_stamp) { printf ("[%s] ", time_stamp); }

          int print_nl = 0;
          int len = ctx->line_len_limit - ctx->line_len;
          if(len > raw_data_len) { len = raw_data_len; }
          else { print_nl = 1; }
          memcpy(ctx->work_buff, raw_data + offset, len);
          offset += len;
          ctx->line_len += len;
          raw_data_len -= len;
          if(ctx->line_len >= ctx->line_len_limit) { ctx->line_len = 0; }

          if(print_nl)
            {
              ctx->work_buff[len] = '\n';
              ctx->work_buff[len + 1] = 0;
            }
          else
            {
              ctx->work_buff[len] = 0;
            }

#ifdef DEBUG
          fprintf(debug_file, "workbuff: '%s'\n", ctx->work_buff);
          fflush(debug_file);
#endif // DEBUG

          fputs (ctx->work_buff, stdout);
          fflush(stdout);
        }
    }
  if(fmt == FMT_RAW)
    {
      while(raw_data_len)
        {
          if (time_stamp)
            {
              if(ctx->line_len != 0) { fputs("\n", stdout); }
              printf ("[%s] ", time_stamp);
              ctx->line_len = 0;
            }

          int print_nl = 0;
          int len = ctx->line_len_limit - ctx->line_len;
          if(len > raw_data_len) { len = raw_data_len; }
          else { print_nl = 1; }
          memcpy(ctx->work_buff, raw_data + offset, len);
          offset += len;
          ctx->line_len += len;
          raw_data_len -= len;
          if(ctx->line_len >= ctx->line_len_limit) { ctx->line_len = 0; }

          if(print_nl)
            {
              ctx->work_buff[len] = '\n';
              ctx->work_buff[len + 1] = 0;
            }
          else
            {
              ctx->work_buff[len] = 0;
            }

#ifdef DEBUG
          fprintf(debug_file, "workbuff: '%s'\n", ctx->work_buff);
          fflush(debug_file);
#endif // DEBUG

          fputs(ctx->work_buff, stdout);
          fflush(stdout);
        }
    }
  else if(fmt == FMT_HEX_LC || fmt == FMT_HEX_UC)
    {
      const char* hex_chars = (fmt == FMT_HEX_LC) ? hex_chars_lc : hex_chars_uc;
      while(raw_data_len)
      {
        if (time_stamp)
          {
            if(ctx->line_len != 0) { fputs("\n", stdout); }
            printf ("[%s] ", time_stamp);
            ctx->line_len = 0;
          }

        int print_nl = 0;
        int len = ctx->line_len_limit - ctx->line_len;
        if(len > raw_data_len) { len = raw_data_len; }
        else { print_nl = 1; }

        int buff_len = 0;
        for(int i = 0; i < len; i++)
          {
              unsigned char d = raw_data[offset + i];
              if (i || ctx->line_len) { ctx->work_buff[buff_len++] = ' '; }
              ctx->work_buff[buff_len++] = hex_chars[(d >> 4) & 0x0F];
              ctx->work_buff[buff_len++] = hex_chars[d & 0x0F];
          }

        if(print_nl)
          {
            ctx->work_buff[buff_len] = '\n';
            ctx->work_buff[buff_len + 1] = 0;
          }
        else
          {
            ctx->work_buff[buff_len] = 0;
          }
        offset += len;
        ctx->line_len += len;
        raw_data_len -= len;
        if(ctx->line_len >= ctx->line_len_limit) { ctx->line_len = 0; }

#ifdef DEBUG
        fprintf(debug_file, "workbuff: '%s'\n", ctx->work_buff);
        fflush(debug_file);
#endif // DEBUG

        fputs(ctx->work_buff, stdout);
        fflush(stdout);
      }
    }
}


/* Function to create timestamp according to timestamp format fmt. */
const char* make_timestamp(int fmt, const struct timespec* start_time)
{
  char* timestr = NULL;
  static char buffer[128];

  if(fmt == FMT_OLD)
    {
      time_t rawtime;
      struct tm *timeinfo;
      time(&rawtime);
      timeinfo = localtime(&rawtime);
      timestr = asctime(timeinfo);
      timestr[strlen(timestr) - 1] = 0;
    }
  else if(fmt == FMT_ISO)
    {
      /* YYYY-MM-DDTHH:MM:SS.sss */
      struct timespec ts;
      struct tm TM;
      unsigned ms;
      ts.tv_sec = 0;
      ts.tv_nsec = 0;
      clock_gettime(CLOCK_REALTIME, &ts);
      TM = *localtime(&ts.tv_sec);
      ms = ts.tv_nsec / 1000000UL;
      snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
            TM.tm_year + 1900, TM.tm_mon + 1, TM.tm_mday,
            TM.tm_hour, TM.tm_min, TM.tm_sec, ms);

      timestr = buffer;
    }
  else if(fmt == FMT_MS)
    {
      /* 32-bit number of milliseconds. */
      struct timespec ts;
      uint64_t ms = 0;
      ts.tv_sec = 0;
      ts.tv_nsec = 0;
      clock_gettime(CLOCK_MONOTONIC, &ts);

      /* Subtract start time from current time. */
      ts.tv_sec -= start_time->tv_sec;
      ts.tv_nsec -= start_time->tv_nsec;
      if(ts.tv_nsec < 0)
        {
          ts.tv_sec--;
          ts.tv_nsec += 1000000000LL;
        }

      /* Convert to milliseconds. */
      ms = ts.tv_nsec / 1000000UL;
      ms += ts.tv_sec * 1000U;
      ms %= 1000000000ULL;   /* 9 decimal digits. */
      snprintf(buffer, sizeof(buffer), "%09" PRIu64, ms);

      /* Insert dots after every 3 digits. 000000136 => 000.000.136 */
      /*                                   012345678    01234567890 */
      buffer[11] = 0;
      buffer[10] = buffer[8];
      buffer[9] = buffer[7];
      buffer[8] = buffer[6];
      buffer[7] = '.';
      buffer[6] = buffer[5];
      buffer[5] = buffer[4];
      buffer[4] = buffer[3];
      buffer[3] = '.';

      timestr = buffer;
    }
  else if(fmt == FMT_US)
    {
      /* 64-bit number of microseconds. */
      struct timespec ts;
      uint64_t us = 0;
      ts.tv_sec = 0;
      ts.tv_nsec = 0;
      clock_gettime(CLOCK_MONOTONIC, &ts);

      /* Subtract start time from current time. */
      ts.tv_sec -= start_time->tv_sec;
      ts.tv_nsec -= start_time->tv_nsec;
      if(ts.tv_nsec < 0)
        {
          ts.tv_sec--;
          ts.tv_nsec += 1000000000LL;
        }

      /* Convert to microseconds. */
      us = ts.tv_nsec / 1000UL;
      us += ts.tv_sec * 1000000UL;
      us %= 1000000000000ULL;   /* 12 decimal digits. */
      snprintf(buffer, sizeof(buffer), "%012" PRIu64, us);

      /* Insert dots after every 3 digits. 000000000136 => 000.000.000.136 */
      /*                                   012345678901    012345678901234 */
      buffer[15] = 0;
      buffer[14] = buffer[11];
      buffer[13] = buffer[10];
      buffer[12] = buffer[9];
      buffer[11] = '.';
      buffer[10] = buffer[8];
      buffer[9] = buffer[7];
      buffer[8] = buffer[6];
      buffer[7] = '.';
      buffer[6] = buffer[5];
      buffer[5] = buffer[4];
      buffer[4] = buffer[3];
      buffer[3] = '.';

      timestr = buffer;
    }

  return timestr;
}


int select_baud_rate(const char* baud_str)
{
	long long b = strtoll(baud_str, NULL, 10);
	int baud = 0;
  switch(b)
    {
      case 300: baud = B300; break;
      case 600: baud = B600; break;
      case 1200: baud = B1200; break;
      case 2400: baud = B2400; break;
      case 4800: baud = B4800; break;
      case 9600: baud = B9600; break;
      case 19200: baud = B19200; break;
#if defined(B28800)
      case 28800: baud = B28800; break;
#endif // defined
      case 38400: baud = B38400; break;
      case 57600: baud = B57600; break;
#if defined(B115200)
      case 115200: baud = B115200; break;
#endif // defined
#if defined(B230400)
      case 230400: baud = B230400; break;
#endif // defined
#if defined(B460800)
      case 460800: baud = B460800; break;
#endif // defined
#if defined(B500000)
      case 500000: baud = B500000; break;
#endif // defined
#if defined(B576000)
      case 576000: baud = B576000; break;
#endif // defined
#if defined(B921600)
      case 921600: baud = B921600; break;
#endif // defined
#if defined(B1000000)
      case 1000000: baud = B1000000; break;
#endif // defined
#if defined(B1152000)
      case 1152000: baud = B1152000; break;
#endif // defined
#if defined(B1500000)
      case 1500000: baud = B1500000; break;
#endif // defined
#if defined(B2000000)
      case 2000000: baud = B2000000; break;
#endif // defined
#if defined(B2500000)
      case 2500000: baud = B2500000; break;
#endif // defined
#if defined(B3000000)
      case 3000000: baud = B3000000; break;
#endif // defined
#if defined(B3500000)
      case 3500000: baud = B3500000; break;
#endif // defined
#if defined(B4000000)
      case 4000000: baud = B4000000; break;
#endif // defined
    }

  return baud;
}
