/*
 * See Licensing and Copyright notice in naev.h
 */

/**
 * @file log.c
 *
 * @brief Home of logprintf.
 */

/** @cond */
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h> /* strftime */
#include "physfs.h"

#include "naev.h"

#if HAS_POSIX
#include <unistd.h> /* isatty */
#endif
/** @endcond */

#include "log.h"

#include "console.h"
#include "ndata.h"
#include "nstring.h"


/**< Temporary storage buffers. */
static char *outcopy = NULL;
static char *errcopy = NULL;

static size_t moutcopy; /* Allocated size of outcopy. */
static size_t merrcopy; /* Allocated size of errcopy. */

static int noutcopy = 0; /* Number of bytes written to outcopy. */
static int nerrcopy = 0; /* Number of bytes written to errcopy. */

/**< Output filenames for stdout and stderr. */
static char *outfiledouble = NULL;
static char *errfiledouble = NULL;

/* Whether to copy stdout and stderr to temporary buffers. */
static int copying = 0;

/* File descriptors */
static PHYSFS_File *logout_file = NULL;
static PHYSFS_File *logerr_file = NULL;


/*
 * Prototypes
 */
static void log_append( FILE *stream, char *str );

/**
 * @brief Like fprintf but also prints to the naev console.
 */
int logprintf( FILE *stream, int newline, const char *fmt, ... )
{
   va_list ap;
   char buf[2048];
   size_t n;

   if (fmt == NULL)
      return 0;
   else { /* get the message */
      /* Add header if necessary. */
      /* Print variable text. */
      va_start( ap, fmt );
      n = vsnprintf( &buf[2], sizeof(buf)-3, fmt, ap )-1;
      va_end( ap );

   }

#ifndef NOLOGPRINTFCONSOLE
   /* Add to console. */
   if (stream == stderr) {
      buf[0] = '#';
      buf[1] = 'r';
      cli_addMessage( buf );
   }
   else
      cli_addMessage( &buf[2] );
#endif /* NOLOGPRINTFCONSOLE */

   /* Finally add newline if necessary. */
   if (newline) {
      buf[2+n+1] = '\n';
      buf[2+n+2] = '\0';
   }
   else
      buf[2+n+1] = '\0';

   /* Append to buffer. */
   if (copying)
      log_append(stream, &buf[2]);

   if ( stream == stdout && logout_file != NULL ) {
      PHYSFS_writeBytes( logout_file, &buf[2], newline ? n+2 : n+1 );
      if ( newline )
         PHYSFS_flush( logout_file );
   }

   if ( stream == stderr && logerr_file != NULL ) {
      PHYSFS_writeBytes( logerr_file, &buf[2], newline ? n+1 : n );
      if ( newline )
         PHYSFS_flush( logerr_file );
   }

   /* Also print to the stream. */
   n = fprintf( stream, "%s", &buf[ 2 ] );
   if ( newline )
      fflush( stream );
   return n;
}


/**
 * @brief Redirects stdout and stderr to files.
 *
 * Should only be performed if conf.redirect_file is true and Naev isn't
 * running in a terminal.
 */
void log_redirect (void)
{
   time_t cur;
   struct tm *ts;
   char timestr[20];

   time(&cur);
   ts = localtime(&cur);
   strftime( timestr, sizeof(timestr), "%Y-%m-%d_%H-%M-%S", ts );

   outfiledouble = malloc(PATH_MAX);
   errfiledouble = malloc(PATH_MAX);

   PHYSFS_mkdir( "logs" );
   logout_file = PHYSFS_openWrite( "logs/stdout.txt" );
   if ( logout_file == NULL )
      WARN(_("Unable to redirect stdout to file"));

   logerr_file = PHYSFS_openWrite( "logs/stderr.txt" );
   if ( logerr_file == NULL )
      WARN(_("Unable to redirect stderr to file"));

   nsnprintf( outfiledouble, PATH_MAX, "logs/%s_stdout.txt", timestr );
   nsnprintf( errfiledouble, PATH_MAX, "logs/%s_stderr.txt", timestr );
}


/**
 * @brief Checks whether Naev is connected to a terminal.
 *
 *    @return 1 if Naev is connected to a terminal, 0 otherwise.
 */
