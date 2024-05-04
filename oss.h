#ifndef OSS_H
#define OSS_H




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


#endif
