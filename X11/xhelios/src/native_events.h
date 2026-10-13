/* Native receivers own packets only; all X objects stay on the dispatch thread. */
#ifndef HELIOS_NATIVE_EVENTS_H
#define HELIOS_NATIVE_EVENTS_H
#include <helios.h>
#include <syslib.h>
#include <gsp.h>
#include <sem.h>

#define HELIOS_EVENT_PACKETS 8
typedef struct {
    Semaphore lock;
    long produced, consumed;
    int pipe[2], armed, failed;
} HeliosEventNotify;

typedef struct {
    word result;
    unsigned size;
    union { word align; byte bytes[IOCDataMax]; } data;
} HeliosEventPacket;

typedef struct {
    Stream *stream;
    Port port;
    HeliosEventNotify *notify;
    Semaphore lock, space, stopped;
    int running, stopping, failed;
    unsigned head, tail, count;
    HeliosEventPacket packets[HELIOS_EVENT_PACKETS];
} HeliosEventSource;

int heliosInitEventNotify(HeliosEventNotify *);
void heliosCloseEventNotify(HeliosEventNotify *);
int heliosConsumeEventNotify(HeliosEventNotify *);
int heliosOpenEvents(HeliosEventSource *, char *, word, HeliosEventNotify *);
void heliosCloseEvents(HeliosEventSource *);
int heliosReadEvents(HeliosEventSource *, HeliosEventPacket *);
#endif
