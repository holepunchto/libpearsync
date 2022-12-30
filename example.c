#include "pearsync.h"
#include <uv.h>
#include <stdio.h>
#include <stdlib.h>

void
on_wakeup_thread (pearsync_port_t *a) {
  printf("wakeup thread...\n");

  pearsync_msg_t m;
  while (pearsync_recv(a, &m)) {
    printf("thread recv: %s\n", m.data);
  }
}

void
on_wakeup_main  (pearsync_port_t *a) {
  printf("wakeup main... %i\n", a->is_main ? 1 : 0);

  pearsync_msg_t m;
  while (pearsync_recv(a, &m)) {
    printf("main recv: %s\n", m.data);
  }
}

void
run_thread (void *data) {
  pearsync_t *a = (pearsync_t *) data;

  printf("i am a thread\n");
  pearsync_open_thread(a, pearsync_wakeup, on_wakeup_thread);
}

int
main () {
  pearsync_t a;
  pearsync_init(&a);

  pearsync_port_t *p = pearsync_open_uv(&a, uv_default_loop(), on_wakeup_main);

  pearsync_msg_t m = {
    .len = 13,
    .data = malloc(13)
  };

  memcpy(m.data, "hello thread", 13);

  pearsync_send(p, &m);
  pearsync_send(p, &m);

  uv_thread_t id;
  uv_thread_create(&id, run_thread, &a);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  printf("done, goodbye!\n");

  return 0;
}
