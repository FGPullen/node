
#ifndef NODE_WIN32_EV
#define NODE_WIN32_EV

#include <platform_win32.h>
#include <malloc.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Global io-completion port
 */

extern HANDLE iocp;

void iocp_fatal_error(const char *syscall = NULL);

/* Event loop time */

typedef double ev_tstamp;

extern ev_tstamp ev_rt_now;

ev_tstamp ev_time(void);
void ev_now_update(void);

inline ev_tstamp ev_now(void) {
  return ev_rt_now;
}

inline void ev_now_update(void) {
  ev_rt_now = ev_time();
}

/*
 * Iocp packet type and helper functions
 */

typedef struct IocpPacket IocpPacket;
typedef void IocpCallback(IocpPacket* packet);

struct IocpPacket {
  /* The overlapped data that windows touches */
  OVERLAPPED overlapped;

  /* The callback that is called by ev_poll when it dequeues a this package */
  IocpCallback *callback;

  /* Watcher type specific data associated with the packet, that can be used */
  /* by the callback */
  union {
    struct ev_async *w_async; /* For libev ev_async compatibility layer */
    struct ev_timer *w_timer; /* For libev ev_timer compatibility layer */
  };

  /* Handler priority */
  int priority;

  /* The next unused iocp package, used by the freelist */
  IocpPacket *next_free;
};

inline OVERLAPPED *PacketToOverlapped(IocpPacket *packet) {
  return &(packet->overlapped);
}

inline IocpPacket *OverlappedToPacket(OVERLAPPED *overlapped) {
  return CONTAINING_RECORD(overlapped, IocpPacket, overlapped);
}

/* Iocp packet freelist */
extern IocpPacket *free_iocp_packet_list;


/* Reuse an unused iocp packet, or optionally allocate a new one */
inline IocpPacket *AllocIocpPacket() {
  IocpPacket* packet;
  if (free_iocp_packet_list != NULL) {
    packet = free_iocp_packet_list;
    free_iocp_packet_list = packet->next_free;
  } else {
    packet = (IocpPacket*)malloc(sizeof(IocpPacket));
  }
  return packet;
}


/* Free an iocp packet, or allow it to be re-used */
inline void FreeIocpPacket(IocpPacket *packet) {
  // Todo: free iocp packets after some time
  packet->next_free = free_iocp_packet_list;
  free_iocp_packet_list = packet;
}


/* Libev compatibility layer */

#define EV_P_
#define EV_A_
#define EV_DEFAULT
#define EV_DEFAULT_
#define EV_DEFAULT_UC
#define EV_DEFAULT_UC_

enum {
  EV_UNDEF    = 0xFFFFFFFF, /* guaranteed to be invalid */
  EV_NONE     =       0x00, /* no events */
  EV_READ     =       0x01, /* ev_io detected read will not block */
  EV_WRITE    =       0x02, /* ev_io detected write will not block */
  EV_IO       =    EV_READ, /* alias for type-detection */
  EV_TIMER    = 0x00000100, /* timer timed out */
  EV_PERIODIC = 0x00000200, /* periodic timer timed out */
  EV_SIGNAL   = 0x00000400, /* signal was received */
  EV_CHILD    = 0x00000800, /* child/pid had status change */
  EV_STAT     = 0x00001000, /* stat data changed */
  EV_IDLE     = 0x00002000, /* event loop is idling */
  EV_PREPARE  = 0x00004000, /* event loop about to poll */
  EV_CHECK    = 0x00008000, /* event loop finished poll */
  EV_EMBED    = 0x00010000, /* embedded event loop needs sweep */
  EV_FORK     = 0x00020000, /* event loop resumed in child */
  EV_CLEANUP  = 0x00040000, /* event loop resumed in child */
  EV_ASYNC    = 0x00080000, /* async intra-loop signal */
  EV_CUSTOM   = 0x01000000, /* for use by user code */
  EV_ERROR    = 0x80000000, /* sent when an error occurs */

