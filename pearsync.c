#include "pearsync.h"

#include <stdlib.h>
#include <stdbool.h>
#include <uv.h>

#define PEARSYNC_QUEUE_MASK (PEARSYNC_QUEUE - 1)

#define PEARSYNC_RECEIVING 0b01
#define PEARSYNC_NEEDS_DRAIN 0b10
#define PEARSYNC_DRAINED 0b01

#define PEARSYNC_DRAIN_OVERFLOW(self, q, send, status) \
  while (q->overflow_tail != q->overflow_head && ((q->head + 1) & PEARSYNC_QUEUE_MASK) != q->tail) { \
    pearsync_msg_t tmp; \
    pearsync_overflow_shift(q, &tmp); \
    status &= PEARSYNC_DRAINED; \
    send(self, &tmp); \
    if (q->overflow_tail != q->overflow_head) status |= PEARSYNC_NEEDS_DRAIN; \
  }

static bool
pearsync_main_send (pearsync_t *self, pearsync_msg_t *m);

static bool
pearsync_thread_send (pearsync_t *self, pearsync_msg_t *m);

static void
pearsync_queue_init (pearsync_queue_t *q) {
  q->tail = 0;
  q->head = 0;

  q->overflow = NULL;
  q->overflow_size = 0;
  q->overflow_head = 0;
  q->overflow_tail = 0;
}

static bool
pearsync_queue_shift (pearsync_queue_t *q, pearsync_msg_t *m) {
  if (q->tail == q->head) {
    if (m != NULL) {
      m->len = 0;
      m->data = NULL;
    }
    return false;
  }

  if (m != NULL) {
    pearsync_msg_t *v = ((pearsync_msg_t *) q->buffer) + q->tail;
    *m = *v;
  }

  q->tail = (q->tail + 1) & PEARSYNC_QUEUE_MASK;
  return true;
}

static bool
pearsync_queue_push (pearsync_queue_t *q, pearsync_msg_t *m) {
  int next = (q->head + 1) & PEARSYNC_QUEUE_MASK;

  if (next == q->tail) return false;

  pearsync_msg_t *v = ((pearsync_msg_t *) q->buffer) + q->head;
  *v = *m;

  q->head = next;
  return true;
}

static bool
pearsync_overflow_shift (pearsync_queue_t *q, pearsync_msg_t *m) {
  if (q->overflow_tail == q->overflow_head) return false;

  int mask = q->overflow_size - 1;

  *m = *(q->overflow + q->overflow_tail);
  q->overflow_tail = (q->overflow_tail + 1) & mask;

  return true;
}

static void
pearsync_overflow_push (pearsync_queue_t *q, pearsync_msg_t *m) {
  int mask = q->overflow_size - 1;

  if (q->overflow_size == 0 || ((q->overflow_head + 1) & mask) == q->overflow_tail) {
    if (q->overflow_size == 0) q->overflow_size = PEARSYNC_QUEUE;
    else q->overflow_size *= 2;

    mask = q->overflow_size - 1;
    pearsync_msg_t *old_overflow = q->overflow;
    pearsync_msg_t *new_overflow = q->overflow = malloc(q->overflow_size * sizeof(pearsync_msg_t));

    while (q->overflow_tail != q->overflow_head) {
      *(new_overflow++) = *(old_overflow + q->overflow_tail);
      q->overflow_tail = (q->overflow_tail + 1) & mask;
    }

    if (old_overflow != NULL) free(old_overflow);
  }

  *(q->overflow + q->overflow_head) = *m;
  q->overflow_head = (q->overflow_head + 1) & mask;
}

static void
pearsync_clear (pearsync_queue_t *q, size_t *len, pearsync_msg_t **msgs) {
  int mask = q->overflow_size - 1;

  size_t l = 0;

  l += (q->head - q->tail) & PEARSYNC_QUEUE_MASK;
  l += (q->overflow_head - q->overflow_tail) & mask;

  pearsync_msg_t *m = l ? malloc(l * sizeof(pearsync_msg_t)) : NULL;

  *len = l;
  *msgs = m;

  while (pearsync_queue_shift(q, m)) {
    m++;
  }

  while (pearsync_overflow_shift(q, m)) {
    m++;
  }

  if (q->overflow != NULL) {
    free(q->overflow);
    q->overflow = NULL;
    q->overflow_size = 0;
    q->overflow_head = 0;
    q->overflow_tail = 0;
  }
}

static void
pearsync_on_wakeup (uv_async_t *handle) {
  pearsync_t *self = (pearsync_t *) handle;

  pearsync_queue_t *mq = &(self->main_queue);
  pearsync_queue_t *tq = &(self->thread_queue);

  if (tq->tail != tq->head) self->on_wakeup_main(&(self->main_port));

  PEARSYNC_DRAIN_OVERFLOW(self, mq, pearsync_main_send, self->main_status)

  if (self->wakeup_thread) {
    self->wakeup_thread = false;
    self->on_wakeup_thread(&(self->thread_port));
  }
}

static void
pearsync_on_close (uv_handle_t *handle) {
  pearsync_t *self = (pearsync_t *) handle;

  pearsync_msg_t *main_msgs;
  size_t main_len;

  pearsync_msg_t *thread_msgs;
  size_t thread_len;

  pearsync_clear(&(self->main_queue), &main_len, &main_msgs);
  pearsync_clear(&(self->thread_queue), &thread_len, &thread_msgs);

  if (self->on_close != NULL) self->on_close(self, main_len, main_msgs, thread_len, thread_msgs);

  if (main_msgs != NULL) free(main_msgs);
  if (thread_msgs != NULL) free(thread_msgs);
}

