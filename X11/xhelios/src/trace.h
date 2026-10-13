#ifndef HELIOS_TRACE_H
#define HELIOS_TRACE_H
extern char *heliosTracePath;
void heliosTraceInit(void);
void heliosTrace(const char *, long, long, long, long);
#define HTRACE(stage,a,b,c,d) do { if (heliosTracePath) \
    heliosTrace(stage, (long)(a), (long)(b), (long)(c), (long)(d)); } while (0)
#endif
