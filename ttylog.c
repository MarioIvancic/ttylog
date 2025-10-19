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
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "config.h"

#define BAUDN 13

/* Constants for output format. */
enum
{
  FMT_ACSII = 0,  /* Old ttylog ascii output format. */
  FMT_HEX_LC = 1, /* HEX output using lowercase abcdef characters. */
  FMT_HEX_UC = 2, /* HEX output using uppercase ABCDEF characters. */
  FMT_RAW = 3,    /* Raw output format, EOL character is not added by ttylog. */
};

/* Function that prints line in specified output format. Timestamp is optional.
   Buffer should be at least 4 times the line length. */
void print_line(char* line, int line_len, char* buffer, const char* time_stamp, int fmt);

char flush = 0;

const char* BAUD_T[] =
{"300", "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600", "2000000"};

int BAUD_B[] =
{B300, B1200, B2400, B4800, B9600, B19200, B38400, B57600, B115200, B230400, B460800, B921600, B2000000};

int
main (int argc, char *argv[])
{
  FILE *logfile;
  fd_set rfds;
  int retval, i, j, baud = -1;
  int stamp = 0;
  timer_t timerid;
  struct sigevent sevp;
  sevp.sigev_notify = SIGEV_SIGNAL;
  sevp.sigev_signo = SIGINT;
  int fd;
  char line[1024];
  char buffer[4 * sizeof(line)];
  char modem_device[512];
  struct termios oldtio, newtio;
  time_t rawtime;
  struct tm *timeinfo;
  char *timestr;
  int output_fmt = FMT_ACSII;
  int line_len_limit = sizeof(line) - 1;
  const char* baud_str = NULL;

  memset (modem_device, '\0', sizeof(modem_device));

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
          fprintf (stderr, "Usage:  ttylog [-b|--baud] [-d|--device] [-f|--flush] [-s|--stamp] [-t|--timeout] [-F|--format] [-l|--limit] > /path/to/logfile\n");
          fprintf (stderr, " -h, --help     This help\n");
          fprintf (stderr, " -v, --version  Version number\n");
          fprintf (stderr, " -b, --baud     Baud rate\n");
          fprintf (stderr, " -d, --device   Serial device (eg. /dev/ttyS1)\n");
          fprintf (stderr, " -f, --flush    Flush output\n");
          fprintf (stderr, " -s, --stamp    Prefix each line with datestamp\n");
          fprintf (stderr, " -t, --timeout  How long to run, in seconds.\n");
          fprintf (stderr, " -F, --format   Set output format to one of a[scii] (default), h[ex], H[EX], r[aw].\n");
          fprintf (stderr, " -l, --limit    Limit line length.\n");
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
      else if (!strcmp (argv[i], "-f") || !strcmp (argv[i], "--flush"))
        {
          flush = 1;
        }
      else if (!strcmp (argv[i], "-s") || !strcmp (argv[i], "--stamp"))
        {
          stamp = 1;
        }
      else if (!strcmp (argv[i], "-b") || !strcmp (argv[i], "--baud"))
        {
          if ((i + 1) >= argc)
            {
              fprintf (stderr, "%s: baud rate not specified\n", argv[0]);
              exit (0);
            }

          baud_str = argv[i + 1];
          i++;
          for (j = 0; j < BAUDN; j++)
          {
            if (!strcmp (baud_str, BAUD_T[j]))
              {
                baud = j;
                break;
              }
          }
        }
      else if (!strcmp (argv[i], "-d") || !strcmp (argv[i], "--device"))
        {
          if ((i + 1) >= argc)
            {
              fprintf (stderr, "%s: serial device not specified\n", argv[0]);
              exit(0);
            }

          strncpy (modem_device, argv[i + 1], sizeof(modem_device) - 1);
          i++;
        }
      else if (!strcmp (argv[i], "-t") || !strcmp (argv[i], "--timeout"))
        {
          if ((i + 1) >= argc)
            {
              fprintf (stderr, "%s: invalid time span\n", argv[0]);
              exit(0);
            }
          if (timer_create (CLOCK_REALTIME, &sevp, &timerid) == -1)
            {
              fprintf (stderr, "%s: unable to create timer: %s\n", argv[0], strerror(errno));
              exit (0);
            }
          struct itimerspec new_value;
          new_value.it_interval.tv_sec = 0;
          new_value.it_interval.tv_nsec = 0;
          int sec = atoi(argv[i + 1]);
          if (!sec)
            {
              fprintf (stderr, "%s: invalid time span %s\n", argv[0], argv[i + 1]);
              exit(0);
            }
          new_value.it_value.tv_sec = atoi(argv[i + 1]);
          new_value.it_value.tv_nsec = 0;
          if (timer_settime(timerid, 0, &new_value, NULL) == -1)
            {
              fprintf (stderr, "%s: unable to set timer time: %s\n", argv[0], strerror(errno));
              exit (0);
            }

          i++;
        }
      else if (!strcmp (argv[i], "-F") || !strcmp (argv[i], "--format"))
        {
          if ((i + 1) >= argc)
          {
            fprintf (stderr, "%s: output format not specified\n", argv[0]);
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
        }
      else if (!strcmp (argv[i], "-l") || !strcmp (argv[i], "--limit"))
        {
          if ((i + 1) >= argc)
          {
            fprintf (stderr, "%s: line length limit not specified\n", argv[0]);
            exit(0);
          }

          int len = atoi(argv[i + 1]);
          if (!len)
          {
            fprintf (stderr, "%s: invalid line length limit %s\n", argv[0], argv[i + 1]);
            exit(0);
          }

          line_len_limit = len;
          i++;
        }
    }

  if (baud_str == NULL)
    {
      fprintf (stderr, "%s: baud rate not specified\n", argv[0]);
      exit (0);
    }

  if (baud == -1)
    {
      fprintf (stderr, "%s: invalid baud rate %s\n", argv[0], baud_str);
      exit (0);
    }

  if (!strlen(modem_device)) {
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

  /* Check are we connected to serial port and if yes, save current serial port settings */
  int serial_port = (0 == tcgetattr (fd, &oldtio));
  if(serial_port)
    {
      bzero (&newtio, sizeof (newtio)); /* clear struct for new port settings */

      newtio.c_cflag = CRTSCTS | CS8 | CLOCAL | CREAD;
      newtio.c_iflag = IGNPAR | IGNCR;
      newtio.c_oflag = 0;
      newtio.c_lflag = ICANON;
      /* Only truly portable method of setting speed. */
      cfsetispeed (&newtio, BAUD_B[baud]);
      cfsetospeed (&newtio, BAUD_B[baud]);

      tcflush (fd, TCIFLUSH);
      tcsetattr (fd, TCSANOW, &newtio);

      /* Clear the device */
      {
        int flags = fcntl (fd, F_GETFL, 0);
        fcntl (fd, F_SETFL, flags | O_NONBLOCK);
        while (fread (line, 1, sizeof(line), logfile) > 0 );
        fcntl (fd, F_SETFL, flags);
      }
    }

  if (flush)
    setbuf(stdout, NULL);

  while (1)
    {
      FD_ZERO (&rfds);
      FD_SET (fd, &rfds);
      retval = select (fd + 1, &rfds, NULL, NULL, NULL);
      if (retval > 0)
        {
          size_t len = 0;
          if(output_fmt == FMT_ACSII)
            {
              if (!fgets (line, line_len_limit + 1, logfile))
                {
                  if (ferror (logfile)) { break; }
                  if (feof (logfile)) { break; } /* Used with files, for testing. */
                }
            } else {
              len = fread (line, 1, line_len_limit, logfile);
              if (!len)
                {
                  if (ferror (logfile)) { break; }
                  if (feof (logfile)) { break; }
                }
              line[len - 1] = 0;
            }

            if (stamp)
              {
                time(&rawtime);
                timeinfo = localtime(&rawtime);
                timestr = asctime(timeinfo);
                timestr[strlen(timestr) - 1] = 0;
              } else {
                timestr = NULL;
              }

            if(!len){ len = strlen(line); }
            print_line(line, len, buffer, timestr, output_fmt);

          if (flush) { fflush(stdout); }
        }
      else if (retval < 0) { break; }
    }

  fclose (logfile);
  if(serial_port) { tcsetattr (fd, TCSANOW, &oldtio); }
  return 0;
}

/* Function that prints line in specified output format. Timestamp is optional.
   Buffer should be at least 4 times the line length. */
void print_line(char* line, int line_len, char* buffer, const char* time_stamp, int fmt)
{
  static const char* hex_chars_lc = "0123456789abcdef";
  static const char* hex_chars_uc = "0123456789ABCDEF";

  if(fmt == FMT_ACSII || fmt == FMT_RAW)
    {
      if (time_stamp)
        {
          /* If timestamps are printed we have to be sure that timestamp is at
          the beginning of the line. */
          if(line[line_len - 1] == '\n') { line[line_len - 1] = 0; }

          printf ("[%s] %s\n", time_stamp, line);
        } else {
          fputs (line, stdout);
        }
    } else if(fmt == FMT_HEX_LC || fmt == FMT_HEX_UC) {
      int buff_len = 0;
      const char* hex_chars = (fmt == FMT_HEX_LC) ? hex_chars_lc : hex_chars_uc;
      for(int i = 0; i < line_len; i++)
        {
            unsigned char d = line[i];
            buffer[buff_len++] = hex_chars[(d >> 4) & 0x0F];
            buffer[buff_len++] = hex_chars[d & 0x0F];
            buffer[buff_len++] = ' ';
        }

      buffer[buff_len++] = '\n';
      buffer[buff_len] = 0;

      if (time_stamp)
        {
          printf ("[%s] %s", time_stamp, buffer);
        } else {
          fputs (buffer, stdout);
        }
    }
}
