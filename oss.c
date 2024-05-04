#include<time.h>
#include<stdio.h>
#include<sys/types.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<math.h>
#include<signal.h>
#include<sys/time.h>
#include<getopt.h>
#include<string.h>
#include<sys/msg.h>
#include<stdarg.h>
#include<errno.h>
#include <stdbool.h>


#define SHMKEY1 2011434
#define SHMKEY2 2011446
#define BUFF_SZ sizeof (int)
#define MAXDIGITS 3
#define PERMS 0644
#define BOUND 20



struct page {
    int frameNumber;  // Frame number associated with this page
    bool occupied;  // Indicates if the page is occupied
    long accessTime;  // Optional: last access time for page replacement algorithms
};



struct frame {
    bool occupied : 1;      // Whether the frame is occupied
    int pageNumber;           // Page number stored in this frame
    pid_t pid;                // Process ID owning this frame
    bool secondChance : 1;    // Second chance indicator for page replacement
    bool dirtyBit : 1;        // Indicates if the frame is dirty
    int nextFrame;            // Index of the next frame
    int lastAccessTime;       // Last access time of the frame
};



struct PCB {
    bool occupied;            // Whether the PCB is occupied
    pid_t pid;                  // Process ID
    int startSeconds;       // Time when the process started, in seconds
    int startNano;          // Time when the process started, in nanoseconds
    int blockSeconds;       // Time when the process was blocked, in seconds
    int blockNano;          // Time when the process was blocked, in nanoseconds
    int memoryAccessTime;       // Time spent accessing memory
    struct page pageTable[64];  // Page table for the process
};



typedef struct {
    long mtype;          // Message type
    int memoryRequest;   // Memory request type
    pid_t pid;           // Process ID associated with the message
} msgbuffer;


typedef struct{
    int proc;
    int simul;
    int interval;
    char logfile[20];
} options_t;

//Shared Memory pointers
int *sharedSeconds;
int *sharedNano;
int shmidSeconds;
int shmidNano;

//Process Table
struct PCB processTable[20];

//Message ID
int msqid;

struct QNode{
    int key;
    int request;
    struct QNode* next;
};

struct Queue{
    struct QNode *front, *rear;
};

//Function prototypes
void fprintFrameTable(int SysClockS, int SysClockNano, struct frame frameTable[256], int nextFrame, FILE *fptr);
void printFrameTable(int SysClockS, int SysClockNano, struct frame frameTable[256], int nextFrame);
int clearFrame(struct frame frameTable[256], pid_t pid);
int terminateCheck();
int req_lt_avail(const int *req, const int *avail, const int pnum, const int num_res);
int addProcessTable(struct PCB processTable[20], pid_t pid, FILE *fptr);
struct QNode* newNode(int k, int r);
struct Queue* createQueue();
bool enQueue(struct Queue* q, int k, int r);
bool deQueue(struct Queue* q);
int lfprintf(FILE *stream,const char *format, ... );
static int setupinterrupt(void);
static int setupitimer(void);
void print_usage(const char * app);
void printProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20]);
void fprintProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20], FILE *fptr);
void incrementClock(int *seconds, int *nano, int increment);
static int randomize_helper(FILE *in);
static int randomize(void);
void clearResources();
int clearProcessTable(struct PCB processTable[20], pid_t pid);
int fillPageTable(pid_t, int, int);
void fillFrameTable(pid_t, int, int, struct frame frameTable[256], int SysClockS, int SysClockNano);
int secondChance(struct frame FrameTable[256]);

