// llist_lab.c
// CS4532 Concurrent Programming - Take Home Lab 1
// Implements: Serial, One-Mutex, RW-Lock for a sorted singly linked list
// Times ONLY the m operations region (per lab spec).

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <math.h>   // for sqrt

typedef struct node {
    int key;
    struct node* next;
} node_t;

// ------- global list head -------
static node_t* head = NULL;

// ------- single global mutex / rwlock -------
static pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t list_rwlock;

// ------- operation kinds -------
typedef enum { OP_MEMBER=0, OP_INSERT=1, OP_DELETE=2 } op_t;

typedef struct {
    op_t type;
    int  value;
} task_t;

// ------- config --------
typedef enum { MODE_SERIAL=0, MODE_MUTEX=1, MODE_RWLOCK=2 } run_mode_t;

typedef struct {
    int thread_id;
    int start_idx;   // inclusive
    int end_idx;     // exclusive
    task_t* ops;     // shared array of m ops
    run_mode_t mode;
} thread_arg_t;

// ------- utilities -------
static inline uint64_t nsecs_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000000ull + (uint64_t)ts.tv_nsec;
}

// ------- basic (unsynchronized) linked-list operations on global head -------

static bool Member_unsync(int value) {
    node_t* curr = head;
    while (curr && curr->key < value) curr = curr->next;
    return curr && curr->key == value;
}

static bool Insert_unsync(int value) {
    node_t* curr = head;
    node_t* pred = NULL;

    while (curr && curr->key < value) { pred = curr; curr = curr->next; }
    if (curr && curr->key == value) return false; // already present

    node_t* n = (node_t*)malloc(sizeof(node_t));
    if (!n) { perror("malloc"); exit(1); }
    n->key = value;
    n->next = curr;
    if (pred == NULL) head = n; else pred->next = n;
    return true;
}

static bool Delete_unsync(int value) {
    node_t* curr = head;
    node_t* pred = NULL;

    while (curr && curr->key < value) { pred = curr; curr = curr->next; }
    if (!curr || curr->key != value) return false; // not found

    if (pred == NULL) head = curr->next; else pred->next = curr->next;
    free(curr);
    return true;
}

// ------- synchronized wrappers (one global mutex) -------
static bool Member_mutex(int value) {
    pthread_mutex_lock(&list_mutex);
    bool r = Member_unsync(value);
    pthread_mutex_unlock(&list_mutex);
    return r;
}
static bool Insert_mutex(int value) {
    pthread_mutex_lock(&list_mutex);
    bool r = Insert_unsync(value);
    pthread_mutex_unlock(&list_mutex);
    return r;
}
static bool Delete_mutex(int value) {
    pthread_mutex_lock(&list_mutex);
    bool r = Delete_unsync(value);
    pthread_mutex_unlock(&list_mutex);
    return r;
}

// ------- synchronized wrappers (one global rwlock) -------
static bool Member_rw(int value) {
    pthread_rwlock_rdlock(&list_rwlock);
    bool r = Member_unsync(value);
    pthread_rwlock_unlock(&list_rwlock);
    return r;
}
static bool Insert_rw(int value) {
    pthread_rwlock_wrlock(&list_rwlock);
    bool r = Insert_unsync(value);
    pthread_rwlock_unlock(&list_rwlock);
    return r;
}
static bool Delete_rw(int value) {
    pthread_rwlock_wrlock(&list_rwlock);
    bool r = Delete_unsync(value);
    pthread_rwlock_unlock(&list_rwlock);
    return r;
}

// ------- thread function -------
static void* worker(void* argp) {
    thread_arg_t* a = (thread_arg_t*)argp;

    for (int i = a->start_idx; i < a->end_idx; i++) {
        task_t t = a->ops[i];
        switch (a->mode) {
            case MODE_SERIAL:
                if (t.type == OP_MEMBER) (void)Member_unsync(t.value);
                else if (t.type == OP_INSERT) (void)Insert_unsync(t.value);
                else (void)Delete_unsync(t.value);
                break;
            case MODE_MUTEX:
                if (t.type == OP_MEMBER) (void)Member_mutex(t.value);
                else if (t.type == OP_INSERT) (void)Insert_mutex(t.value);
                else (void)Delete_mutex(t.value);
                break;
            case MODE_RWLOCK:
                if (t.type == OP_MEMBER) (void)Member_rw(t.value);
                else if (t.type == OP_INSERT) (void)Insert_rw(t.value);
                else (void)Delete_rw(t.value);
                break;
        }
    }
    return NULL;
}

// ------- helpers -------
static void free_list(void) {
    node_t* c = head;
    while (c) { node_t* n = c->next; free(c); c = n; }
    head = NULL;
}

static void populate_unique_random(int n, int maxv, unsigned int seed) {
    // Not timed: pre-populate unique random values
    int inserted = 0;
    while (inserted < n) {
        int val = rand_r(&seed) & (maxv - 1);
        if (Insert_unsync(val)) inserted++;
    }
}

