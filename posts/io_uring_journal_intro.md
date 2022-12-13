#  The Power of `io_uring` - A journey

Sometimes you really just need to make as few system calls as possible and ever since [io_uring was added](https://en.wikipedia.org/wiki/Io_uring){:target="_blank"} to Linux kernel 5.1, I've been wanting to try and use it for asynchronous network I/O instead of things like [epoll](https://man7.org/linux/man-pages/man7/epoll.7.html){:target="_blank"}, [select](https://man7.org/linux/man-pages/man2/select.2.html){:target="_blank"}, etc.

This led me to document this process and hopefully provide something useful to others doing the same (including future me).

For a very brief introduction to what `io_uring` is, here's a quote from [kernel patch](https://git.kernel.dk/cgit/linux-block/commit/?h=for-next&id=2b188cc1bb857a9d4701ae59aa7768b5124e262e) introducing the feature:

> With this setup, it's possible to do async IO with a single system call. Future developments will enable polled IO with this interface, and polled submission as well. The latter will enable an application to do IO without doing ANY system calls at all.

If you are not familiar with `io_uring`, I'd suggest taking a look at the [Lord of the io_uring guide](https://unixism.net/loti/index.html) first.

The posts that follow discuss the more practical aspects of implementing basic async network I/O with `io_uring` and will cover everything one can do with `io_uring` (which is a lot).

While the motivation for using `io_uring` is to have highly performant asynchronous I/O, this series will not contain any benchmarks or performance comparisons. There is a [very interesting discussion](https://github.com/axboe/liburing/issues/189) on this subject that's worth reading.

Here's my journal
1. [Day 01: a "hello world", of sorts](io_uring_journal_day01.md)
1. [Day 02: Initialization, parameters and kernel features](io_uring_journal_day02.md)
1. [Day 03: Accepting connections in the `io_uring` universe](io_uring_journal_day03.md)
1. [Day 04: An event loop for `io_uring`](io_uring_journal_day04.md)