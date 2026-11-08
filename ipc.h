#include <stdbool.h>

#define SHM_SIZE 4096

typedef struct {
    char input[1024];
    float result;
    bool is_zero_division;
} SharedData;