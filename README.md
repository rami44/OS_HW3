# OS_HW3
Operating Systems HW3

# Multi-Threaded Web Server in C

A fast, concurrent web server written in C with:

- 🧵 Thread pool for handling multiple requests
- ⭐ VIP request prioritization
- 🔒 Condition variable–based synchronization
- ⚠️ Overload handling (drop tail/head/random, block)
- 📊 Per-request/thread usage statistics

## Features
- Efficient multi-threading with low overhead
- Priority scheduling for VIP clients
- Detailed tracking: arrival time, dispatch time, thread ID
- Safe concurrent queue with mutexes + condition variables

## Build & Run
```bash
make
./server <port> <thread_count> <queue_size> <overload_policy>
