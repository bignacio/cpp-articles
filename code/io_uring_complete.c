#include <liburing.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// compile with -luring

typedef enum {
  EventTypeAccept,
  EventTypeRead,
  EventTypeWrite,
} EventType;

typedef struct {
  char *buffer;
  unsigned int bufferSize;
  int clientSocket;
  EventType eventType;
} EventData;

int startListeningSocket() {
  const in_port_t port = 4242;

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("Error creating socket");
    exit(1);
  }

  int optState = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optState,
                 sizeof(optState)) != 0) {
    perror("Error setting socket options");
    exit(1);
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Error binding socket to address");
    exit(1);
  }

  // Listen for incoming connections
  if (listen(sockfd, 5) < 0) {
    perror("Error listening for incoming connections");
    exit(1);
  }

  printf("Socket listening on port %d\n", port);

  return sockfd;
}

void setupRing(struct io_uring *ring) {
  struct io_uring_params uringParams;
  memset(&uringParams, 0, sizeof(struct io_uring_params));

  uringParams.sq_thread_idle =
      2; // millis before putting the worker thread to sleep

  int errorCode = io_uring_queue_init_params(8, ring, &uringParams);

  if (errorCode != 0) {
    perror(strerror(-errorCode));
    exit(1);
  }

  errorCode = io_uring_register_ring_fd(ring);
  if (errorCode != 1) {
    perror(strerror(-errorCode));
    exit(1);
  }

  errorCode = io_uring_register_files_sparse(ring, 8);
  if (errorCode != 0) {
    perror(strerror(-errorCode));
    exit(1);
  }
}

void startEventLoop(struct io_uring *ring, int serverSocket) {
  struct io_uring_sqe *sQueue = io_uring_get_sqe(ring);
  io_uring_prep_multishot_accept_direct(sQueue, serverSocket, NULL, NULL, 0);

  // user_data must be set after prep commands because they will clear various
  // fields, including user_data
  EventData acceptEvent;
  acceptEvent.eventType = EventTypeAccept;
  io_uring_sqe_set_data(sQueue, &acceptEvent);

  if (io_uring_submit(ring) != 1) {
    exit(1);
  }

  while (true) {
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

    EventData *cEvent = (EventData *)io_uring_cqe_get_data(cQueue);

    switch (cEvent->eventType) {
    case EventTypeAccept:
      // this is the accepted client socket
      int clientSocket = cQueue->res;

      struct io_uring_sqe *sQueue = io_uring_get_sqe(ring);

      // setup the buffer we'll use to read and attach it to the data we're
      // using to track events
      EventData *eventData = malloc(sizeof(EventData));
      eventData->eventType = EventTypeRead;
      const unsigned int bufferSize = 1024;
      eventData->buffer = malloc(bufferSize);
      eventData->bufferSize = bufferSize;

      // we need to know which socket the event is coming from if we want to
      // continue performing operations on it
      eventData->clientSocket = clientSocket;

      io_uring_prep_read(sQueue, clientSocket, eventData->buffer, bufferSize,
                         0);
      io_uring_sqe_set_data(sQueue, eventData);

      sQueue->flags = sQueue->flags | IOSQE_FIXED_FILE;

      io_uring_submit(ring);
      break;
    case EventTypeRead:
      // keep the client socket, will need it
      int readSocket = cEvent->clientSocket;

      int recvLen = cQueue->res;

      cEvent->buffer[recvLen] = 0;
      printf("Received '%s'\n", cEvent->buffer);

      // you'll not want to be allocating and freeing memory in the event
      // loop but this should illustrate the work needed
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

      strncpy(rEventData->buffer, "ack\0", bufferSize);

      io_uring_prep_write(rsq, readSocket, rEventData->buffer, wSize, 0);
      io_uring_sqe_set_data(rsq, rEventData);
      rsq->flags = sQueue->flags | IOSQE_FIXED_FILE;

      io_uring_submit(ring);
      break;
    case EventTypeWrite:
      close(cEvent->clientSocket);
      free(cEvent->buffer);
      free(cEvent);
      break;
    }

    io_uring_cqe_seen(ring, cQueue);
  }
}

int main() {
  int serverSocket = startListeningSocket();
  struct io_uring ring;
  memset(&ring, 0, sizeof(struct io_uring));
  setupRing(&ring);

  startEventLoop(&ring, serverSocket);
  return 0;
}
