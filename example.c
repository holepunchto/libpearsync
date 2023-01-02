#include "pearsync.h"
#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static uv_async_t thread_async;

void
on_recv_thread (pearsync_port_t *a) {
  printf("woke up thread...\n");

  pearsync_msg_t m;
  while (pearsync_recv(a, &m)) {
    printf("thread recv: %s\n", (char *) m.data);
  }
}

void
on_recv_uv  (pearsync_port_t *a) {
  printf("woke up main...\n");

  pearsync_msg_t m;
  while (pearsync_recv(a, &m)) {
    printf("uv recv: %s\n", (char *) m.data);
  }
}

void
on_thread_wakeup (uv_async_t *handle) {
  pearsync_wakeup(handle->data);
}

void
signal_thread (pearsync_port_t *port) {
  uv_async_send(&thread_async);
}

void
run_thread (void *data) {
  pearsync_t *a = (pearsync_t *) data;

  printf("running on a thread! (spinning up sep uv loop but could be anything...)\n");

  uv_loop_t loop;
  uv_loop_init(&loop);

  uv_async_init(&loop, &thread_async, on_thread_wakeup);

  pearsync_port_t *p = pearsync_open_thread(a, signal_thread, on_recv_thread);
  thread_async.data = p;

  for (int i = 0; i < 64; i++) {
    pearsync_msg_t m = {
      .len = 32,
      .data = malloc(32)
    };

    m.len = sprintf(m.data, "hello uv #%i", i) + 1;

    pearsync_send(p, &m);
  }

  for (int i = 0; i < 4; i++) {
    pearsync_msg_t m = {
      .len = 32,
      .data = malloc(32)
    };

    m.len = sprintf(m.data, "hello uv #%i", 64 + i) + 1;

    pearsync_send(p, &m);
  }

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
}

int
main () {
  pearsync_t a;
  pearsync_init(&a);

  pearsync_port_t *p = pearsync_open_uv(&a, uv_default_loop(), on_recv_uv);

  // uncomment to send to thread

  // for (int i = 0; i < 32; i++) {
  //   pearsync_msg_t m = {
  //     .len = 32,
  //     .data = malloc(32)
  //   };

  //   m.len = sprintf(m.data, "hello thread #%i", i) + 1;

  //   pearsync_send(p, &m);
  // }

  uv_thread_t id;
  uv_thread_create(&id, run_thread, &a);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  printf("done, goodbye!\n");

  return 0;
}