  EV_TIMEOUT  =   EV_TIMER  /* pre 4.0 API compatibility */
};

/* priority range */

#ifndef EV_MINPRI
# define EV_MINPRI -2
#endif

#ifndef EV_MAXPRI
# define EV_MAXPRI 2
#endif

#define EV_NUMPRI                                           \
  ((EV_MAXPRI) - (EV_MINPRI) + 1)

#define EV_ABSPRI(priority)                                 \
  ((priority) - (EV_MINPRI))

/* ev_watcher base class */

#define EV_WATCHER_CB(type)                                 \
  void (*cb)(type *w, int revents)                          \

#define EV_BASE(type)                                       \
  int active;                                               \
  int priority;                                             \
  void *data;                                               \
  EV_WATCHER_CB(type);

#define ev_init(w, new_cb)                                  \
  do {                                                      \
    (w)->cb = (new_cb );                                    \
    (w)->active = 0;                                        \
    (w)->priority = 0;                                      \
  } while (0)

#define ev_is_active(w)                                     \
  ((w)->active)

#define ev_priority(w)                                      \
  ((w)->priority)

#define ev_set_priority(w, new_pri)                         \
  do {                                                      \
    (w)->priority = (new_pri);                              \
  } while (0)

#define ev_cb(w)                                            \
  ((w)->cb)                                                 \

#define ev_cb_set(w, new_cb)                                \
  do {                                                      \
    (w)->cb = (new_cb);                                     \
  } while (0)


/* static ev_watchers: ev_prepare, ev_check, ev_idle */

#define EV_STATIC_DEFINE(type)                              \
  typedef struct type type;                                 \
  extern type *type##_list[EV_NUMPRI];                      \
                                                            \
  struct type {                                             \
    EV_BASE(type)                                           \
    type *prev;                                             \
    type *next;                                             \
  };                                                        \
                                                            \
  inline void type##_init(type *w, EV_WATCHER_CB(type)) {   \
    ev_init(w, cb);                                         \
    w->next = NULL;                                         \
    w->prev = NULL;                                         \
  }                                                         \
                                                            \
  inline void type##_set(type *w) {                         \
  }                                                         \
                                                            \
  inline void type##_start(type *w) {                       \
    if (w->active)                                          \
      return;                                               \
    w->active = 1;                                          \
                                                            \
    int abs_pri = EV_ABSPRI(w->priority);                   \
    type *old_head = type##_list[abs_pri];                  \
    type##_list[abs_pri] = w;                               \
    w->prev = NULL;                                         \
    w->next = old_head;                                     \
    if (old_head)                                           \
      old_head->prev = w;                                   \
  }                                                         \
                                                            \
  inline void type##_stop(type *w) {                        \
    if (!w->active)                                         \
      return;                                               \
    w->active = 0;                                          \
                                                            \
    int abs_pri = EV_ABSPRI(w->priority);                   \
    if (type##_list[abs_pri] == w)                          \
      type##_list[abs_pri] = w->next;                       \
    if (w->next)                                            \
      w->next->prev = w->prev;                              \
    if (w->prev)                                            \
      w->prev->next = w->next;                              \
  }                                                         \

EV_STATIC_DEFINE(ev_prepare)
EV_STATIC_DEFINE(ev_check)
EV_STATIC_DEFINE(ev_idle)

/* ev_async */

typedef struct ev_async ev_async;

struct ev_async {
  EV_BASE(ev_async)
  volatile int sent;
};

void ev_async_handle_packet(IocpPacket *packet);

inline void ev_async_set(ev_async *w) {
}

inline void ev_async_init(ev_async *w, EV_WATCHER_CB(ev_async)) {
  ev_init(w, cb);
  w->sent = 0;
}

inline void ev_async_start(ev_async *w) {
  w->active = true;
}

inline void ev_async_stop(ev_async *w) {
  w->active = false;
}

