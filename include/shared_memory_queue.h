#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <semaphore.h>
#include <iostream>

#define MUTEX_SEM_NAME "/mutex_sem"
#define ITEMS_SEM_NAME "/items_sem"

struct SharedMemoryQueue {
    int buffer[1024]; // Ring buffer for connections (or request data)
    size_t head;
    size_t tail;
    sem_t* mutex;  // Semaphore for synchronizing access
    sem_t* items;  // Semaphore to count available items
};

SharedMemoryQueue* create_shared_memory(const char* name) {
    // Unlink the shared memory to remove any existing object with the same name
    shm_unlink(name);

    int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return nullptr;
    }
// std::cout << "Shared memory file descriptor: " << shm_fd << std::endl;
// std::cout << "Size of SharedMemoryQueue: " << sizeof(SharedMemoryQueue) << std::endl;
// long page_size = sysconf(_SC_PAGESIZE);
// std::cout << "Page size: " << page_size << std::endl;
// size_t queue_size = sizeof(SharedMemoryQueue);
// size_t aligned_size = ((queue_size + page_size - 1) / page_size) * page_size;  // Align to page size
// std::cout<< "Aligned size:"<< aligned_size<<std::endl;
    // Set the size of the shared memory
    if (ftruncate(shm_fd, sizeof(SharedMemoryQueue)) == -1) {
        perror("ftruncate");
        return nullptr;
    }

    // Map the shared memory to the process's address space
    void* ptr = mmap(0, sizeof(SharedMemoryQueue), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        return nullptr;
    }

    // Initialize the shared memory structure
    SharedMemoryQueue* queue = static_cast<SharedMemoryQueue*>(ptr);

    // Destroy previously created semaphores
    // Close and unlink the named semaphores
    if (sem_close(queue->mutex) == -1) {
        perror("sem_close (mutex)");
    }
    if (sem_unlink("/mutex_sem") == -1) {  
        perror("sem_unlink (mutex)");
    }

    if (sem_close(queue->items) == -1) {
        perror("sem_close (items)");
    }
    if (sem_unlink("/items_sem") == -1) {  
        perror("sem_unlink (items)");
    }
    
    queue->head = 0;
    queue->tail = 0;
    // Initialize named semaphore for mutex
    sem_t* mutex = sem_open(MUTEX_SEM_NAME, O_CREAT, 0644, 1); // Semaphore initialized to 1
    if (mutex == SEM_FAILED) {
        std::cerr << "[Error] Failed to create mutex semaphore" << std::endl;
        return nullptr;
    }
    queue->mutex = mutex;
    // Initialize named semaphore for items
    sem_t* items = sem_open(ITEMS_SEM_NAME, O_CREAT, 0644, 0); // Semaphore initialized to 0
    if (items == SEM_FAILED) {
        std::cerr << "[Error] Failed to create items semaphore" << std::endl;
        return nullptr;
    }
    queue->items = items;

    return queue;
}

// Detach the shared memory from the current process
void detach_shared_memory(SharedMemoryQueue* queue) {
    if (munmap(queue, sizeof(SharedMemoryQueue)) == -1) {
        perror("munmap");
    }
    if (sem_close(queue->mutex) == -1) {
        perror("sem_close (mutex)");
    }
    if (sem_close(queue->items) == -1) {
        perror("sem_close (items)");
    }
}

// Destroy the shared memory and semaphores
void destroy_shared_memory(const char* name, SharedMemoryQueue* queue) {
    // Close and unlink the shared memory object
    if (shm_unlink(name) == -1) {
        perror("shm_unlink");
    }

    // Destroy the semaphores
    // Close and unlink the named semaphores
    if (sem_close(queue->mutex) == -1) {
        perror("sem_close (mutex)");
    }
    if (sem_unlink("/mutex_sem") == -1) {  
        perror("sem_unlink (mutex)");
    }

    if (sem_close(queue->items) == -1) {
        perror("sem_close (items)");
    }
    if (sem_unlink("/items_sem") == -1) {  
        perror("sem_unlink (items)");
    }

    // Detach from the shared memory (already done in detach_shared_memory)
    detach_shared_memory(queue);
}