int main(int argc, char* argv[]){

    
    //Seed random
    if(randomize()){
        fprintf(stderr, "Warning: No source for randomness.\n");
    }

    //Set up shared memory
    shmidSeconds = shmget(SHMKEY1, BUFF_SZ, 0666 | IPC_CREAT);
    if(shmidSeconds == -1){
        fprintf(stderr, "error in shmget 1.0\n");
        exit(1);
    }
    sharedSeconds = shmat(shmidSeconds, 0, 0);
    
    //Attach shared memory to nano
    shmidNano = shmget(SHMKEY2, BUFF_SZ, 0777 | IPC_CREAT);
    if(shmidNano == -1){
        fprintf(stderr, "error in shmget 2.0\n");
        exit(1);
    }
    sharedNano=shmat(shmidNano, 0, 0);

    //Set up structs defaults
    for(int i = 0; i < 20; i++){
        processTable[i].occupied = 0;
        processTable[i].pid = 0;
        processTable[i].startSeconds = 0;
        processTable[i].startNano = 0;
        processTable[i].blockSeconds = 0;
        processTable[i].blockNano = 0;
        processTable[i].memoryAccessTime = 0;
        for(int i2 = 0; i2 < 64; i2++){
            processTable[i].pageTable[i2].occupied = 0;
            processTable[i].pageTable[i2].frameNumber = 0;
        }
    }

    struct frame frameTable[256];
    for(int i = 0; i < 256; i++){
        frameTable[i].pageNumber = 0;
        frameTable[i].pid = 0;
        frameTable[i].occupied = 0;
        frameTable[i].dirtyBit = 0;
        frameTable[i].secondChance = 0;
        frameTable[i].nextFrame = 0;
    }

    //Set up user parameters defaults
    options_t options;
    options.proc = 80; //n
    options.simul = 18; //s
    options.interval = 0; //i
    strcpy(options.logfile, "log.txt"); //f

    //Set up user input
    const char optstr[] = "hn:s:i:f:";
    char opt;
    while((opt = getopt(argc, argv, optstr))!= -1){
        switch(opt){
            case 'h':
                print_usage(argv[0]);
                return(EXIT_SUCCESS);
            case 'n':
                options.proc = atoi(optarg);
                break;
            case 's':
                options.simul = atoi(optarg);
                break;
            case 'i':
                options.interval = atoi(optarg);
                break;
            case 'f':
                strcpy(options.logfile, optarg);
                break;
            default:
                printf("Invalid options %c\n", optopt);
                print_usage(argv[0]);
                return(EXIT_FAILURE);
        }
    }
   
    //Set up variables;
    int seconds = 0;
    int nano = 0;
    *sharedSeconds = seconds;
    *sharedNano = nano;

    //Variables for message queue
    key_t key;
    msgbuffer buff;
    buff.mtype = 0;
    buff.pid = 0;
    buff.memoryRequest = 0;

    // Configure timers
    if (setupinterrupt() == -1) {
        perror("Unable to establish signal handler for SIGPROF");
        return 1;
    }
    if (setupitimer() == -1) {
        perror("Unable to configure ITIMER_PROF interval timer");
        return 1;
    }


   
    char commandString[20];
    strcpy(commandString, "touch ");
    strcat(commandString, options.logfile);
    system(commandString);
    FILE *fptr;
    fptr = fopen(options.logfile, "w");
    if(fptr == NULL){
        fprintf(stderr, "Error: file has not opened.\n");
        exit(0);
    }

    //get a key for message queue
    if((key = ftok("oss.c", 1)) == -1){
        perror("ftok");
        exit(1);
    }

    //create our message queue
    if((msqid = msgget(key, PERMS | IPC_CREAT)) == -1){
        perror("msgget in parent");
        exit(1);
    }
    
    //Variables
    int simulCount = 0;
    int childrenFinishedCount = 0;
    int nextIntervalSecond = 0;
    int nextIntervalNano = 0;
    int childLaunchedCount = 0;
    pid_t terminatedChild = 0;
    int status = 0;
    struct frame *nextFrame;
    nextFrame = &frameTable[0];
    int frameInterval = 0;
    int secondChanceIndex = 0;

    struct Queue *blockQueue = createQueue();
    //Stats
    int memoryAccessCount = 0;
    int pageFaultCount = 0;
   
    while(childrenFinishedCount < options.proc){
        
        int terminatedChild = 0;
        if(simulCount > 0 && (terminatedChild = terminateCheck()) < 0){
            perror("Wait for PID failed\n");
        }
        else if(terminatedChild > 0){
            
            int tempIndex = 0;
            for(int i = 0; i < 20; i++){
                if(processTable[i].pid == terminatedChild){
                    tempIndex = i;
                }
            }

            fprintf(fptr, "OSS: Process P%d ended, with a memory access duration of %d at %d:%d\n",
                    terminatedChild, processTable[tempIndex].memoryAccessTime,*sharedSeconds, *sharedNano);
            printf("OSS: Process %d has completed with a memory access duration of %d at time %d:%d\n",
                   terminatedChild, processTable[tempIndex].memoryAccessTime,*sharedSeconds, *sharedNano);
            simulCount--;
            childrenFinishedCount++;
            clearProcessTable(processTable, terminatedChild);
            clearFrame(frameTable, terminatedChild);
        }

        pid_t child = 0;
    
        
        if(childLaunchedCount < options.proc &&
            simulCount < options.simul &&
            simulCount < 18 &&
            (*sharedSeconds > nextIntervalSecond || *sharedSeconds == nextIntervalNano &&
            *sharedNano > nextIntervalSecond) &&
            (child = fork()) == 0){
                        

            char * args[] = {"./user_proc"};
            
            execlp(args[0], args[0], NULL);
            printf("Exec failed\n");
            exit(1);
        }
        if(child > 0){
            if(addProcessTable(processTable, child, fptr) == -1){
                perror("Add process table failed");
                exit(1);
            }
            printf("OSS: Process %d started at %d seconds and %d nanoseconds\n", child, *sharedSeconds, *sharedNano);
            fprintf(fptr, "Process %d began at %d seconds and %d nanoseconds\n", child, *sharedSeconds, *sharedNano);
            simulCount++;
            childLaunchedCount++;
            nextIntervalSecond += 1;
        }

        while(blockQueue->front != NULL &&
            (((*sharedSeconds) > processTable[blockQueue->front->key].blockSeconds) ||
            ((*sharedSeconds) == processTable[blockQueue->front->key].blockSeconds &&
            (*sharedNano) > processTable[blockQueue->front->key].blockNano))
        ){
            int queuedRequest = blockQueue->front->request;
            int queuedProcessIndex = blockQueue->front->key;
            pid_t queuedProcess = processTable[queuedProcessIndex].pid;

            if(processTable[queuedProcessIndex].occupied == 0){
                deQueue(blockQueue);
            }
            else{
                int next = -1;
                while(next < 0){
                    if(nextFrame->secondChance == 1){
                        nextFrame->secondChance = 0;
                        secondChanceIndex++;
                        if(secondChanceIndex > 255){
                            secondChanceIndex = 0;
                        }
                        nextFrame = &frameTable[secondChanceIndex];
                    }
                    else{
                        processTable[queuedProcessIndex].memoryAccessTime += 14 * pow(10, 6);
                        if(queuedRequest < 0){
                            processTable[queuedProcessIndex].memoryAccessTime += pow(10, 6); \
                        }
                        if(nextFrame->occupied == 1){
                            printf("OSS: Removing frame %d and inserting page %d of process P%d\n", secondChanceIndex, queuedProcess, abs(queuedRequest/1024));
                        }
                        nextFrame->secondChance = 1;
                        secondChanceIndex++;
                        if(secondChanceIndex > 255){
                            secondChanceIndex = 0;
                        }
                        fillPageTable(queuedProcess, queuedRequest, secondChanceIndex-1);
                        fillFrameTable(queuedProcess, queuedRequest, secondChanceIndex-1, frameTable, *sharedNano, *sharedSeconds);
                        memoryAccessCount++;
                        next = secondChanceIndex;
                        nextFrame = &frameTable[secondChanceIndex];
                    }
                }

               
                if(queuedRequest > 0){
                    printf("OSS: Address %d is in page %d within frame %d. Providing data to process P%d at %d:%d\n",
                        queuedRequest, abs(queuedRequest)/1024, secondChanceIndex-1, queuedProcess, *sharedSeconds, *sharedNano);
                }
                else{
                    printf("OSS: Writing data to address %d, located in page %d within frame %d, at %d:%d\n",
                        queuedRequest, abs(queuedRequest)/1024, secondChanceIndex-1, *sharedSeconds, *sharedNano);
                    printf("OSS: The dirty bit of frame %d is set, adding extra time to the clock\n", secondChanceIndex);
                    incrementClock(sharedSeconds, sharedNano, pow(10,6));

                }
                buff.pid = getpid();
                buff.mtype = queuedProcess;
                buff.memoryRequest = 0;
                if(msgsnd(msqid, &buff, sizeof(buff)-sizeof(long), 0) == -1){
                    perror("queued message send failed\n");
                    exit(1);
                }
                if(queuedRequest < 0){
                    printf("OSS: Informed process P%d that a write occurred at address %d\n", queuedProcess, abs(queuedRequest));
                }
             
                deQueue(blockQueue);
                processTable[queuedProcessIndex].blockSeconds = 0;
                processTable[queuedProcessIndex].blockNano = 0;
            }
        }

        
        if(simulCount > 0){
            if(msgrcv(msqid, &buff, sizeof(buff) - sizeof(long), getpid(), IPC_NOWAIT)==-1){
                if(errno == ENOMSG){
                    incrementClock(sharedSeconds, sharedNano, 1000);
                }
                else{
                    printf("MSQID: %li\n", buff.mtype);
                    printf("Parent: %d Child: %d\n", getpid(), buff.pid);
                    perror("Msgrcv in parent error\n");
                    exit(1);
                }
            }
            else{
                int address = abs(buff.memoryRequest);
                
                printf("OSS: P%d requesting ", buff.pid);
                if(buff.memoryRequest > 0){
                    printf("read");
                }
                else{
                    printf("write");
                }
                printf(" of address %d at time %d:%d\n", abs(buff.memoryRequest), *sharedSeconds, *sharedNano);
                

            
                int processNumber = -1;
                for(int i = 0; i < 20; i++){
                    if(processTable[i].pid == buff.pid){
                        processNumber = i;
                    }
                }

            
                if(processTable[processNumber].pageTable[address/1024].occupied == 1){
                    int frameReference = abs(processTable[processNumber].pageTable[address/1024].frameNumber);
                    frameTable[frameReference].secondChance = 1;
                    if(buff.memoryRequest < 0){
                        frameTable[frameReference].dirtyBit = 1;
                    }
                    incrementClock(sharedSeconds, sharedNano, 100);
                    memoryAccessCount++;
                    processTable[processNumber].memoryAccessTime += 100;
                    printf("OSS: Providing data to process P%d from address %d in frame %d at %d:%d\n",
                        abs(buff.memoryRequest), frameReference, buff.pid, *sharedSeconds, *sharedNano);

                    buff.mtype = buff.pid;
                    buff.pid = getpid();
                    buff.memoryRequest = 0;
                    if(msgsnd(msqid, &buff, sizeof(buff) - sizeof(long), 0) == -1){
                        perror("Msgsnd failed\n");
                        exit(1);
                    }
                }
                else{
                    printf("OSS: Address %d is not in a frame, resulting in a page fault\n", abs(buff.memoryRequest));
                    pageFaultCount++;
                    enQueue(blockQueue, processNumber, buff.memoryRequest);
                    processTable[processNumber].blockSeconds = *sharedSeconds;
                    processTable[processNumber].blockNano = (*sharedNano) + 14 * pow(10, 6);
                    if(processTable[processNumber].blockNano > pow(10, 9)){
                        processTable[processNumber].blockNano -= pow(10, 9);
                        processTable[processNumber].blockSeconds++;
                    }
                }
            }
        }
        if((*sharedSeconds) > frameInterval){
            frameInterval++;
            printProcessTable(getpid(), *sharedSeconds, *sharedNano, processTable);
            fprintProcessTable(getpid(), *sharedSeconds, *sharedNano, processTable, fptr);
            printFrameTable(*sharedSeconds, *sharedNano, frameTable, secondChanceIndex);
            fprintFrameTable(*sharedSeconds, *sharedNano, frameTable, secondChanceIndex, fptr);
        }
        incrementClock(sharedSeconds, sharedNano, 5000);
    }

    
    float memAccessPerSecond = (float) memoryAccessCount / *sharedSeconds;
    float pageFaultPerMem = (float) pageFaultCount / (memoryAccessCount);
    
    if(msgctl(msqid, IPC_RMID, NULL) == -1){
        perror("msgctl to get rid of queue in parent failed");
        exit(1);
    }
    
   
    shmdt(sharedSeconds);
    shmdt(sharedNano);


    fclose(fptr);
    return 0;

}




