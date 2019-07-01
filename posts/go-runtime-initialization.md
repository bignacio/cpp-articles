# The initialization of Dynamic Shared Objects built in Go[lang]
**Disclaimer**: this post isn't really about C++ but I hope you'll find it useful in any case ;)


When I first heard that dynamically loading libraries built in Go take much longer than one written in C or C++, my first reaction was one of surprise.

The second was: of course!
And although it made sense I wanted to find out the reasons behind it.

So I went to compare what happens when loading a shared library object built in Go and another in C++, compiled with gcc.
For clarity, both both cases built for and running on linux 64bit using Go version 1.11.

## How are go binaries generated
Go works with the concept of packages, that (normally for the most part) are also written in go. In the default [build mode](https://golang.org/cmd/go/#hdr-Build_modes) is generate archive (static linked) libraries. These libraries are then, later in the build process, linked against the main package(s).

The same thing happens with the go run time code: all is statically included into the final executable or library. And that is a good thing.

Running `ldd`  on an executable would probably look like this.

```
$ ldd go-executable     
	linux-vdso.so.1 (0x00007fffc7fe9000)
	libpthread.so.0 => /lib/x86_64-linux-gnu/libpthread.so.0 (0x00007f84d3dbb000)
	libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f84d3bd1000)
	/lib64/ld-linux-x86-64.so.2 (0x00007f84d3df4000)

```

Naturally there are exceptions, but we'll save that discussion for another day.


## Setting up the test

The idea is very simple but sufficient (or so I hope): write 2 functionally identical libraries, one in Go and the other in C++. 
Then a third program to load and use these libraries. This is the program we'll be profiling and looking for .

I say functionally because obviously the code is different but the exported functions do the same thing.

This is the source code for `testgo.go`

```go
package main

import (
  "C"
)

//export GetSomeInfo
func GetSomeInfo() *C.char {
  return C.CString("this is a go library")
}

func main() {}
```

and here its C++ counterpart, `testcpp.cpp`

```c++
#include <string>

static std::string somethingToBeReturned("this is a C++ library");

extern "C"{

  __attribute__ ((visibility ("default")))
  const char* GetSomeInfo(){
    return somethingToBeReturned.c_str();
  }
}
```

Very simple. 

The Go version of the library was compiled with 

`go build o libtestgo.so -buildmode=c-shared testgo.go`

And the C++ version compiled with gcc with some extra arguments to match the default go compilation. 

`g++  testcpp.cpp -g -shared -o libtestcpp.so -fpic -m64 -fmessage-length=0`

To what Go build defaults are, run `go env` and check the value of the variable `GOGCCFLAGS`.


Finally, the program to load the libraries, `loader.cpp`

```c++
#include <iostream>
#include <string>
#include <dlfcn.h>

#define CPPLIBNAME "./libtestcpp.so"
#define GOLIBNAME "./libtestgo.so"

char* (*getSomeInfo)();

void handleError(){
  char* error = dlerror();
  if(error != NULL){
    std::cerr << "Error loading lib " << error << std::endl;
  }
}

void loadLib(const char* libName){  
  void* handle = NULL;
  handle = dlopen(libName, RTLD_NOW);
  handleError();
  getSomeInfo = (char*(*)())dlsym(handle, "GetSomeInfo");

  handleError();

  std::cout << "some info is '" << (*getSomeInfo)() << "'" << std::endl;
  dlclose(handle);
}

void cppLoad(){
  loadLib(CPPLIBNAME); 
}

void goLoad(){
  loadLib(GOLIBNAME);
}

int main(int argc, char** argv){
  if(argv[1][0] == 'c'){
    cppLoad();
  }else{
    goLoad();
  }
}

```

A bit of hacked code but it will suffice.

Note the argument `c` that tells our loader to, well, load the C++ library otherwise, the go library. We'll use this below.

## Measuring the library load time

My first attempt to benchmark load time was to use one of my favourite frameworks [Google Benchmark](https://github.com/google/benchmark) but that would require the library loading code to be executed multiple times.
However, my instincts were telling me that anything that was happening behind the scenes during the library initialization would happen only once and it would be hard to work around that.

It turns out those instincts were right, for once. So I turned to another favourite, linux `perf`.


First, measuring loading time for each library. We'll do that with `perf stat`.

Ideally we'd this repeated times so that things that would interfere with the results would get diluted but since we're comparing two result sets, a few runs should be sufficient to give us confidence.

Measuring the load time of the C++ library

```bash
$ perf stat -- ./loader c
some info is 'this is a C++ library'

 Performance counter stats for './loader c':

          1.460310      task-clock (msec)         #    0.824 CPUs utilized          
                 0      context-switches          #    0.000 K/sec                  
                 0      cpu-migrations            #    0.000 K/sec                  
               142      page-faults               #    0.097 M/sec                  
         3,229,283      cycles                    #    2.211 GHz                    
         3,558,250      instructions              #    1.10  insn per cycle         
           633,848      branches                  #  434.050 M/sec                  
            19,978      branch-misses             #    3.15% of all branches        

       0.001771322 seconds time elapsed

       0.001778000 seconds user
       0.000000000 seconds sys
 ```

 Now, for the load time of the Go library

 ```bash
some info is 'this is a go library'

 Performance counter stats for './loader g':

          3.568294      task-clock (msec)         #    1.006 CPUs utilized          
                32      context-switches          #    0.009 M/sec                  
                 3      cpu-migrations            #    0.841 K/sec                  
               387      page-faults               #    0.108 M/sec                  
         6,418,891      cycles                    #    1.799 GHz                    
         5,798,816      instructions              #    0.90  insn per cycle         
         1,078,772      branches                  #  302.322 M/sec                  
            31,764      branch-misses             #    2.94% of all branches        

       0.003547798 seconds time elapsed

       0.000000000 seconds user
       0.004151000 seconds sys

 ```

See [the documentation page](https://perf.wiki.kernel.org/index.php/Main_Page) for more details on `perf`.

There are two things immediately visible in the results: task-clock and instructions. The task clock tells us that loading the Go library takes twice as long as the C++ library.

The number of instructions executed indicates there's a lot more going on when the library built in Go is loaded.

Also, note the number of context-switches and CPU migrations. This suggests that the initialization of the Go library is probably creating (and running) threads.

Now, let's try and find explanation for these differences.

## The Initialization of the Go Runtime

To find out what the `loader` application is doing, profiling the application is a good, time-tested approach. And for that, I resort to `perf` once more.
We'll use a combination of `perf record` with `perf report`.

Remember, we're analyzing our library loader with the Go version of our test shared object.


```
$ perf record  -Fmax  --strict-freq -g --running-time --call-graph dwarf -- ./loader g
[ perf record: Woken up 3 times to write data ]
[ perf record: Captured and wrote 2.320 MB perf.data (305 samples) ]


$ perf report
-    8.26%     0.00%  loader  [.] runtime.rt0_go
   - runtime.rt0_go
      - 5.08% runtime.schedinit
         + 2.98% runtime.mcommoninit
         + 1.27% runtime.mallocinit
         + 0.83% runtime.modulesinit
      + 0.92% runtime.mstart
      + 0.91% runtime.newproc
      + 0.80% x_cgo_init
      + 0.55% runtime.check
+    6.01%     0.00%  loader  [.] runtime.malg
+    5.08%     0.00%  loader  [.] runtime.schedinit
+    4.83%     0.00%  loader  [.] threadentry
+    4.78%     0.00%  loader  [.] runtime.mstart
+    4.78%     0.00%  loader  [.] runtime.mstart1
+    4.20%     0.00%  loader  [.] runtime.mcommoninit
+    3.86%     0.00%  loader  [.] crosscall_amd64
+    3.58%     0.00%  loader  [.] runtime.newm
+    3.56%     0.00%  loader  [.] runtime.newobject
+    3.56%     0.00%  loader  [.] runtime.mallocgc
+    3.56%     0.00%  loader  [.] runtime.(*mcache).nextFree
+    3.56%     0.00%  loader  [.] runtime.(*mcache).nextFree.func1
+    3.56%     0.00%  loader  [.] runtime.(*mcache).refill
+    3.56%     0.00%  loader  [.] runtime.(*mcentral).cacheSpan
+    3.56%     0.00%  loader  [.] runtime.(*mcentral).grow
+    3.36%     0.31%  loader  [.] runtime.schedule
+    3.29%     0.00%  loader  [.] runtime.asmcgocall
+    3.27%     0.00%  loader  [.] runtime.mpreinit
+    3.23%     0.00%  loader  [.] runtime.main
+    3.04%     0.26%  loader  [.] runtime.(*mheap).allocSpanLocked
+    3.03%     0.00%  loader  [.] runtime.malg.func1
+    2.91%     0.00%  loader  [.] runtime.startm
+    2.77%     0.00%  loader  [.] runtime.newproc.func1
+    2.77%     0.00%  loader  [.] runtime.newproc1
+    2.76%     0.00%  loader  [.] runtime.(*mheap).alloc
+    2.76%     0.00%  loader  [.] runtime.(*mheap).alloc.func1
+    2.76%     0.00%  loader  [.] runtime.(*mheap).alloc_m
+    2.66%     0.00%  loader  [.] _cgo_sys_thread_start
+    2.66%     0.00%  loader  [.] _cgo_try_pthread_create
+    2.56%     0.00%  loader  [.] runtime.gcenable
+    2.56%     0.00%  loader  [.] runtime.chanrecv1
+    2.56%     0.00%  loader  [.] runtime.chanrecv
+    2.37%     0.00%  loader  [.] _rt0_amd64_lib
```

Above is the call stack after the code executed through perf's instrumentation process, ordered by percentage execution time.

If you've used perf, you are probably well aware that it is awesome but not perfect so the best way to interpret these results are by comparing function calls relative to one another. I cleaned up the output a little so as to focus on the shared object (library) we're loading. The top one on the list is expanded and we'll talk a little more about those calls. The remaining lines I left for *scientific purposes*. 

All the calls in the list can aide further investigation steps.

Let's start with `runtime.rt0_go`, simply because it's the first entry in the list :). Let's see what was happening before that call was executed.

For that, we'll use `gdb` and put a break point on `runtime.rt0_go`

```
$ gdb --args ./loader g
(gdb) break runtime.rt0_go
Breakpoint 1 (runtime.rt0_go) pending.
(gdb) r

New Thread 0x7ffff793c700 (LWP 27545)]
[Switching to Thread 0x7ffff793c700 (LWP 27545)]

Thread 2 "loader" hit Breakpoint 1, runtime.rt0_go () at /usr/lib/go-1.11/src/runtime/asm_amd64.s:89
89		MOVQ	DI, AX		// argc
(gdb) bt
#0  runtime.rt0_go () at /usr/lib/go-1.11/src/runtime/asm_amd64.s:89
#1  0x00007ffff7e0c164 in start_thread (arg=<optimized out>) at pthread_create.c:486
#2  0x00007ffff7d34def in clone () at ../sysdeps/unix/sysv/linux/x86_64/clone.S:95

```

Not quite what I was expecting, none of the code we wrote is creating threads so all indicates the go runtime is doing it. Which means `runtime.rt0_go` is not actually the code entrypoint when our library is loaded.

What's the Go code entrypoint for this library then? Looking at the output from perf, another interesting candidate is `_rt0_amd64_lib`. Putting a breakpoint on it will help clarifying things.

```
(gdb) break _rt0_amd64_lib
Breakpoint 1 at 0x7ffff79c6d10: file /usr/lib/go-1.11/src/runtime/asm_amd64.s, line 31.
(gdb) r
Breakpoint 1, _rt0_amd64_lib () at /usr/lib/go-1.11/src/runtime/asm_amd64.s:31
31	TEXT _rt0_amd64_lib(SB),NOSPLIT,$0x50
(gdb) bt
#0  _rt0_amd64_lib () at /usr/lib/go-1.11/src/runtime/asm_amd64.s:31
#1  0x00007ffff7fe398a in call_init (l=<optimized out>, argc=argc@entry=2, argv=argv@entry=0x7fffffffde48, env=env@entry=0x7fffffffde60) at dl-init.c:72
#2  0x00007ffff7fe3a89 in call_init (env=0x7fffffffde60, argv=0x7fffffffde48, argc=2, l=<optimized out>) at dl-init.c:30
#3  _dl_init (main_map=main_map@entry=0x55555556aec0, argc=2, argv=0x7fffffffde48, env=0x7fffffffde60) at dl-init.c:119
#4  0x00007ffff7fe7cdc in dl_open_worker (a=a@entry=0x7fffffffdae0) at dl-open.c:517
#5  0x00007ffff7d7648f in __GI__dl_catch_exception (exception=<optimized out>, operate=<optimized out>, args=<optimized out>) at dl-error-skeleton.c:196
#6  0x00007ffff7fe72c6 in _dl_open (file=0x555555556045 "./libtestgo.so", mode=-2147483646, caller_dlopen=0x555555555330 <loadLib(char const*)+16>, nsid=<optimized out>, argc=2, argv=0x7fffffffde48, 
    env=0x7fffffffde60) at dl-open.c:599
#7  0x00007ffff7fb1256 in dlopen_doit (a=a@entry=0x7fffffffdd00) at dlopen.c:66
#8  0x00007ffff7d7648f in __GI__dl_catch_exception (exception=exception@entry=0x7fffffffdca0, operate=<optimized out>, args=<optimized out>) at dl-error-skeleton.c:196
#9  0x00007ffff7d7651f in __GI__dl_catch_error (objname=0x55555556ae80, errstring=0x55555556ae88, mallocedp=0x55555556ae78, operate=<optimized out>, args=<optimized out>) at dl-error-skeleton.c:215
#10 0x00007ffff7fb1a25 in _dlerror_run (operate=operate@entry=0x7ffff7fb1200 <dlopen_doit>, args=args@entry=0x7fffffffdd00) at dlerror.c:163
#11 0x00007ffff7fb12e6 in __dlopen (file=<optimized out>, mode=<optimized out>) at dlopen.c:87
#12 0x0000555555555330 in loadLib (libName=<optimized out>) at loader.cpp:19
#13 0x0000555555555129 in goLoad () at loader.cpp:43
#14 main (argc=<optimized out>, argv=<optimized out>) at loader.cpp:43

```

That's more like it! As we go down the stack trace we see the familiar function calls we wrote in our loader application. We found it `_rt0_amd64_lib` is the first piece of code invoked when a shared library built in Go is loaded.

For completeness, the entrypoint of a linux Go executable is `_rt0_amd64_linux`. It will likely be different for each platform.

### It takes the time it takes

Alright, all this reverse engineering got us on the right track. Let's look at some source code

We saw that the library entry point `_rt0_amd64_lib` is in the file `asm_amd64.s` and the source code for it [can be found here](https://github.com/golang/go/blob/master/src/runtime/asm_amd64.s).

Here's a few interesting bits 
```
TEXT _rt0_amd64_lib(SB),NOSPLIT,$0x50
  // some code here
	...

	MOVQ	DI, _rt0_amd64_lib_argc<>(SB)
	MOVQ	SI, _rt0_amd64_lib_argv<>(SB)

	// Synchronous initialization.
	CALL	runtime·libpreinit(SB)

	// Create a new thread to finish Go runtime initialization.
	MOVQ	_cgo_sys_thread_create(SB), AX
	TESTQ	AX, AX
	JZ	nocgo
	MOVQ	$_rt0_amd64_lib_go(SB), DI
	MOVQ	$0, SI
	CALL	AX
  
  // more code here
  ...
```

First, the fact there's a pre-initialization step specific for libraries (build type archive or shared) that is called very early on. The the version of Go we're using in this post, all that is happening at this stage is the initialization of signal handlers.

Simple, but not necessarily cheap; handlers are installed for every signal the underlying OS supports and that happens via a system call, which makes it a fairly expensive operation.
In this example, 66 system calls were made to install Go's signal handlers.

So that's one of the things adding to the initialization burden, but not worse.

The second interesting thing is that a good chunk of the initialization happens in the background by executing `_rt0_amd64_lib_go`, which in turn simply invokes `runtime·rt0_go` where the rest of the initialization happens.
This means that, at some point, this thread has to wait for the initialization tom complete before the code we wrote can actually be executed. This waiting happens in [`_cgo_wait_runtime_init_done`](https://github.com/golang/go/blob/9586c093a2e65cb8edd73a4dd0a6a18823249cf4/src/runtime/cgo/gcc_libinit.c) and this behaviour [is nicely documented in the code](https://github.com/golang/go/blob/14560da7e469aff46a6f1270ce84204bbd6ffdb3/src/runtime/cgo/callbacks.go#L80).


A lot happens in `runtime·rt0_go` but the two most interesting calls (in our case, that is) are the calls to `runtime·osinit` and `runtime·schedinit`. The runtime bootstrap order is documented in [proc.go](https://github.com/golang/go/blob/47df542fefc355d65659278761d06cb9d5eba235/src/runtime/proc.go). This is where the fun begins. Take a look at list of functions invoked in `schedinit`; from that we can get a good indication of what's happening and what are the more expensive calls.

```go
func schedinit() { 
  // some code here
  ...
  
  tracebackinit()
  moduledataverify()
  stackinit()
  mallocinit()
  mcommoninit(_g_.m)
  cpuinit()
  alginit()
  modulesinit()
  typelinksinit()
  itabsinit()

  msigsave(_g_.m)
  initSigmask = _g_.m.sigmask

  goargs()
  goenvs()
  parsedebugvars()
  gcinit()
  
  // and more code here
  ...

```

We could talk about the things that happen in `schedinit` for days so let's focus on memory management: allocation and garbage collection.

### Memory

If we look back at the `perf` results above, a great number of functions that top the list are doing something related to memory allocation or are "slow" because they are invoking memory allocation.

Allocating memory is expensive and managing it is not any less trivial. The Go runtime has a garbage collector - for the happiness joy of us developers - and if you ever used any language that relies on GC (mostly likely java), you know there's a lot of setup and pain along the lifetime of an executing program.

On top of that, Go has a mechanism to grow the size of goroutines stack dynamically, in order to have the creation of goroutines very efficient. Stack growth happens by acquiring memory from a pre-allocated stack pool.

The creation of the pool happens in the function [`stackinit()`](https://github.com/golang/go/blob/7ed7669c0d35768dbb73eb33d7dc0098e45421b1/src/runtime/stack.go#L150) we saw above in `schedinit`. The stack management is a very interesting topic and important to know if you're trying to get every bit of performance from your Go programs. Since we're not to explore it further in this post, I suggest treading through the comments and code in [stack.go](https://golang.org/src/runtime/stack.go)

Keep in mind that the number of processor cores affects the stack initialization and management, since the runtime creates a thread pool for the execution of goroutines and this thread pool is sensitive to the number of processor cores.

Continuing on, we have `mallocinit()` that does a loot of complicated and cool stuff and that includes the creation of per threads arenas, later used for memory allocation. Here also the address spaces used by the runtime are defined, the heap (important for GC) and cache are initialized. 

Running a garbage collected environment, Go programs do not allocate memory directly from the OS; instead, memory is "handed" over to the executing program by Go's memory manager. The requested blocks of memory are reserved in the heap given its size. Each size defined by its span class and it can range from small to large. 
By pre-allocating memory in a way that best serve these classes, the Go runtime can make memory accessible to programs much more quickly than if it always tried to match the requested size precisely.

This allocation (and more) happen in [`(*mheap) init()`](https://github.com/golang/go/blob/7ed7669c0d35768dbb73eb33d7dc0098e45421b1/src/runtime/mheap.go). Take a look at the code and you'll see other memory blocks being reserved for other functions.

Finally on our list is the initialization of the garbage collector in [`gcinit()`](https://github.com/golang/go/blob/7ed7669c0d35768dbb73eb33d7dc0098e45421b1/src/runtime/mgc.go) 
Here the GC threads (sweep, mark, cleanup) are initialized as well as the thresholds are defined.


## Final considerations 
We've covered a lot of ground already and I will stop here, for now.

Go doesn't give us many options to tweak the runtime initialization - perhaps for the best - so there's not much we can do in that sense.
We can however be mindful of this fact and how hardware configuration will perform differently and **especially** aware of packages we import in our program.
For example, look at the hot spots (output from `perf` again) in the runtime initialization when gzip is imported in our test library.

```
-    7.82%     0.00%  loader  [.] runtime.main
   - runtime.main
      - 6.84% main.init
         - 6.19% compress/gzip.init
            - 4.35% bufio.init
                 bytes.init
               + unicode.init
            + 1.20% compress/flate.init
            + 0.64% hash/crc32.init
         + 0.65% syscall.init
      + 0.99% runtime.gcenable
```

See how the compress and buffer packages add to the overall initialization time substantially.


The initialization of the Go runtime maybe not as fast as one would like but it does what it has to do only once so that the user code, the code we write, can perform well.

### References
* [A Quick Guide to Go's Assembler](https://golang.org/doc/asm)
* [GopherCon 2016: Rob Pike - The Design of the Go Assembler](https://www.youtube.com/watch?v=KINIAgRpkDA)
* [Go Internals](https://blog.altoros.com/golang-part-1-main-concepts-and-project-structure.html)


<br>

*Bira Ignacio*