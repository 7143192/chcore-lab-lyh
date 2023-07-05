#include <chcore/sd.h>
#include <stdio.h>

#define TEST_FUNC(name) \
    do { \
        if (name() == 0) { \
            printf(#name" pass!\n"); \
        } else { \
            printf(#name" fail!\n"); \
        } \
    } while (0)

#define TEST_BLOCK_NUM 512

int inner_sd_io_test()
{
    int ret;
    char buffer[BLOCK_SIZE];

    chcore_connect_sd_server();

    for (int i = 0; i < TEST_BLOCK_NUM; ++i) {
        for (int j = 0; j < BLOCK_SIZE; ++j) {
            buffer[j] = (10 * i + j) % 120;
        }
        // printf("finish write into buffer, ready to do inner write test!\n");
        ret = chcore_sd_write(i, buffer);
        // printf("finish a sd write! ret = %d\n", ret);
        if (ret != 0)
            return -1;
    }
    // printf("finish write!\n");

    for (int i = TEST_BLOCK_NUM - 1; i >= 0; --i) {
        ret = chcore_sd_read(i, buffer);
        for (int j = 0; j < BLOCK_SIZE; ++j) {
            if (buffer[j] != (10 * i + j) % 120) {
                // printf("fail a read test!j = %d, get: %c, required: %c\n", j, buffer[j], (10 * i + j) % 120);
                return -1;
            }
            // printf("pass a read test!\n");    
        }
        // printf("pass a read test!\n");
    }

    return 0;
}

int main()
{
    TEST_FUNC(inner_sd_io_test);
    return 0;
}