int clearFrame(struct frame frameTable[256], pid_t pid) {
    // Error handling for NULL frameTable pointer
    if (frameTable == NULL) {
        // Print an error message or handle the error in an appropriate way
        return -1; // Return -1 to indicate failure
    }

    int framesCleared = 0; // Variable to count the number of frames cleared

    // Iterate through the frame table
    for (int i = 0; i < 256; i++) {
        // If the frame is associated with the given process ID, clear it
        if (frameTable[i].pid == pid) {
            frameTable[i].pid = 0;
            frameTable[i].secondChance = 0;
            frameTable[i].nextFrame = 0;
            frameTable[i].dirtyBit = 0;
            frameTable[i].occupied = 0;
            frameTable[i].pageNumber = 0;
            framesCleared++; // Increment the count of cleared frames
        }
    }

    return framesCleared; // Return the number of frames cleared
}



int secondChance(struct frame frameTable[256]) {
    // Error handling for NULL frameTable pointer
    if (frameTable == NULL) {
        // Print an error message or handle the error in an appropriate way
        return -1; // Return -1 to indicate failure
    }

    int nextFrame = -1; // Variable to store the index of the selected frame

    int i = 0;
    while (nextFrame < 0) {
        if (frameTable[i].secondChance == 1) {
            frameTable[i].secondChance = 0;
        } else {
            frameTable[i].nextFrame = 1;
            nextFrame = i; // Set the selected frame index
            break;
        }
        i = (i + 1) % 256;
    }

    return nextFrame; // Return the index of the selected frame
}






