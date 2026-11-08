#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>

#define SERVER_PROGRAM_NAME "posix_ipc-server"

char shm_name[256];
char sem_req_name[256];
char sem_resp_name[256];

int my_strlen(const char *str) {
    int len = 0;
    while (str[len] != '\0') len++;
    return len;
}

void cleanup_garbage(int signum) {
    const char msg[] = "\n[Garbage Collector] Caught signal! Cleaning up\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    shm_unlink(shm_name);
    sem_unlink(sem_req_name);
    sem_unlink(sem_resp_name);

    exit(128 + signum);
}

void my_concat_path(char *dest, const char *path, const char *name) {
    int i = 0;
    while (path[i] != '\0') {
        dest[i] = path[i];
        i++;
    }
    dest[i++] = '/';
    int j = 0;
    while (name[j] != '\0') {
        dest[i + j] = name[j];
        j++;
    }
    dest[i + j] = '\0';
}

typedef struct {
    char input[1024];
    float result;
    bool is_zero_division;
} SharedData;

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <output_filename>\n", argv[0]);
        return 1;
    }

    snprintf(shm_name, sizeof(shm_name), "/my_shm_%d", getpid());
    
    // Используем PID текущего (родительского) процесса для уникальности
    snprintf(shm_name, sizeof(shm_name), "/my_shm_%d", getpid());
    snprintf(sem_req_name, sizeof(sem_req_name), "/my_sem_req_%d", getpid());
    snprintf(sem_resp_name, sizeof(sem_resp_name), "/my_sem_resp_%d", getpid());

    // РЕГИСТРАЦИЯ СБОРЩИКА МУСОРА
    signal(SIGINT, cleanup_garbage);
    signal(SIGTERM, cleanup_garbage);

    // 1 Создаем именованную общую память
    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
    if (shm_fd == -1) { perror("shm_open"); exit(1); }

    // 2 Задаем размер
    if (ftruncate(shm_fd, sizeof(SharedData)) == -1) { perror("ftruncate"); exit(1); }

    // 3 Отображаем память
    SharedData *shared = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) { perror("mmap"); exit(1); }

    // 4 Создаем семафоры (начальные значения = 0)
    sem_t *sem_req = sem_open(sem_req_name, O_CREAT, 0600, 0);
    sem_t *sem_resp = sem_open(sem_resp_name, O_CREAT, 0600, 0);
    if (sem_req == SEM_FAILED || sem_resp == SEM_FAILED) { perror("sem_open"); exit(1); }

    // ЗАПУСК СЕРВЕРА
    char progpath[1024];
    ssize_t len = readlink("/proc/self/exe", progpath, sizeof(progpath) - 1);
    if (len == -1) {
        const char msg[] = "error: failed to read full program path\n";
        write(STDERR_FILENO, msg, my_strlen(msg));
        exit(EXIT_FAILURE);
    }
    while (len > 0 && progpath[len] != '/') {
        --len;
    }

    progpath[len] = '\0';

    pid_t child = fork();
    if (child == 0) {
        char path[2048];
        my_concat_path(path, progpath, "posix_ipc-server");
        
        // Передаем имена в качестве аргументов
        char *const args[] = {"posix_ipc-server", argv[1], shm_name, sem_req_name, sem_resp_name, NULL};
        execv(path, args);
        perror("execv");
        exit(1);
    }

    // РОДИТЕЛЬСКИЙ ЦИКЛ
    char buf[1024];
    printf("[Parent] Enter floats (or empty line to exit):\n");

    while (fgets(buf, sizeof(buf), stdin)) {
        // Проверка на пустую строку
        if (buf[0] == '\n') break;

        strncpy(shared->input, buf, sizeof(shared->input) - 1);
        
        sem_post(sem_req);
        sem_wait(sem_resp);

        if (shared->is_zero_division) {
            printf("[Parent] Division by zero detected in child! Exiting...\n");
            break;
        }
        printf("[Parent] Server processed successfully. Result: %f\n", shared->result);
    }

    // ОЧИСТКА
    shared->input[0] = '\0';
    sem_post(sem_req);
    
    wait(NULL);

    munmap(shared, sizeof(SharedData));
    close(shm_fd);
    sem_close(sem_req);
    sem_close(sem_resp);
    
    shm_unlink(shm_name);
    sem_unlink(sem_req_name);
    sem_unlink(sem_resp_name);

    return 0;
}