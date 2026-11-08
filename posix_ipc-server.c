#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <string.h>
#include <stdbool.h>

float my_atof(const char *str, int *consumed) {
    float res = 0.0f;
    float frac = 0.1f;
    bool in_frac = false;
    int i = 0;

while (str[i] == ' ') i++;

int start = i;
while (str[i] != '\0' && str[i] != ' ' && str[i] != '\n') {
        if (str[i] == '.') {
            in_frac = true;
        } else if (str[i] >= '0' && str[i] <= '9') {
            if (!in_frac) {
                res = res * 10.0f + (str[i] - '0');
            } else {
                res = res + (str[i] - '0') * frac;
                frac *= 0.1f;
            }
        }
        i++;
    }
    *consumed = i;
    return (i == start) ? -1.0f : res;
}

int my_ftoa(float n, char *buf) {
    int pos = 0;
    int ipart = (int)n;
    float fpart = n - (float)ipart;

    int temp = ipart;
    int digits = 0;
    if (temp == 0) digits = 1;
    while (temp > 0) { digits++; temp /= 10; }

    for (int i = digits - 1; i >= 0; i--) {
        buf[pos + i] = (ipart % 10) + '0';
        ipart /= 10;
    }
    pos += digits;
    buf[pos++] = '.';

    for (int i = 0; i < 6; i++) {
        fpart *= 10.0f;
        int fdigit = (int)fpart;
        buf[pos++] = fdigit + '0';
        fpart -= fdigit;
    }
    buf[pos++] = '\n';
    buf[pos] = '\0';
    return pos;
}

typedef struct {
    char input[1024];
    float result;
    bool is_zero_division;
} SharedData;

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "server: not enough args\n");
        return 1;
    }

    const char *filename = argv[1];
    const char *shm_name = argv[2];
    const char *sem_req_name = argv[3];
    const char *sem_resp_name = argv[4];

    // 1 Открываем существующую память
    int shm_fd = shm_open(shm_name, O_RDWR, 0600);
    if (shm_fd == -1) { perror("shm_open server"); exit(1); }

    SharedData *shared = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) { perror("mmap server"); exit(1); }

    // 2 Открываем существующие семафоры
    sem_t *sem_req = sem_open(sem_req_name, 0);
    sem_t *sem_resp = sem_open(sem_resp_name, 0);
    if (sem_req == SEM_FAILED || sem_resp == SEM_FAILED) { perror("sem_open server"); exit(1); }

    // 3 Открываем файл
    int file = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (file == -1) { perror("open file"); exit(1); }

    // ЦИКЛ ОБРАБОТКИ
    while (1) {
        sem_wait(sem_req);

        if (shared->input[0] == '\0') {
            break;
        }

        // Обработка (парсинг и деление)
        float result = 0.0f;
        bool first_num = true;
        bool div_by_zero = false;
        char *ptr = shared->input;

        while (*ptr != '\0' && *ptr != '\n') {
            while (*ptr == ' ') ptr++;
            if (*ptr == '\0' || *ptr == '\n') break;

            int step = 0;
            float num = my_atof(ptr, &step);
            if (step == 0) break;
            ptr += step;

            if (first_num) {
                result = num;
                first_num = false;
            } else {
                if (num == 0.0f) { div_by_zero = true; break; }
                result /= num;
            }
        }

        // Записываем ответ в общую память
        shared->is_zero_division = div_by_zero;
        shared->result = result;

        if (!div_by_zero) {
            char out_buf[64];
            int out_len = my_ftoa(result, out_buf);
            write(file, out_buf, out_len);
        } else {
            sem_post(sem_resp);
            break;
        }

        sem_post(sem_resp);
    }

    close(file);
    munmap(shared, sizeof(SharedData));
    close(shm_fd);
    sem_close(sem_req);
    sem_close(sem_resp);
    return 0;
}