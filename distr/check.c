#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/sysinfo.h>

#include "nodes.h"
#include "ec/ec-method.h"

size_t data_size;
uint8_t* decoded;
uint8_t* data;
uint8_t* output[ROW];
uint32_t row[ROW];

void init(){
    int i;
    FILE* f;
    char filename[PATH_MAX];
    size_t size;

    f = fopen("data", "rb");
    fseek(f, 0, SEEK_END);
    data_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    data = (uint8_t *)malloc(data_size);
    decoded = (uint8_t *)malloc(data_size);

    size = fread(data, data_size, 1, f);
    assert(size == 1);
    fclose(f);

    for(i = 0;i < ROW; ++i) {
        output[i] = (uint8_t*)malloc(data_size / COLUMN);
        row[i] = i;

        sprintf(filename, "data_%d", i);
        f = fopen(filename, "rb");
        size = fread(output[i], data_size / COLUMN, 1, f);
        assert(size == 1);
        fclose(f);
    }

    ec_method_initialize();
}

int main(int argc, char** argv){
    int i, j, times;
    size_t size;

    init();
    printf("Finish init\n");

    size = data_size / COLUMN;
    ec_method_decode(size, COLUMN, row, output, decoded);

    if (memcmp(data, decoded, data_size)) 
        printf("wrong! \n");
    else 
        printf("right. \n");

    return 0;
}