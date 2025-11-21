#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdio.h>
#include <semaphore.h>

#define SHM_SIZE 4096
#define SHM_NAME "/shared_memory"
#define SEM_PARENT "/sem_parent_ready"
#define SEM_CHILD "/sem_child_ready"

typedef struct {
    char data[SHM_SIZE - 4];
    int data_ready;
} shared_data_t;

void write_str(int fd, const char* str) {
    write(fd, str, strlen(str));
}

void float_to_str(float num, char* buf) {
    int pos = 0;
    
    if (num < 0) {
        buf[pos++] = '-';
        num = -num;
    }
    
    int integer = (int)num;
    float fraction = num - integer;
    
    if (integer == 0) {
        buf[pos++] = '0';
    } else {
        char temp[20];
        int temp_len = 0;
        int n = integer;
        
        while (n > 0) {
            temp[temp_len++] = '0' + (n % 10);
            n /= 10;
        }
        
        for (int i = temp_len - 1; i >= 0; i--) {
            buf[pos++] = temp[i];
        }
    }
    
    buf[pos++] = '.';
    
    int frac_part = (int)(fraction * 100 + 0.5);
    buf[pos++] = '0' + (frac_part / 10);
    buf[pos++] = '0' + (frac_part % 10);
    
    buf[pos] = '\0';
}

int main(int argc, char* argv[]) {
    // Открываем существующие named семафоры
    sem_t *sem_parent = sem_open(SEM_PARENT, 0);
    if (sem_parent == SEM_FAILED) {
        write_str(STDERR_FILENO, "Error: sem_open parent failed in child\n");
        return 1;
    }
    
    sem_t *sem_child = sem_open(SEM_CHILD, 0);
    if (sem_child == SEM_FAILED) {
        write_str(STDERR_FILENO, "Error: sem_open child failed in child\n");
        sem_close(sem_parent);
        return 1;
    }
    
    // Открываем POSIX shared memory объект
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0644);
    if (shm_fd == -1) {
        write_str(STDERR_FILENO, "Error: cannot open shared memory\n");
        return 1;
    }
    
    shared_data_t* shared_data = mmap(NULL, SHM_SIZE, 
                                     PROT_READ | PROT_WRITE, 
                                     MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        write_str(STDERR_FILENO, "Error: mmap failed in child\n");
        close(shm_fd);
        return 1;
    }
    
    close(shm_fd); // Дескриптор больше не нужен после mmap
    
    // Первое сообщение содержит имя файла
    char filename[256];
    strncpy(filename, shared_data->data, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
    
    // Открываем файл для записи
    int file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd == -1) {
        write_str(STDERR_FILENO, "Error: cannot open output file\n");
        munmap(shared_data, SHM_SIZE);
        return 1;
    }
    
    char buffer[1024];
    float numbers[100];
    
    // Увеличиваем семафор - сигнализируем, что прочитали имя файла
    shared_data->data_ready = 0;
    sem_post(sem_child);
    
    // Основной цикл обработки данных
    while (1) {
        // Ждём семафор от родительского процесса (данные готовы)
        sem_wait(sem_parent);
        
        if (shared_data->data_ready == 2) {
            break; // Сигнал о завершении
        }
        
        // Копируем данные из memory-mapped file
        strncpy(buffer, shared_data->data, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        
        // Увеличиваем семафор - сигнализируем, что прочитали данные
        shared_data->data_ready = 0;
        sem_post(sem_child);
        
        // Парсинг чисел
        int count = 0;
        char* ptr = buffer;
        
        while (*ptr != '\0' && count < 100) {
            while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n') ptr++;
            if (*ptr == '\0') break;
            
            char* end;
            float num = strtod(ptr, &end);
            
            if (end != ptr) {
                numbers[count++] = num;
                ptr = end;
            } else {
                ptr++;
            }
        }
        
        // Обработка чисел
        if (count > 0) {
            float total = 0;
            
            for (int i = 0; i < count; i++) {
                total += numbers[i];
            }
            
            // Запись в файл
            write_str(file_fd, "Numbers:");
            
            char num_buf[32];
            for (int i = 0; i < count; i++) {
                write_str(file_fd, " ");
                float_to_str(numbers[i], num_buf);
                write_str(file_fd, num_buf);
            }
            
            write_str(file_fd, "\nSum: ");
            float_to_str(total, num_buf);
            write_str(file_fd, num_buf);
            write_str(file_fd, "\n\n");
        }
    }
    
    // Очистка ресурсов
    close(file_fd);
    munmap(shared_data, SHM_SIZE);
    
    // Закрываем семафоры
    sem_close(sem_parent);
    sem_close(sem_child);
    
    return 0;
}