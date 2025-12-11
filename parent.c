#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <semaphore.h>

#define SHM_SIZE 4096
#define SHM_NAME "/shared_memory"
#define SEM_PARENT "/sem_parent_ready"
#define SEM_CHILD "/sem_child_ready"

typedef struct {
    char data[SHM_SIZE];
} shared_data_t;

void write_str(int fd, const char* str) {
    write(fd, str, strlen(str));
}

int main() {
    // Удаляем старые семафоры, если существуют
    sem_unlink(SEM_PARENT);
    sem_unlink(SEM_CHILD);
    
    // Создаём named семафоры для синхронизации
    sem_t *sem_parent = sem_open(SEM_PARENT, O_CREAT | O_EXCL, 0644, 0);
    if (sem_parent == SEM_FAILED) {
        write_str(STDERR_FILENO, "Error: sem_open parent failed\n");
        return 1;
    }
    
    sem_t *sem_child = sem_open(SEM_CHILD, O_CREAT | O_EXCL, 0644, 0);
    if (sem_child == SEM_FAILED) {
        write_str(STDERR_FILENO, "Error: sem_open child failed\n");
        sem_close(sem_parent);
        sem_unlink(SEM_PARENT);
        return 1;
    }
    
    // Удаляем старый shared memory объект, если существует
    shm_unlink(SHM_NAME);
    
    int shm_fd = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (shm_fd == -1) {
        write_str(STDERR_FILENO, "Error: shm_open failed\n");
        return 1;
    }
    
    
    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        write_str(STDERR_FILENO, "Error: ftruncate failed\n");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return 1;
    }
    
    // Отображаем shared memory в адресное пространство процесса
    shared_data_t* shared_data = mmap(NULL, SHM_SIZE, 
                                     PROT_READ | PROT_WRITE, 
                                     MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        write_str(STDERR_FILENO, "Error: mmap failed\n");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return 1;
    }
    
    close(shm_fd); // Дескриптор больше не нужен после mmap
    
    // Инициализируем shared memory
    shared_data->data[0] = '\0';
    
    // Запрос имени файла у пользователя
    write_str(STDOUT_FILENO, "Enter filename: ");
    char filename[256];
    int bytes = read(STDIN_FILENO, filename, sizeof(filename) - 1);
    if (bytes <= 0) {
        write_str(STDERR_FILENO, "Error: reading filename failed\n");
        munmap(shared_data, SHM_SIZE);
        shm_unlink(SHM_NAME);
        return 1;
    }
    filename[bytes] = '\0';
    
    // Убираем символ новой строки
    for (int i = 0; filename[i] != '\0'; i++) {
        if (filename[i] == '\n') {
            filename[i] = '\0';
            break;
        }
    }
    
    // Копируем имя файла в shared memory для дочернего процесса
    strncpy(shared_data->data, filename, sizeof(shared_data->data) - 1);
    shared_data->data[sizeof(shared_data->data) - 1] = '\0';
    
    // Создаем дочерний процесс
    pid_t pid = fork();
    
    if (pid == -1) {
        write_str(STDERR_FILENO, "Error: fork failed\n");
        munmap(shared_data, SHM_SIZE);
        shm_unlink(SHM_NAME);
        return 1;
    }
    
    if (pid == 0) {
        // Дочерний процесс
        sem_close(sem_parent);
        sem_close(sem_child);
        execl("./child", "child", NULL);
        write_str(STDERR_FILENO, "Error: exec failed\n");
        exit(1);
    } else {
        // Родительский процесс
        sleep(1);
        
        write_str(STDOUT_FILENO, "Enter numbers (one line at a time):\n");
        
        char buffer[1024];
        int total_sent = 0;
        
        while (1) {
            write_str(STDOUT_FILENO, "> ");
            
            bytes = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
            if (bytes <= 0) {
                break;
            }
            
            buffer[bytes] = '\0';
            
            // Ждём, пока дочерний процесс прочитает предыдущие данные
            sem_wait(sem_child);
            
            // Копируем данные в memory-mapped file
            strncpy(shared_data->data, buffer, sizeof(shared_data->data) - 1);
            shared_data->data[sizeof(shared_data->data) - 1] = '\0';
            
            // Проверяем на конец ввода (пустая строка)
            if (bytes == 1 && buffer[0] == '\n') {
                // Для сигнала завершения можно использовать специальную метку в данных
                // или просто закрыть семафор после выхода из цикла
                sem_post(sem_parent); // Последнее уведомление
                break;
            }
            
            // Уведомляем дочерний процесс, что данные готовы
            sem_post(sem_parent);
            
            total_sent += bytes;
            
            // Защита от переполнения 
            if (total_sent > SHM_SIZE * 10) {
                write_str(STDERR_FILENO, "Warning: large amount of data sent\n");
            }
        }
        
        // Ждем завершения дочернего процесса
        wait(NULL);
        
        // Очистка ресурсов
        munmap(shared_data, SHM_SIZE);
        shm_unlink(SHM_NAME); // Удаляем shared memory объект
        
        // Закрываем и удаляем семафоры
        sem_close(sem_parent);
        sem_close(sem_child);
        sem_unlink(SEM_PARENT);
        sem_unlink(SEM_CHILD);
        
        write_str(STDOUT_FILENO, "Program finished\n");
    }
    
    return 0;
}