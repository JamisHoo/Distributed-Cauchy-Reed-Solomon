#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "timer.h"
#include "ec-method.h"

#define MAX_DATA_SIZE (1024 * 1024 * 512)
#define MAX_COLUMN (128)
#define MAX_ROW (MAX_COLUMN + 128)

uint8_t* data;
uint8_t* encoded[MAX_ROW];
uint8_t* decoded;
uint32_t rows[MAX_ROW];

void init() {
    int64_t i;
    
    i = posix_memalign((void**)&data, 32, MAX_DATA_SIZE);
    assert(!i);
    i = posix_memalign((void**)&encoded[0], 32, MAX_DATA_SIZE);
    assert(!i);
    i = posix_memalign((void**)&decoded, 32, MAX_DATA_SIZE);
    assert(!i);

    for (i = 0; i < MAX_DATA_SIZE; ++i) data[i] = rand();

    ec_method_initialize();
}

void swap(uint32_t* a, uint32_t* b) { uint32_t c = *a; *a = *b; *b = c; }

void measure(size_t column, size_t row, size_t data_size, 
             size_t message_remaining, size_t redundant_remaining) {
    int i;
    size_t size;
    size_t num_remaining = message_remaining + redundant_remaining;
    double time1, time2, time3;

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

    memset(encoded[0], 0x00, data_size / column * row);

    time1 = timer_start();
    time1 = time2 = 0;
    for (i = 0; i < row; ++i) {
        time3 = timer_start();
        size = ec_method_encode(data_size, column, rows[i], data, encoded[i]);
        if (rows[i] < column) 
            time1 += timer_start() - time3;
        else
            time2 += timer_start() - time3;
        assert(size == data_size / column);
    }
    printf("encode time: %.3f = %.3f + %.3f \n", time1 + time2, time1, time2);

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

    size_t column = 1 + rand() % (MAX_COLUMN - 1);
    size_t row = rand() % (MAX_ROW - MAX_COLUMN) + column;
    size_t data_size = EC_METHOD_CHUNK_SIZE * column;
    data_size *= rand() % (MAX_DATA_SIZE / (data_size / column * row)) + 1;
    int num_remaining = rand() % 2? column: column + rand() % (row - column + 1);

    printf("%3lu = %3lu + %3lu, %6.1fMiB, recover from %3d packets \n", 
        row, column, row - column, (float)data_size / 1024 / 1024, num_remaining);
    
    assert(row >= column);
    assert(data_size % (EC_METHOD_CHUNK_SIZE * column) == 0);
    assert(data_size <= MAX_DATA_SIZE);
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


    size_t n_segs = data_size / EC_METHOD_CHUNK_SIZE / column;
    size_t s = rand() % n_segs;
    size_t e = s + rand() % (n_segs - s);
    size_t part_data_size = EC_METHOD_CHUNK_SIZE * column * (e - s);
    size_t part_data_offset = EC_METHOD_CHUNK_SIZE * column * s;

    for (i = 0; i < row; ++i)
        encoded[i] += part_data_offset / column;

    memset(decoded + part_data_offset, 0x00, part_data_size);
    size = ec_method_decode(part_data_size / column,
                            column, rows, num_remaining, encoded,
                            decoded + part_data_offset);
    for (i = 0; i < row; ++i)
        encoded[i] -= part_data_offset / column;
    assert(size == part_data_size);
    assert(!memcmp(data + part_data_offset, decoded + part_data_offset, part_data_size));
}

int main() {
    int i;
    int seed = time(0);
    printf("random seed = %d \n", seed);
    srand(seed);

    init();
    printf("Init finished \n");

    // while (1) test_round();
    for (i = 0; i < 5; ++i)
        measure(16, 8 + 16, 256 << 20, 8, 8);
    
}
