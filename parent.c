#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>

#define SHM_SIZE 4096

typedef struct {
    char data[SHM_SIZE - 4];
    int data_ready; // 0 - не готово, 1 - готово к чтению, 2 - конец передачи
} shared_data_t;

void write_str(int fd, const char* str) {
    write(fd, str, strlen(str));
}

int main() {
    shm_unlink("/numbers_shm");
    // Создаем временный файл для shared memory
    int shm_fd = shm_open("/numbers_shm", O_RDWR | O_CREAT | O_EXCL, 0644); 
    if (shm_fd == -1) {
        write_str(STDERR_FILENO, "Error: shared memory creation failed\n");
        return 1;
    }
    
    // Устанавливаем размер shared memory
    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        write_str(STDERR_FILENO, "Error: ftruncate failed\n");
        shm_unlink("/numbers_shm");
        return 1;
    }
    
    // Отображаем shared memory в адресное пространство
    shared_data_t* shared_data = mmap(NULL, SHM_SIZE, 
                                     PROT_READ | PROT_WRITE, 
                                     MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        write_str(STDERR_FILENO, "Error: mmap failed\n");
        shm_unlink("/numbers_shm");
        return 1;
    }
    
    // Инициализируем shared memory
    shared_data->data_ready = 0;
    shared_data->data[0] = '\0';
    
    // Запрос имени файла у пользователя
    write_str(STDOUT_FILENO, "Enter filename: ");
    char filename[256];
    int bytes = read(STDIN_FILENO, filename, sizeof(filename) - 1);
    if (bytes <= 0) {
        write_str(STDERR_FILENO, "Error: reading filename failed\n");
        munmap(shared_data, SHM_SIZE);
        shm_unlink("/numbers_shm");
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
        shm_unlink("/numbers_shm");
        return 1;
    }
    
    if (pid == 0) {
        // Дочерний процесс
        execl("./child", "child", NULL);
        write_str(STDERR_FILENO, "Error: exec failed\n");
        exit(1);
    } else {
        // Родительский процесс
        sleep(1); // Даем время дочернему процессу инициализироваться
        
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
            
            // Ждем, пока дочерний процесс прочитает предыдущие данные
            while (shared_data->data_ready == 1) {
                usleep(1000); // Небольшая задержка
            }
            
            // Копируем данные в shared memory
            strncpy(shared_data->data, buffer, sizeof(shared_data->data) - 1);
            shared_data->data[sizeof(shared_data->data) - 1] = '\0';
            shared_data->data_ready = 1; // Помечаем как готовые к чтению
            
            // Проверяем на конец ввода (пустая строка)
            if (bytes == 1 && buffer[0] == '\n') {
                shared_data->data_ready = 2; // Сигнал о завершении
                break;
            }
            
            total_sent += bytes;
            
            // Защита от переполнения (грубая оценка)
            if (total_sent > SHM_SIZE * 10) {
                write_str(STDERR_FILENO, "Warning: large amount of data sent\n");
            }
        }
        
        // Ждем завершения дочернего процесса
        wait(NULL);
        
        // Очистка ресурсов
        munmap(shared_data, SHM_SIZE);
        shm_unlink("/numbers_shm");
        
        write_str(STDOUT_FILENO, "Program finished\n");
    }
    
    return 0;
}