void
pearsync_init (pearsync_t *self) {
  pearsync_queue_init(&(self->main_queue));
  pearsync_queue_init(&(self->thread_queue));

  self->wakeup_thread = false;
  self->main_status = 0;
  self->thread_status = 0;

  self->main_port.handle = self;
  self->main_port.data = NULL;
  self->main_port.is_main = true;

  self->thread_port.handle = self;
  self->thread_port.data = NULL;
  self->thread_port.is_main = false;
}

pearsync_port_t *
pearsync_port_uv (pearsync_t *self) {
  return &(self->main_port);
}

pearsync_port_t *
pearsync_open_uv (pearsync_t *self, uv_loop_t *loop, void (*on_wakeup)(pearsync_port_t *self)) {
  pearsync_queue_init(&(self->main_queue));
  uv_async_init(loop, (uv_async_t *) self, pearsync_on_wakeup);

  self->on_wakeup_main = (void (*)(void *)) on_wakeup;
  self->main_status |= PEARSYNC_RECEIVING;

  if (self->thread_status & PEARSYNC_RECEIVING) {
    uv_async_send((uv_async_t *) self);
  }

  return &(self->main_port);
}

pearsync_port_t *
pearsync_port (pearsync_t *self) {
  return &(self->thread_port);
}

pearsync_port_t *
pearsync_open (pearsync_t *self, void (*on_wakeup)(pearsync_port_t *self)) {
  pearsync_queue_init(&(self->thread_queue));

  self->on_wakeup_thread = (void (*)(void *)) on_wakeup;
  self->thread_status |= PEARSYNC_RECEIVING;

  if (self->main_status & PEARSYNC_RECEIVING) {
    uv_async_send((uv_async_t *) self);
  }

  return &(self->thread_port);
}

static bool
pearsync_thread_send (pearsync_t *self, pearsync_msg_t *m) {
  pearsync_queue_t *q = &(self->thread_queue);

  bool was_empty = q->head == q->tail;

  if (!(self->thread_status & PEARSYNC_NEEDS_DRAIN) && pearsync_queue_push(q, m)) {
    if (was_empty && self->main_status != 0) {
      uv_async_send((uv_async_t *) self);
    }
    return true;
  }

  self->thread_status |= PEARSYNC_NEEDS_DRAIN;
  pearsync_overflow_push(q, m);
  return false;
}

static bool
pearsync_main_send (pearsync_t *self, pearsync_msg_t *m) {
  pearsync_queue_t *q = &(self->main_queue);

  bool skip_signal = (q->head != q->tail) && !self->wakeup_thread;

  if (!(self->main_status & PEARSYNC_NEEDS_DRAIN) && pearsync_queue_push(q, m)) {
    if (skip_signal) return true;

    if (self->thread_status != 0) {
      self->wakeup_thread = false;
      self->on_wakeup_thread(&(self->thread_port));
    } else {
      self->wakeup_thread = true;
    }
    return true;
  }

  self->main_status |= PEARSYNC_NEEDS_DRAIN;
  pearsync_overflow_push(q, m);
  return false;
}

bool
pearsync_send (pearsync_port_t *port, pearsync_msg_t *m) {
  pearsync_t *self = (pearsync_t *) port->handle;
  return port->is_main ? pearsync_main_send(self, m) : pearsync_thread_send(self, m);
}

static inline bool
pearsync_main_recv (pearsync_t *self, pearsync_msg_t *m) {
  pearsync_queue_t *q = &(self->thread_queue);

  bool res = pearsync_queue_shift(q, m);

  if ((self->thread_status & PEARSYNC_NEEDS_DRAIN) && res && q->head == q->tail) {
    self->wakeup_thread = false;
    self->on_wakeup_thread(&(self->thread_port));
  }

  return res;
}

static inline bool
pearsync_thread_recv (pearsync_t *self, pearsync_msg_t *m) {
  pearsync_queue_t *tq = &(self->thread_queue);

  // just do this now as we know this runs on the "thread" thread
  PEARSYNC_DRAIN_OVERFLOW(self, tq, pearsync_thread_send, self->thread_status)

  pearsync_queue_t *q = &(self->main_queue);
  bool res = pearsync_queue_shift(q, m);

  if ((self->main_status & PEARSYNC_NEEDS_DRAIN) && res && q->tail == q->head) {
    uv_async_send((uv_async_t *) self);
  }

  return res;
}

bool
pearsync_recv (pearsync_port_t *port, pearsync_msg_t *m) {
  pearsync_t *self = (pearsync_t *) port->handle;
  return port->is_main ? pearsync_main_recv(self, m) : pearsync_thread_recv(self, m);
}

void
pearsync_destroy (pearsync_t *self, void (*on_close)(pearsync_t *self, size_t main_len, pearsync_msg_t *main_msgs, size_t thread_len, pearsync_msg_t *thread_msgs)) {
  self->on_close = (void (*)(void *, size_t, pearsync_msg_t *, size_t, pearsync_msg_t *)) on_close;

  if (self->main_status == 0) pearsync_on_close((uv_handle_t *) self);
  else uv_close((uv_handle_t *) self, pearsync_on_close);
}
