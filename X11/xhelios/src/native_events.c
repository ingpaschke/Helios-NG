#include <string.h>
#include <nonansi.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "native_events.h"

typedef struct {
    HeliosEventNotify *notify;
    Semaphore done;
    int result;
} NotifyInit;

static void primeNotifyPipe(NotifyInit *init)
{
    char byte = 0;
    do { init->result = write(init->notify->pipe[1], &byte, 1); }
    while (init->result < 0 && errno == EINTR);
    if (init->result != 1) {
        close(init->notify->pipe[1]);
        init->notify->pipe[1] = -1;
    }
    Signal(&init->done);
}

int heliosInitEventNotify(HeliosEventNotify *notify)
{
    NotifyInit init;
    char byte;
    int result;
    memset(notify, 0, sizeof(*notify));
    notify->pipe[0] = notify->pipe[1] = -1;
    InitSemaphore(&notify->lock, 1);
    if (pipe(notify->pipe) < 0) return 0;
    /* Helios pipe() returns lazy streams. Their first read/write rendezvous
     * can block even with O_NONBLOCK. Complete that handshake before input
     * threads can publish data or the dispatch thread can acquire our lock. */
    init.notify = notify;
    InitSemaphore(&init.done, 0);
    if (!Fork(10000, primeNotifyPipe, sizeof(&init), &init)) {
        heliosCloseEventNotify(notify);
        return 0;
    }
    do { result = read(notify->pipe[0], &byte, 1); }
    while (result < 0 && errno == EINTR);
    Wait(&init.done);
    if (result != 1 || init.result != 1) {
        heliosCloseEventNotify(notify);
        return 0;
    }
    if (fcntl(notify->pipe[0], F_SETFL, O_NONBLOCK) < 0 ||
        fcntl(notify->pipe[1], F_SETFL, O_NONBLOCK) < 0) {
        heliosCloseEventNotify(notify);
        return 0;
    }
    return 1;
}

/* Call only after both receiver threads have stopped. */
void heliosCloseEventNotify(HeliosEventNotify *notify)
{
    if (notify->pipe[0] >= 0) close(notify->pipe[0]);
    if (notify->pipe[1] >= 0) close(notify->pipe[1]);
    notify->pipe[0] = notify->pipe[1] = -1;
    notify->produced = notify->consumed = 0;
}

static void notifyEvents(HeliosEventNotify *notify)
{
    char byte = 0;
    int result;
    if (!notify) return;
    Wait(&notify->lock);
    notify->produced = (unsigned long)notify->produced + 1;
    if (!notify->armed) {
        /* Coalesce wakeups, leaving at most one byte in the pipe. Neither
         * a busy client nor an idle dispatcher may stall a native receiver. */
        do { result = write(notify->pipe[1], &byte, 1); }
        while (result < 0 && errno == EINTR);
        if (result == 1) notify->armed = 1;
        else notify->failed = 1;
    }
    Signal(&notify->lock);
}

int heliosConsumeEventNotify(HeliosEventNotify *notify)
{
    char byte;
    int result, ok;
    Wait(&notify->lock);
    if (notify->armed) {
        do { result = read(notify->pipe[0], &byte, 1); }
        while (result < 0 && errno == EINTR);
        if (result != 1) notify->failed = 1;
        notify->armed = 0;
    }
    /* Snapshot BEFORE draining. A packet published while draining must
     * leave the DIX input check unequal for the next client request. */
    notify->consumed = notify->produced;
    ok = !notify->failed;
    Signal(&notify->lock);
    return ok;
}

static void receiveEvents(HeliosEventSource *source)
{
    MCB message;
    word control[256];
    HeliosEventPacket *packet;
    word result;
    for (;;) {
        Wait(&source->space);
        Wait(&source->lock);
        if (source->stopping) {
            Signal(&source->lock);
            break;
        }
        packet = &source->packets[source->head];
        Signal(&source->lock);
        memset(&message, 0, sizeof(message));
        message.Data = packet->data.bytes;
        message.Control = control;
        message.MsgHdr.Dest = source->port;
        /* Sacrificial mouse packets are dropped unless GetMsg is waiting. */
        message.Timeout = -1;
        result = GetMsg(&message);
        Wait(&source->lock);
        if (source->stopping || result < 0) {
            source->failed = !source->stopping;
            Signal(&source->lock);
            if (source->failed) notifyEvents(source->notify);
            break;
        }
        packet->result = result;
        packet->size = message.MsgHdr.DataSize;
        source->head = (source->head + 1) % HELIOS_EVENT_PACKETS;
        ++source->count;
        Signal(&source->lock);
        notifyEvents(source->notify);
    }
    Signal(&source->stopped);
}

void heliosCloseEvents(HeliosEventSource *source)
{
    if (source->running) {
        Wait(&source->lock);
        source->stopping = 1;
        Signal(&source->lock);
    }
    if (source->stream) {
        EnableEvents(source->stream, 0);
        Close(source->stream);
    }
    /* FreePort also makes a GetMsg started after this point fail. AbortPort
     * alone would race with a receiver about to enter GetMsg. */
    if (source->port) FreePort(source->port);
    if (source->running) {
        Signal(&source->space);
        Wait(&source->stopped);
    }
    memset(source, 0, sizeof(*source));
}

int heliosOpenEvents(HeliosEventSource *source, char *name, word type,
                     HeliosEventNotify *notify)
{
    Object *object;
    source->notify = notify;
#ifdef HELIOS_INPUT_TEST
    /* Private diagnostic build: use the real receiver with an injected port. */
    if (!name) {
        source->port = NewPort();
    } else
#endif
    {
    object = Locate(NULL, name);
    if (!object) return 0;
    source->stream = Open(object, NULL, O_ReadOnly);
    Close(object);
    if (!source->stream) return 0;
    source->port = EnableEvents(source->stream, type);
    }
    if (!source->port) { heliosCloseEvents(source); return 0; }
    InitSemaphore(&source->lock, 1);
    InitSemaphore(&source->space, HELIOS_EVENT_PACKETS);
    InitSemaphore(&source->stopped, 0);
    source->running = Fork(10000, receiveEvents, sizeof(source), source);
    if (!source->running) { heliosCloseEvents(source); return 0; }
    return 1;
}

int heliosReadEvents(HeliosEventSource *source, HeliosEventPacket *packet)
{
    int result = 0;
    if (!source->running) return 0;
    Wait(&source->lock);
    if (source->count) {
        *packet = source->packets[source->tail];
        source->tail = (source->tail + 1) % HELIOS_EVENT_PACKETS;
        --source->count;
        Signal(&source->space);
        result = 1;
    } else if (source->failed) result = -1;
    Signal(&source->lock);
    return result;
}