int fillPageTable(pid_t pid, int request, int frame) {
    // Check if the process table pointer is NULL
    if (processTable == NULL) {
        // Print an error message or handle the error in an appropriate way
        return -1; // Return -1 to indicate failure
    }

    int pageNumber = abs(request) / 1024;

    // Iterate through the process table to find the process with the given PID
    for (int i = 0; i < 20; i++) {
        if (processTable[i].pid == pid) {
            // Fill the page table entry for the process
            processTable[i].pageTable[pageNumber].frameNumber = frame;
            processTable[i].pageTable[pageNumber].occupied = 1;
            return 0; // Return 0 to indicate success
        }
    }

    // If the process with the given PID is not found, return -1 to indicate failure
    return -1;
}




void fillFrameTable(pid_t pid, int request, int frame, struct frame frameTable[256], int SysClockS, int SysClockNano) {
    // Check if the frame table pointer is NULL
    if (frameTable == NULL) {
        // Print an error message or handle the error in an appropriate way
        return; // Return without performing any operations
    }

    int pageNumber = abs(request) / 1024;

    // Fill the frame table entry for the given frame number
    frameTable[frame].pid = pid;
    frameTable[frame].pageNumber = pageNumber;
    frameTable[frame].occupied = 1;
    frameTable[frame].secondChance = 1;

    // Set the dirty bit based on the request value
    frameTable[frame].dirtyBit = (request < 0) ? 1 : 0;
}