inline void ev_async_send(ev_async *w) {
  assert(w->active);

  if (w->sent)
    return;
  w->sent = 1;

  IocpPacket *packet = AllocIocpPacket();
  packet->w_async = w;
  packet->callback = &ev_async_handle_packet;
  packet->priority = w->priority;

  BOOL success = PostQueuedCompletionStatus(iocp, 0, 0, PacketToOverlapped(packet));
  if (!success)
    iocp_fatal_error("PostQueuedCompletionStatus");
}

inline int ev_async_pending(ev_async *w) {
  return w->sent;
}

/* ev_timer */

typedef struct ev_timer ev_timer;

struct ev_timer {
  EV_BASE(ev_timer)
  double after;
  double repeat;
  HANDLE timer;
};

void CALLBACK ev_timer_handle_apc(void *arg, DWORD timeLow, DWORD timeHigh);

inline void ev_timer_set(ev_timer *w, double after, double repeat) {
  w->after = after;
  w->repeat = repeat;
}

inline void ev_timer_init(ev_timer *w, EV_WATCHER_CB(ev_timer), double after,
    double repeat) {
  ev_init(w, cb);
  ev_timer_set(w, after, repeat);
}

inline void ev_timer_start(ev_timer *w) {
  LARGE_INTEGER due;
  LONG period;

  if (w->active)
    return;
  w->active = true;

  w->timer = CreateWaitableTimerW(NULL, FALSE, NULL);
  if (!w->timer)
    iocp_fatal_error("CreateWaitableTimerW");

  /* Calculate timeout */
  /* 1 tick per 100ns, negative value denotes relative time */
  due.QuadPart = w->after > 0
               ? w->after * -10000000LL
               : -1LL;

  /* Calculate repeat period */
  /* 1 tick per 1ms */
  period = w->repeat > 0
         ? w->repeat * 1000L
         : 0L;

  if (!SetWaitableTimer(w->timer,
                        &due,
                        period,
                        &ev_timer_handle_apc,
                        (void*)w,
                        FALSE))
    iocp_fatal_error("SetWaitableTimer");
}

inline void ev_timer_stop(ev_timer *w) {
  if (!w->active)
    return;
  w->active = false;

  if (!CancelWaitableTimer(w->timer))
    iocp_fatal_error("CancelWaitableTimer");

  CloseHandle(w->timer);
}

inline void ev_timer_again(ev_timer *w) {
  /* todo: optimize this */
  /* Instead of stop, destroy, create, start, it would be better to just */
  /* call SetWaitableTimer to cancel outstanding APCs and set the timeout */
  /* to the same value as repeat */
  ev_timer_stop(w);

  if (w->repeat > 0) {
    w->after = w->repeat;
    ev_timer_start(w);
  }
}

/* meh */
void iocp_run();
void iocp_init();

typedef struct ev_io { EV_BASE(ev_io) } ev_io;
#define ev_io_set(...)
#define ev_io_init(...)
#define ev_io_start(...)
#define ev_io_stop(...)


typedef struct ev_signal { EV_BASE(ev_signal) } ev_signal;
#define ev_signal_init(...)
#define ev_signal_start(...)
#define ev_signal_stop(...)

typedef struct ev_stat {
  EV_BASE(ev_stat)
  struct _stati64 attr;
  struct _stati64 prev;
} ev_stat;
#define ev_stat_init(...)
#define ev_stat_set(...)
#define ev_stat_start(...)
#define ev_stat_stop(...)


#define ev_ref(...)
#define ev_unref(...)
#define ev_version_major() 2
#define ev_version_minor() 0
#define ev_is_pending(...) (0)

#define EVFLAG_AUTO 0
inline void ev_default_loop(int flags) {
  iocp_init();
}
inline void ev_loop(int flags) {
  iocp_run();
}
#define SIGTERM 0xffffff

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NODE_WIN32_EV */
