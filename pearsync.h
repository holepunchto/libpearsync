#ifndef PEARSYNC_H
#define PEARSYNC_H

#include <stdatomic.h>
#include <stdbool.h>
#include <uv.h>

#define PEARSYNC_QUEUE 1024

typedef struct {
  size_t len;
  void *data;
} pearsync_msg_t;

typedef struct {
  pearsync_msg_t buffer[PEARSYNC_QUEUE];
  pearsync_msg_t *overflow;

  int overflow_tail;
  int overflow_head;
  int overflow_size;

  atomic_int tail; // for the producer
  atomic_int head; // for the consumer
} pearsync_queue_t;

typedef struct {
  void *handle;
  void *data;
  bool is_uv;
} pearsync_port_t;

typedef struct {
  uv_async_t async;

  pearsync_queue_t uv_queue;
  pearsync_queue_t thread_queue;

  atomic_int uv_status;
  atomic_int thread_status;

  pearsync_port_t uv_port;
  pearsync_port_t thread_port;

  bool signal_thread;

  void (*on_signal_thread)(pearsync_port_t *);
  void (*on_recv_uv)(void *);
  void (*on_recv_thread)(void *);

  void (*on_close)(void *, size_t, pearsync_msg_t *, size_t, pearsync_msg_t *);
} pearsync_t;

void
pearsync_init (pearsync_t *self);

pearsync_port_t *
pearsync_open_uv (pearsync_t *self, uv_loop_t *loop, void (*on_recv)(pearsync_port_t *port));

pearsync_port_t *
pearsync_open_thread (pearsync_t *self, void (*on_signal_thread)(pearsync_port_t *port), void (*on_recv)(pearsync_port_t *port));

pearsync_port_t *
pearsync_get_port_uv (pearsync_t *self);

pearsync_port_t *
pearsync_get_port_thread (pearsync_t *self);

uv_handle_t *
pearsync_get_uv_handle (pearsync_t *self);

void
pearsync_wakeup (pearsync_port_t *port);

bool
pearsync_send (pearsync_port_t *port, pearsync_msg_t *m);

bool
pearsync_recv (pearsync_port_t *port, pearsync_msg_t *m);

void
pearsync_destroy (pearsync_t *self, void (*on_close)(pearsync_t *self, size_t main_len, pearsync_msg_t *main_msgs, size_t thread_len, pearsync_msg_t *thread_msgs));

#endif