int terminateCheck() {
    int status = 0;
    // Check if any child process has terminated without blocking
    pid_t terminatedChild = waitpid(0, &status, WNOHANG);
    if (terminatedChild > 0) {
        // If a child process has terminated, return its PID
        return terminatedChild;
    } else if (terminatedChild == 0) {
        // If no child process has terminated, return 0
        return 0;
    } else {
        // If an error occurs, return -1
        return -1;
    }
}


int lfprintf(FILE *stream, const char *format, ...) {
    static int lineCount = 0; // Static line counter

    lineCount++; // Increment the line counter

    // Check if the line count exceeds the limit
    if (lineCount > 10000) {
        return 1; // Return 1 to indicate that the line count limit is exceeded
    }

    va_list args; // Variable argument list
    va_start(args, format); // Start the variable argument list

    // Print the formatted output to the provided stream
    vfprintf(stream, format, args);

    va_end(args); // End the variable argument list

    return 0; // Return 0 to indicate successful printing within the line count limit
}


static void myhandler(int s) {
    printf("Signal gotten, terminated\n"); // Print a message indicating that a signal was received

    // Terminate child processes
    for (int i = 0; i < 20; i++) {
        if (processTable[i].occupied == 1) {
            kill(processTable[i].pid, SIGTERM);
        }
    }

    // Remove message queue
    if (msgctl(msqid, IPC_RMID, NULL) == -1) {
        perror("msgctl to getting rid of queue fails");
        exit(1);
    }

    // Detach and remove shared memory segments
    shmdt(sharedSeconds);
    shmdt(sharedNano);
    shmctl(shmidSeconds, IPC_RMID, NULL);
    shmctl(shmidNano, IPC_RMID, NULL);

    // Exit the program
    exit(1);
}