static void build_ops(task_t* ops, int m, double mMember, double mInsert, double mDelete, unsigned int seed) {
    int target_member = (int)(m * mMember + 0.5);
    int target_insert = (int)(m * mInsert + 0.5);
    int target_delete = m - target_member - target_insert;

    int idx = 0;
    for (int i=0;i<target_member;i++) { ops[idx].type = OP_MEMBER; idx++; }
    for (int i=0;i<target_insert;i++) { ops[idx].type = OP_INSERT; idx++; }
    for (int i=0;i<target_delete;i++) { ops[idx].type = OP_DELETE; idx++; }
    // shuffle
    for (int i=m-1;i>0;i--) {
        int j = rand_r(&seed) % (i+1);
        task_t tmp = ops[i]; ops[i] = ops[j]; ops[j] = tmp;
    }
    // assign random values in [0, 2^16)
    for (int i=0;i<m;i++) ops[i].value = rand_r(&seed) & ((1<<16)-1);
}

// ------- run one experiment -------
static double run_once(run_mode_t mode, int thread_count, int m, task_t* ops)
{
    if (mode == MODE_RWLOCK) pthread_rwlock_init(&list_rwlock, NULL);

    pthread_t* th = (pthread_t*)malloc(sizeof(pthread_t)*thread_count);
    thread_arg_t* args = (thread_arg_t*)malloc(sizeof(thread_arg_t)*thread_count);
    if (!th || !args) { perror("malloc"); exit(1); }

    int base = 0;
    int chunk = m / thread_count;
    int rem = m % thread_count;

    uint64_t t0 = nsecs_now();

    for (int t=0;t<thread_count;t++) {
        int len = chunk + (t < rem ? 1 : 0);
        args[t].thread_id = t;
        args[t].start_idx = base;
        args[t].end_idx   = base + len;
        args[t].ops = ops;
        args[t].mode = mode;
        base += len;

        if (mode == MODE_SERIAL && thread_count==1) {
            worker(&args[t]); // no thread creation
        } else {
            int rc = pthread_create(&th[t], NULL, worker, &args[t]);
            if (rc) { fprintf(stderr, "pthread_create: %s\n", strerror(rc)); exit(1); }
        }
    }

    if (!(mode == MODE_SERIAL && thread_count==1)) {
        for (int t=0;t<thread_count;t++) pthread_join(th[t], NULL);
    }

    uint64_t t1 = nsecs_now();

    if (mode == MODE_RWLOCK) pthread_rwlock_destroy(&list_rwlock);
    free(th); free(args);

    return (t1 - t0) / 1e6; // milliseconds
}

// Usage:
//   ./llist_lab <mode> <threads> <n> <m> <mMember> <mInsert> <mDelete> <runs>
// Modes: serial | mutex | rwlock
int main(int argc, char** argv) {
    if (argc < 9) {
        fprintf(stderr, "Usage: %s <mode: serial|mutex|rwlock> <threads> <n> <m> <mMember> <mInsert> <mDelete> <runs>\n", argv[0]);
        return 1;
    }
    const char* mode_s = argv[1];
    int threads = atoi(argv[2]);
    int n = atoi(argv[3]);
    int m = atoi(argv[4]);
    double mMember = atof(argv[5]);
    double mInsert = atof(argv[6]);
    double mDelete = atof(argv[7]);
    int runs = atoi(argv[8]);

    run_mode_t mode;
    if (!strcmp(mode_s,"serial")) mode = MODE_SERIAL;
    else if (!strcmp(mode_s,"mutex")) mode = MODE_MUTEX;
    else if (!strcmp(mode_s,"rwlock")) mode = MODE_RWLOCK;
    else { fprintf(stderr,"Unknown mode\n"); return 2; }

    // seed variation per run
    unsigned int seed0 = (unsigned int)time(NULL);

    // Pre‑populate list with n unique values in [0, 2^16)
    populate_unique_random(n, 1<<16, seed0 ^ 0x1234567u);

    task_t* ops = (task_t*)malloc(sizeof(task_t)*m);
    if (!ops) { perror("malloc"); return 1; }

    double *samples = (double*)malloc(sizeof(double)*runs);
    if (!samples) { perror("malloc"); return 1; }

    for (int r=0; r<runs; r++) {
        build_ops(ops, m, mMember, mInsert, mDelete, seed0 + r*1337u);
        double ms = run_once(mode, threads, m, ops);  // time only m ops
        samples[r] = ms;
    }

    // Compute mean and std
    double sum = 0.0, sum2 = 0.0;
    for (int r=0;r<runs;r++){ sum += samples[r]; sum2 += samples[r]*samples[r]; }
    double mean = sum / runs;
    double var = (sum2/runs) - (mean*mean);
    double sd = (var > 0 ? sqrt(var) : 0);

    printf("Mode=%s Threads=%d n=%d m=%d mMember=%.3f mInsert=%.3f mDelete=%.3f Runs=%d\n",
           mode_s, threads, n, m, mMember, mInsert, mDelete, runs);
    printf("Average(ms)=%.3f  StdDev(ms)=%.3f\n", mean, sd);

    free(samples);
    free(ops);
    free_list();
    return 0;
}
