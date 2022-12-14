# The Power of `io_uring` - A journey

## Day 04: An event loop for `io_uring`

The event loop is required for us to obtain the results of anything that was submitted to the kernel (via `io_uring_submit`) and what we need very similar to what would be implemented using `select` or `poll`/`epoll` but with some important differences.

Unlike mechanisms using non-blocking sockets, submitting an operation doesn't tell the user when to perform that operation but it actually executes it. In other words, submitting a write us the equivalent of performing an async write.

Since we're using `io_uring_prep_multishot_accept_direct`, we can call it once before the event loop starts and just process reads, writes etc in the loop itself.

The code for that would look like this

```
struct io_uring_sqe *sQueue = io_uring_get_sqe(ring);

// socketDesc is the listening socket
io_uring_prep_multishot_accept_direct(sQueue, socketDesc, NULL, NULL, 0);

io_uring_submit(ring);

while(true){
    struct io_uring_cqe *cQueue = NULL;
    int waitErr = io_uring_wait_cqe(ring, &cQueue);

    if (waitErr != 0) {
        perror(strerror(-waitErr));
        exit(1);
    }

    if (cQueue->res < 0) {
        perror(strerror(-cQueue->res));
        exit(1);
    }

    io_uring_cqe_seen(ring, cQueue);
}
```
Note that there are 2 possible failures, one on the wait operation itself and another, in the `res` field, that was the operation previously submitted. In this case, the accept operations.

With that we can accept multiple connections and the accepted socket (the new connection) will be returned in `cQueue->res`.

After the event is processed, `io_uring_cqe_seen` must be called to mark the entry as completed and allow it to be reused.

### Performing other operations

Now that we have the ability to accept socket connections, let's try reading and writing to it and will be doing that by submitting read and write operations to the ring.

Which means we need to know which operation was returned by `io_uring_wait_cqe` since it could be any.
Luckily (actually, by design) the submit queue entry has a `user_data` field that we can use to set any content intended to be passed to a completion queue entry, also in the `user_data` field.

Though `user_data` is an integer field, it can also be, coerced, into taking a pointers and `liburing` offers 2 functions to help with that: `io_uring_sqe_set_data` and `io_uring_cqe_get_data`.

Let's modify the code above to make use of `user_data` to identify completed operations.

```
typedef enum{
  EventTypeAccept,
} EventType;

typedef struct {
  EventType eventType;
} EventData;

struct io_uring_sqe *sQueue = io_uring_get_sqe(ring);
EventData acceptEvent;
acceptEvent.eventType = EventTypeAccept;

// socketDesc is the listening socket
io_uring_prep_multishot_accept_direct(sQueue, socketDesc, NULL, NULL, 0);

// user_data must be set after prep commands because they will clear various
// fields, including user_data
io_uring_sqe_set_data(sQueue, &acceptEvent);

io_uring_submit(ring);

while(true){
    struct io_uring_cqe *cQueue = NULL;
    int waitErr = io_uring_wait_cqe(ring, &cQueue);

    if (waitErr != 0) {
        perror(strerror(-waitErr));
        exit(1);
    }

    if (cQueue->res < 0) {
        perror(strerror(-cQueue->res));
        exit(1);
    }

    EventData* cEvent = (EventData*)io_uring_cqe_get_data(cQueue);

    switch (cEvent->eventType){
    case EventTypeAccept:
        int clientSocket = cQueue->res;

        // do something with clientSocket
        break;
    }

    io_uring_cqe_seen(ring, cQueue);
}
```

Calls to `io_uring_sqe_set_data` must be set after prep commands because they will clear various fields in the submission queue entry, including `user_data`.


Now that we have everything setup, getting basic read and write functionality should be just a matter of submitting the proper operations and handling completion events. The `switch` block would look like this


```
typedef enum{
  EventTypeAccept,
  EventTypeRead,
  EventTypeWrite,
} EventType;

typedef struct {
  char* buffer;
  unsigned int bufferSize;
  int clientSocket;
  EventType eventType;
} EventData;

// ...
// accept, start event loop code above goes here
// ...

switch (cEvent->eventType){
case EventTypeAccept:
  // this is the accepted client socket
  int clientSocket = cQueue->res;

  struct io_uring_sqe *sQueue = io_uring_get_sqe(ring);

  // setup the buffer we'll use to read and attach it to the data we're using to track events
  EventData *eventData = malloc(sizeof(EventData));
  eventData->eventType = EventTypeRead;
  const unsigned int bufferSize = 1024;
  eventData->buffer = malloc(bufferSize);
  eventData->bufferSize = bufferSize;

  // we need to know which socket the event is coming from if we want to continue performing operations on it
  eventData->clientSocket = clientSocket;

  io_uring_prep_read(sQueue, clientSocket, eventData->buffer, bufferSize, 0);
  io_uring_sqe_set_data(sQueue, eventData);

  sQueue->flags = sQueue->flags | IOSQE_FIXED_FILE;

  io_uring_submit(ring);
  break;

case EventTypeRead:
  // here cEvent->buffer will contain received data and
  // cQueue->res the number of received bytes
  // In a real application, we'd do something with it

  // keep the client socket, will need it
  int readSocket = cEvent->clientSocket;

  // you'll also not want to be allocating and freeing memory in the event loop
  // but this should illustrate the work needed
  free(cEvent->buffer);
  free(cEvent);

  // let's write a response back

  struct io_uring_sqe *rsq = io_uring_get_sqe(ring);
  EventData *rEventData = malloc(sizeof(EventData));
  rEventData->eventType = EventTypeWrite;

  const unsigned int wSize = 4;
  rEventData->buffer = malloc(bufferSize);
  rEventData->bufferSize = wSize;
  rEventData->clientSocket = readSocket;

  strncpy(erEventData->buffer, "ack\0", bufferSize);

  io_uring_prep_write(rsq, readSocket, rEventData->buffer, wSize, 0);
  io_uring_sqe_set_data(rsq, rEventData);
  rsq->flags = rsq->flags | IOSQE_FIXED_FILE;

  io_uring_submit(ring);
  break;

case EventTypeRead:
  // the response was sent, we can clean up resources and call it a day
  close(cEvent->clientSocket);
  free(cEvent->buffer);
  free(cEvent);
  break;
}

```

This may look like a lot but most of it is setup and boiler-plate code. It's not good code so don't take it as is and use as inspiration for a proper event handling mechanism.

You can find the [complete working code here](https://github.com/bignacio/cpp-articles/blob/main/code/io_uring_complete.c).

You may have noticed that `cQueue->res` contains the return value of read and write operations. That's a pattern `liburing` uses, `res` had the accepted socket from `io_uring_prep_multishot_accept_direct`, much like the `accept4` system call would. For `io_uring_prep_read`, it would be the number of bytes read, `io_uring_prep_write` for numbers of bytes written. So on and so forth.

Another thing to note in the code is the presence of this line

```
sQueue->flags = sQueue->flags | IOSQE_FIXED_FILE;
```

this is necessary for the kernel to know that we're using registered file descriptors instead of the standard ones. Check how we used `io_uring_register_files_sparse` in [the previous step](io_uring_journal_day03.md).