static int setupinterrupt(void) {
    struct sigaction act; // Signal action structure

    // Set up the signal handler for SIGINT
    act.sa_handler = myhandler; // Set the signal handler function
    act.sa_flags = 0; // No special flags are set

    // Initialize the signal mask for the signal handler
    if (sigemptyset(&act.sa_mask) != 0) {
        return 1; // Return non-zero to indicate an error
    }

    // Register the signal handler for SIGINT
    if (sigaction(SIGINT, &act, NULL) != 0) {
        return 1; // Return non-zero to indicate an error
    }

    // Register the signal handler for SIGPROF
    if (sigaction(SIGPROF, &act, NULL) != 0) {
        return 1; // Return non-zero to indicate an error
    }

    return 0; // Return 0 to indicate success
}

static int setupitimer(void) {
    struct itimerval value; // Structure for timer value

    // Set the interval for the timer to 5 seconds
    value.it_interval.tv_sec = 5;
    value.it_interval.tv_usec = 0;

    // Set the initial expiration time for the timer
    value.it_value = value.it_interval;

    // Set up the interval timer using ITIMER_PROF option
    return setitimer(ITIMER_PROF, &value, NULL);
}

void print_usage(const char * app){
    fprintf(stderr, "usage: %s [-h] [-n proc] [-s simul] [-t timeLimitForChildren] [-i intervalInMsToLaunchChildren] [-f logfile]\n", app);
    fprintf(stderr, "   The '-n' option specifies the total number of child processes to be created.\n");
    fprintf(stderr, "   The '-s' option defines the maximum number of child processes allowed to run simultaneously.\n");
    fprintf(stderr, "   The '-t' option sets the maximum duration (in seconds) that a child process can run.\n");
    fprintf(stderr, "   The '-i' option sets the interval (in milliseconds) at which new child processes should be launched.\n");
    fprintf(stderr, "   The '-f' option specifies the name of the logfile where OSS writes its output.\n");
}


void printFrameTable(int SysClockS, int SysClockNano, struct frame frameTable[256], int nextFrame) {
    printf("Memory layout at time %d:%d:\n", SysClockS, SysClockNano);
    printf("         Occupied   Dirty Bit   Second Chance   Next Frame\n");
    for(int i = 0; i < 256; i++) {
        printf("Frame %d", i);
        printf(" ");
        if(frameTable[i].occupied == 1) {
            printf("Yes         ");
        } else {
            printf("No          ");
        }
        printf("%d            ", frameTable[i].dirtyBit);
        printf("%d              ", frameTable[i].secondChance);
        if(i == nextFrame) {
            printf("*\n");
        } else {
            printf("\n");
        }
    }
}