int log_isTerminal (void)
{
#if HAS_POSIX
   /* stdin and (stdout or stderr) are connected to a TTY */
   if (isatty(fileno(stdin)) && (isatty(fileno(stdout)) || isatty(fileno(stderr))))
      return 1;

#elif HAS_WIN32
   struct stat buf;

   /* Not interactive if stdin isn't a FIFO or character device. */
   if (fstat(_fileno(stdin), &buf) ||
         !((buf.st_mode & S_IFMT) & (S_IFIFO | S_IFCHR)))
      return 0;

   /* Interactive if stdout is a FIFO or character device. */
   if (!fstat(_fileno(stdout), &buf) &&
         ((buf.st_mode & S_IFMT) & (S_IFIFO | S_IFCHR)))
      return 1;

   /* Interactive if stderr is a FIFO or character device. */
   if (!fstat(_fileno(stderr), &buf) &&
         ((buf.st_mode & S_IFMT) & (S_IFIFO | S_IFCHR)))
      return 1;

#else
#error "Feature needs implementation on this Operating System for Naev to work."
#endif
   return 0;
}


/**
 * @brief Sets up or terminates copying of standard streams into memory.
 *
 * While copying is active, all stdout and stderr-bound messages that pass
 * through logprintf will also be put into a buffer in memory, to be flushed
 * when copying is disabled.
 *
 *    @param enable Whether to enable or disable copying. Disabling flushes logs.
 */
void log_copy( int enable )
{
   /* Nothing to do. */
   if (copying == enable)
      return;

   if (enable) {
      copying  = 1;

      moutcopy = 1;
      noutcopy = 0;
      outcopy  = calloc(moutcopy, BUFSIZ);

      merrcopy = 1;
      nerrcopy = 0;
      errcopy  = calloc(merrcopy, BUFSIZ);

      return;
   }

   if ( noutcopy && logout_file != NULL )
      PHYSFS_writeBytes( logout_file, outcopy, strlen(outcopy) );

   if ( nerrcopy && logerr_file != NULL )
      PHYSFS_writeBytes( logerr_file, errcopy, strlen(errcopy) );

   log_purge();
}


/**
 * @brief Whether log copying is enabled.
 *
 *    @return 1 if copying is enabled, 0 otherwise.
 */
int log_copying (void)
{
   return copying;
}


/**
 * @brief Deletes copied output without printing the contents.
 */
void log_purge (void)
{
   if (!copying)
      return;

   free(outcopy);
   free(errcopy);

   outcopy = NULL;
   errcopy = NULL;

   copying = 0;
}


/**
 * @brief Deletes the current session's log pair if stderr is empty.
 */
void log_clean (void)
{
   PHYSFS_Stat err;

   /* We assume redirection is only done in pairs. */
   if ((logout_file == NULL) || (logerr_file == NULL))
      return;

   PHYSFS_close( logout_file );
   logout_file = NULL;
   PHYSFS_close( logerr_file );
   logerr_file = NULL;

   if (PHYSFS_stat( "logs/stderr.txt", &err) == 0)
      return;

   if (err.filesize == 0) {
      PHYSFS_delete( "logs/stdout.txt" );
      PHYSFS_delete( "logs/stderr.txt" );
   } else {
      ndata_copyIfExists( "logs/stdout.txt", outfiledouble );
      ndata_copyIfExists( "logs/stderr.txt", errfiledouble );
   }
}


/**
 * @brief Appends a message to a stream's in-memory buffer.
 *
 *    @param stream Destination stream (stdout or stderr)
 *    @param str String to append.
 */
static void log_append( FILE *stream, char *str )
{
   int len;

   len = strlen(str);
   if (stream == stdout) {
      while ((len + noutcopy) >= (int)moutcopy) {
         moutcopy *= 2;
         outcopy = realloc( outcopy, moutcopy );
         if (outcopy == NULL) goto copy_err;
      }

      strncpy( &outcopy[noutcopy], str, len+1 );
      noutcopy += len;
   }
   else if (stream == stderr) {
      while ((len + nerrcopy) >= (int)merrcopy) {
         merrcopy *= 2;
         errcopy = realloc( errcopy, merrcopy );
         if (errcopy == NULL) goto copy_err;
      }

      strncpy( &errcopy[nerrcopy], str, len+1 );
      nerrcopy += len;
   }

   return;

copy_err:
   log_purge();
   WARN(_("An error occurred while buffering %s!"),
      stream == stdout ? "stdout" : "stderr");
}
