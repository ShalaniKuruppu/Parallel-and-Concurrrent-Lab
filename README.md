

# Linked List Concurrency Lab

This lab benchmarks a sorted singly linked list under three synchronization strategies:

- Serial - A purely serial implementation, serving as a baseline.

- Mutex - A parallel implementation with one global mutex protecting the entire list.

- RWLock - A parallel implementation with a single read–write lock, where multiple threads can read in parallel, but inserts and deletes are exclusive.

---

## Compiling the Program (Windows + MinGW in VS Code)

We used Visual Studio Code with the MinGW compiler to build the program.

```bash
gcc -g -Wall -o pth_llist_lab llist_lab.c -lpthread
```


---

## Running the Program

```bash
./llist_lab <mode> <threads> <n> <m> <mMember> <mInsert> <mDelete> <runs>
```
---

## Arguments

- \<mode> - One of: serial / mutex / rwlock
- \<threads> - Number of worker threads (ignored for serial)
- \<n> - Initial number of nodes in the list
- \<m> - Total number of operations to perform
- \<mMember> - Fraction of operations that are Member() lookups
- \<mInsert> - Fraction of operations that are Insert()
- \<mDelete> - Fraction of operations that are Delete()
- \<runs> - Number of runs

---

## Example

```bash
./llist_lab rwlock 4 1000 10000 0.99 0.005 0.005 100
```

This runs the program in RWLock mode with 4 threads, an initial list size of 1000, 10,000 operations, 99% member, 0.5% insert, 0.5% delete, repeated 100 times.