void fprintFrameTable(int SysClockS, int SysClockNano, struct frame frameTable[256], int nextFrame, FILE *fptr) {
    lfprintf(fptr, "Memory layout at time %d:%d:\n", SysClockS, SysClockNano);
    lfprintf(fptr, "         Occupied   Dirty Bit   Second Chance   Next Frame\n");
    for(int i = 0; i < 256; i++) {
        lfprintf(fptr, "Frame %d", i);
        lfprintf(fptr, " ");
        if(frameTable[i].occupied == 1) {
            lfprintf(fptr, "Yes         ");
        } else {
            lfprintf(fptr, "No          ");
        }
        lfprintf(fptr, "%d            ", frameTable[i].dirtyBit);
        lfprintf(fptr, "%d              ", frameTable[i].secondChance);
        if(i == nextFrame) {
            lfprintf(fptr, "*\n");
        } else {
            lfprintf(fptr, "\n");
        }
    }
}



void printProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20]) {
    printf("OSS PID %d   SysClockS: %d   SysClockNano: %d\n", PID, SysClockS, SysClockNano);
    printf("Process Table:\n");
    printf("Entry     Occupied  PID       StartS    Startn     Memory Access Time\n");
    for(int i = 0; i < 20; i++) {
        if(processTable[i].occupied == 1) {
            printf("%-10d%-10d%-10d%-10d%-11d%-13d\n", i, processTable[i].occupied,
                   processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano, processTable[i].memoryAccessTime);
        }
    }
}

void fprintProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20], FILE *fptr) {
    lfprintf(fptr, "OSS PID %d   SysClockS: %d   SysClockNano: %d\n", PID, SysClockS, SysClockNano);
    lfprintf(fptr, "Process Table:\n");
    lfprintf(fptr, "Entry     Occupied  PID       StartS    Startn    Memory Access Time\n");
    for(int i = 0; i < 20; i++) {
        if(processTable[i].occupied == 1) {
            lfprintf(fptr, "%-10d%-10d%-10d%-10d%-11d%-13d\n", i, processTable[i].occupied,
                   processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano, processTable[i].memoryAccessTime);
        }
    }
}


void printResourceTable(int allocatedTable[20][10]) {
    printf("Resource Allocation Table:\n");
    printf("Process ID   Resource ID\n");
    for(int i = 0; i < 20; i++) {
        printf("    P%d          ", i);
        for(int j = 0; j < 10; j++) {
            printf("%d ", allocatedTable[i][j]);
        }
        printf("\n");
    }
}


void incrementClock(int *seconds, int *nano, int increment) {
    // Convert nanoseconds increment to seconds
    *seconds += increment / 1000000000;
    // Calculate remaining nanoseconds after converting to seconds
    *nano = (*nano + increment) % 1000000000;
}



static int randomize_helper(FILE *in) {
    unsigned int seed;
    
    // Check if the input file pointer is valid
    if (!in) {
        return -1; // Return -1 to indicate failure
    }
    
    // Read the seed value from the input file
    if (fread(&seed, sizeof(seed), 1, in) == 1) {
        fclose(in); // Close the input file
        srand(seed); // Seed the random number generator
        return 0; // Return 0 to indicate success
    }
    
    fclose(in); // Close the input file if reading fails
    return -1; // Return -1 to indicate failure
}




static int randomize(void) {
    // Attempt to seed the random number generator using various sources of entropy
    if (!randomize_helper(fopen("/dev/urandom", "r"))) {
        return 0; // Return success if seeding is successful
    }
    if (!randomize_helper(fopen("/dev/arandom", "r"))) {
        return 0; // Return success if seeding is successful
    }
    if (!randomize_helper(fopen("/dev/random", "r"))) {
        return 0; // Return success if seeding is successful
    }
    return -1; // Return failure if all attempts to seed fail
}




