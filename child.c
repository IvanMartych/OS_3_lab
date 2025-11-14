#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdio.h>

#define SHM_SIZE 4096

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
    // Открываем shared memory
    int shm_fd = shm_open("/numbers_shm", O_RDWR, 0644);
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
    
    // Первое сообщение содержит имя файла
    char filename[256];
    strncpy(filename, shared_data->data, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
    
    // Открываем файл для записи
    int file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd == -1) {
        write_str(STDERR_FILENO, "Error: cannot open output file\n");
        munmap(shared_data, SHM_SIZE);
        close(shm_fd);
        return 1;
    }
    
    char buffer[1024];
    float numbers[100];
    
    // Сигнализируем, что прочитали имя файла
    shared_data->data_ready = 0;
    
    // Основной цикл обработки данных
    while (1) {
        // Ждем новых данных от родительского процесса
        while (shared_data->data_ready == 0) {
            usleep(1000);
        }
        
        if (shared_data->data_ready == 2) {
            break; // Сигнал о завершении
        }
        
        // Копируем данные из shared memory
        strncpy(buffer, shared_data->data, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        
        // Сигнализируем, что прочитали данные
        shared_data->data_ready = 0;
        
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
    close(shm_fd);
    
    return 0;
}