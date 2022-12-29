# libpearsync

Simple message passing between a libuv thread and something else.

No locks.
No allocs*
No fuzz

__... unless you send > 1024 messages in one tick, at which stage an extra buffer is allocated__

## Usage

``` c
// somewhere (but only in one place), init it
pearsync_t sync;
pearsync_init(&sync);

// then pass it to the libuv thread somehow and call
pearsync_port_t *uv_port = pearsync_open_uv(&sync, uv_loop, on_wakeup_uv);

// then in the other thread call
pearsync_port_t *port = pearsync_open(&sync, on_wakeup);

// note that these two open calls are threadsafe!
// the wakeup function is called when there is something to do (ie new messages)
// it is simply called with your message port

// to send a message do

pearsync_msg_t send_msg = {
  .len = 12,
  .data = "hello world"
};

pearsync_send(port, &send_msg);

// and to recv messages (usually inside the wakeup do)

pearsync_msg_t recv_msg;
while (pearsync_recv(port, &recv_msg)) {
  // do with the message as you please!
}

// that is it! see example.c for a full small example
// when you are done with it you can call

pearsync_destroy(&sync, on_close);

// NOTE that this should only be called in one place AND any
// calls after this is unsafe.
// on_close is called with (uv_len, uv_msgs, len, msgs)
// which is all the pending messages so you can free them etc
```

## License

MIT