int clearProcessTable(struct PCB processTable[20], pid_t pid) {
    // Iterate through the process table to find the entry with the given PID
    for (int i = 0; i < 20; i++) {
        if (processTable[i].pid == pid) {
            // Clear the process table entry
            processTable[i].occupied = 0;
            processTable[i].pid = 0;
            processTable[i].startSeconds = 0;
            processTable[i].startNano = 0;
            processTable[i].blockSeconds = 0;
            processTable[i].blockNano = 0;
            processTable[i].memoryAccessTime = 0;

            // Clear the page table entries
            for (int i2 = 0; i2 < 64; i2++) {
                processTable[i].pageTable[i2].occupied = 0;
                processTable[i].pageTable[i2].frameNumber = 0;
            }

            return 0; // Return success
        }
    }
    
    return -1; // Return failure if the given PID is not found in the process table
}





int addProcessTable(struct PCB processTable[20], pid_t pid, FILE *fptr) {
    // Check if the process table is full
    int emptyIndex = -1;
    for (int i = 0; i < 20; i++) {
        if (processTable[i].occupied == 0) {
            emptyIndex = i;
            break;
        }
    }
    if (emptyIndex == -1) {
        // Print an error message or handle the error in an appropriate way
        return -1; // Return -1 to indicate failure (process table is full)
    }

    // Add the process to the process table at the first empty slot
    processTable[emptyIndex].occupied = 1;
    processTable[emptyIndex].pid = pid;
    processTable[emptyIndex].startNano = *sharedNano;
    processTable[emptyIndex].startSeconds = *sharedSeconds;
    processTable[emptyIndex].memoryAccessTime = 0;

    // DELETE: For testing, you may have this line to ensure the function runs without errors
    return 0; // Return 0 to indicate success
}


struct QNode* newNode(int k, int r) {
    // Allocate memory for the new node
    struct QNode* temp = (struct QNode*)malloc(sizeof(struct QNode));
    if (temp == NULL) {
        // Print an error message or handle the error in an appropriate way
        return NULL; // Return NULL to indicate failure
    }

    // Initialize the new node
    temp->key = k;
    temp->request = r;
    temp->next = NULL;
    return temp;
}



struct Queue* createQueue() {
    // Allocate memory for the new queue
    struct Queue* q = (struct Queue*)malloc(sizeof(struct Queue));
    if (q == NULL) {
        // Print an error message or handle the error in an appropriate way
        return NULL; // Return NULL to indicate failure
    }

    // Initialize front and rear pointers
    q->front = q->rear = NULL;
    return q;
}




bool enQueue(struct Queue* q, int k, int r) {
    // Error handling for NULL queue pointer
    if (q == NULL) {
        // Print an error message or handle the error in an appropriate way
        return false;
    }

    // Create a new node with key k and resource r
    struct QNode* temp = newNode(k, r);
    if (temp == NULL) {
        // Print an error message or handle the error in an appropriate way
        return false;
    }

    // If the queue is empty, set both front and rear to the new node
    if (q->rear == NULL) {
        q->front = q->rear = temp;
    } else {
        // Otherwise, add the new node to the rear of the queue and update the rear pointer
        q->rear->next = temp;
        q->rear = temp;
    }

    return true; // Enqueue operation successful
}



bool deQueue(struct Queue* q) {
    // Error handling for NULL queue pointer
    if (q == NULL) {
        // Print an error message or handle the error in an appropriate way
        return false;
    }

    // Check if the queue is empty
    if (q->front == NULL) {
        // Print an error message or handle the error in an appropriate way
        return false;
    }

    // Remove the front node
    struct QNode* temp = q->front;
    q->front = q->front->next;

    // If the queue becomes empty, update the rear pointer
    if (q->front == NULL) {
        q->rear = NULL;
    }

    // Free the memory of the dequeued node
    free(temp);

    return true; // Dequeue operation successful
}
