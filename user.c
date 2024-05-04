#include <stdio.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <errno.h>

#define SHMKEY1 2031535
#define SHMKEY2 2031536
#define BUFF_SZ sizeof(int)
#define PERMS 0644
#define TERM_CHANCE 5

// Struct for message queue
typedef struct {
    long mtype;
    int memoryRequest;
    pid_t pid;
} MsgBuffer;

// Helper function to initialize random seed
static int initialize_random_seed() {
    unsigned int seed;
    FILE *urandom = fopen("/dev/urandom", "r");
    if (!urandom) {
        perror("Failed to open /dev/urandom");
        return -1;
    }
    if (fread(&seed, sizeof(seed), 1, urandom) != 1) {
        perror("Failed to read /dev/urandom");
        fclose(urandom);
        return -1;
    }
    fclose(urandom);
    srand(seed);
    return 0;
}

// Function to initialize message queue
static int init_message_queue() {
    key_t key;
    int msqid;
    
    // Create key for message queue
    if ((key = ftok("oss.c", 1)) == -1) {
        perror("ftok");
        return -1;
    }

    // Get message queue ID
    if ((msqid = msgget(key, PERMS)) == -1) {
        perror("msgget");
        return -1;
    }
    return msqid;
}

int main() {
    MsgBuffer buff;
    buff.mtype = 1;
    buff.pid = 0;
    int msqid;

    // Initialize random seed
    if (initialize_random_seed() != 0) {
        fprintf(stderr, "Warning: No sources for randomness.\n");
    }

    // Initialize message queue
    msqid = init_message_queue();
    if (msqid == -1) {
        exit(EXIT_FAILURE);
    }

    // Get shared memory for seconds
    int shm_id = shmget(SHMKEY1, BUFF_SZ, IPC_CREAT | 0666);
    if (shm_id <= 0) {
        perror("Shared memory get failed for seconds");
        exit(EXIT_FAILURE);
    }
    int *sharedSeconds = shmat(shm_id, 0, 0);

    // Get shared memory for nanoseconds
    shm_id = shmget(SHMKEY2, BUFF_SZ, IPC_CREAT | 0666);
    if (shm_id <= 0) {
        perror("Shared memory get failed for nanoseconds");
        exit(EXIT_FAILURE);
    }
    int *sharedNano = shmat(shm_id, 0, 0);

    int exitFlag = 0;
    int requestCount = 0;

    // Main loop
    while (!exitFlag) {
        // Generate random memory request
        int pageNumber = rand() % 64;
        int offset = rand() % 1024;
        int memoryLocation = (pageNumber * 1024) + offset;
        int readWrite = rand() % 100;

        // Randomly choose read or write operation
        if (readWrite < 10) {
            memoryLocation *= -1;
        }

        // Prepare message buffer
        buff.memoryRequest = memoryLocation;
        buff.pid = getpid();

        // Send message
        if (msgsnd(msqid, &buff, sizeof(buff) - sizeof(long), 0) == -1) {
            perror("Message Sent failure in child");
            exit(EXIT_FAILURE);
        }

        // Receive response
        if (msgrcv(msqid, &buff, sizeof(buff) - sizeof(long), getpid(), 0) == -1) {
            if (errno != EINTR) { // Check if error is due to signal interruption
                perror("Message Received failure in child");
                exit(EXIT_FAILURE);
            }
        }

        requestCount++;
        if (requestCount % 1000 == 0) {
            int terminate = rand() % 100;
            if (terminate < TERM_CHANCE) {
                exitFlag = 1;
            }
        }
    }

    // Detach shared memory
    shmdt(sharedSeconds);
    shmdt(sharedNano);

    return EXIT_SUCCESS;
}
