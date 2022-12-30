#include "pearsync.h"
#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void
on_wakeup_thread (pearsync_port_t *a) {
  printf("woke up thread...\n");

  pearsync_msg_t m;
  while (pearsync_recv(a, &m)) {
    printf("thread recv: %s\n", (char *) m.data);
  }
}

void
on_wakeup_uv  (pearsync_port_t *a) {
  printf("woke up main...\n");

  pearsync_msg_t m;
  while (pearsync_recv(a, &m)) {
    printf("uv recv: %s\n", (char *) m.data);
  }
}

void
run_thread (void *data) {
  pearsync_t *a = (pearsync_t *) data;

  printf("running on a thread...\n");

  pearsync_port_t *p = pearsync_open_thread(a, pearsync_wakeup, on_wakeup_thread);

  for (int i = 0; i < 32; i++) {
    pearsync_msg_t m = {
      .len = 32,
      .data = malloc(32)
    };

    m.len = sprintf(m.data, "hello uv #%i", i) + 1;

    pearsync_send(p, &m);
  }

  printf("thread is sleeping 2s and then sending 4 more\n");
  sleep(2);

  for (int i = 0; i < 4; i++) {
    pearsync_msg_t m = {
      .len = 32,
      .data = malloc(32)
    };

    m.len = sprintf(m.data, "hello uv #%i", 32 + i) + 1;

    pearsync_send(p, &m);
  }
}

int
main () {
  pearsync_t a;
  pearsync_init(&a);

  pearsync_port_t *p = pearsync_open_uv(&a, uv_default_loop(), on_wakeup_uv);

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
