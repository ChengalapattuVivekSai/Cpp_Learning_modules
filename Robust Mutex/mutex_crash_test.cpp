
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>


struct SharedData {
    pthread_mutex_t mutex;

    int value;
    int previous_value;
    int transaction_in_progress;
};

static void fail(const char* message) {
    perror(message);
    std::exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    if (argc != 2 ||
        (std::strcmp(argv[1], "normal") != 0 &&
         std::strcmp(argv[1], "robust") != 0)) {
        std::fprintf(stderr,
            "Usage: %s normal|robust\n", argv[0]);
        return EXIT_FAILURE;
    }

    const bool use_robust =
        std::strcmp(argv[1], "robust") == 0;

    // 1. Create memory shared by forked processes.
    void* memory = mmap(
        nullptr,
        sizeof(SharedData),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0
    );

    if (memory == MAP_FAILED)
        fail("mmap");

    auto* shared = static_cast<SharedData*>(memory);

    // 2. Configure a process-shared pthread mutex.
    pthread_mutexattr_t attr;
    int rc = pthread_mutexattr_init(&attr);

    if (rc != 0) {
        errno = rc;
        fail("pthread_mutexattr_init");
    }

    rc = pthread_mutexattr_setpshared(
        &attr, PTHREAD_PROCESS_SHARED);

    if (rc != 0) {
        errno = rc;
        fail("pthread_mutexattr_setpshared");
    }

    if (use_robust) {
        rc = pthread_mutexattr_setrobust(
            &attr, PTHREAD_MUTEX_ROBUST);

        if (rc != 0) {
            errno = rc;
            fail("pthread_mutexattr_setrobust");
        }
    }

    rc = pthread_mutex_init(&shared->mutex, &attr);

    if (rc != 0) {
        errno = rc;
        fail("pthread_mutex_init");
    }

    pthread_mutexattr_destroy(&attr);

    shared->value = 42;
    shared->previous_value = 42;
    shared->transaction_in_progress = 0;

    // Parent-child coordination: not the tested mutex.
    int ready_pipe[2];
    int result_pipe[2];

    if (pipe(ready_pipe) == -1)
        fail("ready pipe");

    if (pipe(result_pipe) == -1)
        fail("result pipe");

    std::printf("Mode: %s\n",
        use_robust ? "ROBUST" : "NORMAL");
    std::fflush(stdout);

    // 3. PROCESS 1: acquire the mutex and crash.
    pid_t p1 = fork();

    if (p1 == -1)
        fail("fork process 1");

    if (p1 == 0) {
        close(ready_pipe[0]);
        close(result_pipe[0]);
        close(result_pipe[1]);

        int lock_rc = pthread_mutex_lock(&shared->mutex);

        if (lock_rc != 0)
            _exit(10);

        std::printf("P1: acquired mutex\n");

        // Simulate a transaction that is not yet committed.
        shared->previous_value = shared->value;
        shared->transaction_in_progress = 1;
        shared->value = 999;

        std::printf(
            "P1: partial update: value=%d, in_progress=%d\n",
            shared->value,
            shared->transaction_in_progress
        );
        std::printf("P1: exiting WITHOUT unlocking mutex\n");
        std::fflush(stdout);

        char ready = 'R';
        if (write(ready_pipe[1], &ready, 1) != 1)
            _exit(11);

        close(ready_pipe[1]);

        // Simulate sudden process termination.
        // Deliberately do NOT call pthread_mutex_unlock().
        _exit(42);
    }

    close(ready_pipe[1]);

    char ready;
    ssize_t n = read(ready_pipe[0], &ready, 1);

    if (n != 1)
        fail("wait for process 1");

    close(ready_pipe[0]);

    int p1_status;
    if (waitpid(p1, &p1_status, 0) == -1)
        fail("waitpid process 1");

    std::printf("Parent: P1 has terminated\n");
    std::printf("Parent: shared value=%d, in_progress=%d\n",
        shared->value, shared->transaction_in_progress);

    // 4. PROCESS 2: attempt to acquire the abandoned mutex.
    pid_t p2 = fork();

    if (p2 == -1)
        fail("fork process 2");

    if (p2 == 0) {
        close(result_pipe[0]);

        std::printf("P2: attempting pthread_mutex_lock...\n");
        std::fflush(stdout);

        int lock_rc = pthread_mutex_lock(&shared->mutex);

        if (lock_rc == 0) {
            std::printf("P2: acquired mutex normally\n");
        } else if (lock_rc == EOWNERDEAD) {
            std::printf("P2: received EOWNERDEAD\n");
            std::printf("P2: mutex acquired; protected data needs repair\n");

            // Simulated rollback of the interrupted transaction.
            if (shared->transaction_in_progress) {
                shared->value = shared->previous_value;
                shared->transaction_in_progress = 0;
            }

            // Declare the protected data consistent after repair.
            int consistent_rc =
                pthread_mutex_consistent(&shared->mutex);

            if (consistent_rc != 0) {
                std::fprintf(stderr,
                    "P2: pthread_mutex_consistent failed: %s\n",
                    std::strerror(consistent_rc));
                _exit(20);
            }

            std::printf("P2: repaired shared state\n");
        } else {
            std::fprintf(stderr, "P2: lock failed: %s\n",
                std::strerror(lock_rc));
            _exit(21);
        }

        std::printf("P2: value=%d, in_progress=%d\n",
            shared->value, shared->transaction_in_progress);
        std::fflush(stdout);

        int unlock_rc = pthread_mutex_unlock(&shared->mutex);

        if (unlock_rc != 0)
            _exit(22);

        char result = 'D';
        if (write(result_pipe[1], &result, 1) != 1)
            _exit(23);

        close(result_pipe[1]);
        _exit(0);
    }

    close(result_pipe[1]);

    // 5. Bound the wait: a normal mutex may block forever.
    struct pollfd pfd {};
    pfd.fd = result_pipe[0];
    pfd.events = POLLIN;

    int poll_rc;
    do {
        poll_rc = poll(&pfd, 1, 2500);
    } while (poll_rc == -1 && errno == EINTR);

    if (poll_rc == -1)
        fail("poll");

    if (poll_rc == 0) {
        std::printf(
            "Parent: timeout; P2 did not finish acquiring the mutex\n");
        std::printf(
            "Parent: terminating blocked P2 to end the experiment\n");
        kill(p2, SIGKILL);
    } else {
        char result;
        ssize_t result_n = read(result_pipe[0], &result, 1);

        if (result_n != 1)
            std::printf("Parent: P2 exited without reporting success\n");
    }

    close(result_pipe[0]);

    int p2_status;
    if (waitpid(p2, &p2_status, 0) == -1)
        fail("waitpid process 2");

    std::printf("Parent: final shared value=%d\n", shared->value);
    std::printf("Parent: final in_progress=%d\n",
        shared->transaction_in_progress);

    pthread_mutex_destroy(&shared->mutex);
    munmap(shared, sizeof(SharedData));

    return 0;
}


