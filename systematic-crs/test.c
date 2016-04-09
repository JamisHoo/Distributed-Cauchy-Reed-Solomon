#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "timer.h"
#include "ec-method.h"

#define MAX_DATA_SIZE (1024 * 1024 * 512) // 64MiB
#define MAX_COLUMN (128)
#define MAX_ROW (MAX_COLUMN + 128)

uint8_t data[MAX_DATA_SIZE];
uint8_t* encoded[MAX_ROW];
uint8_t decoded[MAX_DATA_SIZE];
uint32_t rows[MAX_ROW];

void init() {
    int i;
    
    for (i = 0; i < MAX_DATA_SIZE; ++i) data[i] = rand();

    encoded[0] = (uint8_t*)malloc(MAX_DATA_SIZE);

    ec_method_initialize();
}

void swap(uint32_t* a, uint32_t* b) { uint32_t c = *a; *a = *b; *b = c; }

void measure(size_t column, size_t row, size_t data_size, 
             size_t message_remaining, size_t redundant_remaining) {
    int i;
    size_t size;
    size_t num_remaining = message_remaining + redundant_remaining;
    double time1;

    printf("%lu = %lu + %lu, %.1fMiB, recover from %lu = %lu + %lu packets \n", 
        row, column, row - column, (float)data_size / 1024 / 1024, 
        num_remaining, message_remaining, redundant_remaining);

    assert(row >= column);
    assert(data_size % (EC_METHOD_CHUNK_SIZE * column) == 0);
    assert(data_size / column * row <= MAX_DATA_SIZE);
    assert(message_remaining + redundant_remaining >= column);

    for (i = 1; i < row; ++i)
        encoded[i] = encoded[i - 1] + data_size / column;

    for (i = 0; i < column; ++i) 
        rows[i] = i;
    for (i = 0; i < 1000; ++i) 
        swap(&rows[rand() % column], &rows[rand() % column]);
    for (i = message_remaining; i < column; ++i)
        rows[i + row - column] = rows[i];

    for (i = column; i < row; ++i) 
        rows[message_remaining + i - column] = i;
    for (i = 0; i < 1000; ++i) 
        swap(&rows[message_remaining + rand() % (row - column)], 
             &rows[message_remaining + rand() % (row - column)]);

    for (i = 0; i < row; ++i) {
        if (i == redundant_remaining) printf("| ");
        if (i == num_remaining) printf("|| ");
        printf("%d ", rows[i]); 
    }
    printf("\n");

    time1 = timer_start();
    for (i = 0; i < row; ++i) {
        size = ec_method_encode(data_size, column, rows[i], data, encoded[i]);
        assert(size == data_size / column);
    }
    timer_end(time1, "encode time: %.3f \n");

    memset(decoded, 0x00, data_size);
    time1 = timer_start();
    size = ec_method_decode(data_size / column, column, rows, num_remaining,
                            encoded, decoded);
    timer_end(time1, "decode time: %.3f \n");
    assert(size == data_size);
    assert(!memcmp(decoded, data, data_size));
}

void test_round() {
    int i;
    size_t size;

    size_t column = rand() % MAX_COLUMN;
    size_t row = rand() % (MAX_ROW - MAX_COLUMN) + column;
    size_t data_size = EC_METHOD_CHUNK_SIZE * column;
    data_size *= rand() % (MAX_DATA_SIZE / (data_size / column * row) - 1) + 1;
    int num_remaining = rand() % 2? column: column + rand() % (row - column);

    printf("%lu = %lu + %lu, %.1fMiB, recover from %d packets \n", 
        row, column, row - column, (float)data_size / 1024 / 1024, num_remaining);
    
    assert(row >= column);
    assert(data_size % (EC_METHOD_CHUNK_SIZE * column) == 0);
    assert(data_size / column * row <= MAX_DATA_SIZE);

    for (i = 1; i < row; ++i)
        encoded[i] = encoded[i - 1] + data_size / column;

    for (i = 0; i < row; ++i) rows[i] = i;
    for (i = 0; i < 1000; ++i) swap(&rows[rand() % row], &rows[rand() % row]);

    for (i = 0; i < num_remaining; ++i) {
        size = ec_method_encode(data_size, column, rows[i], data, encoded[i]);
        assert(size == data_size / column);
    }

    memset(decoded, 0x00, data_size);
    size = ec_method_decode(data_size / column, column, rows, num_remaining,
                            encoded, decoded);
    assert(size == data_size);
    assert(!memcmp(decoded, data, data_size));
}

int main() {
    int seed = time(0);
    printf("random seed = %d \n", seed);
    srand(seed);

    init();
    printf("Init finished \n");

    // while (1) test_round();
    measure(16, 8 + 16, 128 << 20, 8, 8);
    
}