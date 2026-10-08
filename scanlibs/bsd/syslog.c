/* $Id: syslog.c,v 1.3 1993/07/09 12:58:52 nickc Exp $ */
#include <sys/types.h>
#include <syslog.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <syslib.h>
#include <errno.h>
#include <unistd.h>

static int logfd = -1;
static int logopt = 0;
static int logfac = 0;
static char *logident = NULL;
static int logmask = -1;

#define LOGBUFSIZE 512
static char logbuf[LOGBUFSIZE];
static char fbuf[100];

/* vsprintf into buf[size], dropping what does not fit.  This is the	*/
/* string FILE that Clib's sprintf uses, with a finite count: once it	*/
/* is used up, Clib's _flushbuf drops further characters.  The flag	*/
/* values are those of stdio.h (only visible with __system_io).	*/
#define LOG_IOWRITE	0x02
#define LOG_IOSTRG	0x08

static int bvsprintf(char *buf, int size, char *fmt, va_list a)
{
	FILE f;

	if( size <= 0 ) return 0;
	memset(&f,0,sizeof(FILE));
	f._flag = LOG_IOSTRG+LOG_IOWRITE;
	f._ptr = (unsigned char *)buf;
	f._ocnt = size - 1;
	vfprintf(&f,fmt,a);
	*f._ptr = 0;
	return (char *)f._ptr - buf;
}

extern int openlog(char *ident, int opt, int facility)
{
	logopt |= opt;
	logfac |= facility;
	if( ident ) 
	{
		if( logident ) Free(logident);
		logident = (char *)Malloc((long)strlen(ident)+1);
		if( logident ) strcpy(logident,ident);
	}
	
	if( logopt & LOG_NDELAY )
	{
		if( fdstream(3) != NULL ) logfd = 3;
		else logfd = open("/logger",O_WRONLY);
	}

	return 0;
}

extern int syslog(int priority, char *message, ... )
{
	va_list a;
	int n = 0;
	int pos = 0;

	if( !(logmask & LOG_MASK(priority)) ) return 0;
	
	if( logfd == -1 ) openlog(NULL,LOG_NDELAY,0);
	
	va_start(a,message);

	if( logident ) pos += sprintf(logbuf,"SYSLOG: %.40s ",logident);
	else pos += sprintf(logbuf,"SYSLOG: ");

	/* the text is cut to leave room for "\r\n" and the terminator */
	for(;;)
	{
		char ch = *message++;
		if( ch == 0 || (ch == '%' && *message == 'm')) 
		{
			char *es;
			fbuf[n] = 0;
			pos += bvsprintf(logbuf+pos,LOGBUFSIZE-2-pos,fbuf,a);
			if( ch == 0 ) break;
			message++;
			if( errno >= 0 && errno < sys_nerr ) es = sys_errlist[errno];
			else es = "Unknown error";
			while( *es != 0 && pos < LOGBUFSIZE-3 ) logbuf[pos++] = *es++;
			logbuf[pos] = 0;
			n = 0;
		}
		else if( n < 99 ) fbuf[n++] = ch;
	}
	n = strlen(logbuf);
	if( logbuf[n-1] != '\n' ) logbuf[n++] = '\r',logbuf[n++] = '\n';
	write(logfd,logbuf,n);
	return 0;
}

extern int closelog()
{
	close(logfd);
	logfd = -1;
	logopt = logfac = 0;
	Free(logident);
	logident = NULL;

	return 0;
}

extern int setlogmask(int maskpri)
{
  int	oldpri = logmask;
  
  logmask = maskpri;

  return oldpri;  
}

