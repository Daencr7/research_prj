#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>

#define BYTE_VALUES 256
#define MAX_N       60

#define DIR_LEFT    0
#define DIR_RIGHT   1
#define NUM_DIRS    2

#define VERSION     1

/*
 * Test levels:
 * Each N contains all distances from 1 to N.
 */
static const int test_levels[] = {
    1, 2, 3, 4, 5, 6, 10, 15, 20, 30, 40, 60
};

#define NUM_LEVELS \
    (sizeof(test_levels) / sizeof(test_levels[0]))

/*
 * Frequency table:
 *
 * freq[target][distance - 1][direction][neighbor]
 *
 * Memory:
 * 256 * 60 * 2 * 256 * 4 bytes
 * = 7,864,320 bytes (~7.5 MiB)
 */
static uint32_t freq[BYTE_VALUES][MAX_N][NUM_DIRS][BYTE_VALUES];

/* Write a 16-bit integer in little-endian format. */
static int write_u16(FILE *fp, uint16_t value)
{
    uint8_t b[2];

    b[0] = (uint8_t)(value & 0xFF);
    b[1] = (uint8_t)((value >> 8) & 0xFF);

    return fwrite(b, 1, 2, fp) == 2;
}

/* Write a 32-bit integer in little-endian format. */
static int write_u32(FILE *fp, uint32_t value)
{
    uint8_t b[4];

    b[0] = (uint8_t)(value & 0xFF);
    b[1] = (uint8_t)((value >> 8) & 0xFF);
    b[2] = (uint8_t)((value >> 16) & 0xFF);
    b[3] = (uint8_t)((value >> 24) & 0xFF);

    return fwrite(b, 1, 4, fp) == 4;
}

/*
 * Analyze input data for all distances from 1 to MAX_N.
 *
 * Circular wrapping is preserved from the previous version.
 */
static void analyze_data(const uint8_t *data, size_t size)
{
    size_t pos;
    int d;

    uint64_t processed = 0;

    printf("\nAnalyzing data up to N = %d...\n", MAX_N);

    for (pos = 0; pos < size; pos++) {

        uint8_t target = data[pos];

        for (d = 1; d <= MAX_N; d++) {

            size_t left_pos;
            size_t right_pos;

            /* LEFT neighbor */
            if (pos >= (size_t)d) {
                left_pos = pos - (size_t)d;
            } else {
                left_pos = size - ((size_t)d - pos);
            }

            /* RIGHT neighbor */
            right_pos = pos + (size_t)d;

            if (right_pos >= size) {
                right_pos -= size;
            }

            uint8_t left_neighbor = data[left_pos];
            uint8_t right_neighbor = data[right_pos];

            freq[target][d - 1][DIR_LEFT][left_neighbor]++;
            freq[target][d - 1][DIR_RIGHT][right_neighbor]++;
        }

        processed++;

        if (processed % 100000000 == 0) {
            printf("Processed: %" PRIu64 " bytes\n", processed);
        }
    }

    printf("Analysis completed.\n");
}

/*
 * Binary file structure:
 *
 * HEADER (12 bytes):
 *   Magic           4 bytes: "BCOR"
 *   Version         1 byte
 *   N               1 byte
 *   Directions      1 byte
 *   Reserved        1 byte
 *   Target count    2 bytes
 *   Neighbor count  2 bytes
 *
 * GROUPS:
 *   Active Count    2 bytes
 *
 *   Each active entry:
 *   Neighbor ID     1 byte
 *   Frequency       4 bytes
 *
 * Group ordering:
 *   Target -> Distance -> Direction
 */
static int generate_binary(int N, const char *filename)
{
    FILE *fp;

    int target;
    int distance;
    int direction;
    int neighbor;

    uint64_t active_entries = 0;
    uint64_t total_entries = 0;

    struct stat st;

    printf("\nGenerating %s...\n", filename);

    fp = fopen(filename, "wb");

    if (fp == NULL) {
        perror("Cannot create output file");
        return 0;
    }

    /* Write 12-byte header. */
    if (fwrite("BCOR", 1, 4, fp) != 4 ||
        fputc(VERSION, fp) == EOF ||
        fputc(N, fp) == EOF ||
        fputc(NUM_DIRS, fp) == EOF ||
        fputc(0, fp) == EOF ||
        !write_u16(fp, BYTE_VALUES) ||
        !write_u16(fp, BYTE_VALUES)) {

        fprintf(stderr, "Header write failed.\n");
        fclose(fp);
        return 0;
    }

    /*
     * Write groups:
     * Target -> Distance -> Direction
     *
     * Only distances 1 to N are included.
     */
    for (target = 0; target < BYTE_VALUES; target++) {

        for (distance = 0; distance < N; distance++) {

            for (direction = 0;
                 direction < NUM_DIRS;
                 direction++) {

                uint16_t count = 0;

                /* Count active neighbors. */
                for (neighbor = 0;
                     neighbor < BYTE_VALUES;
                     neighbor++) {

                    if (freq[target][distance]
                             [direction][neighbor] != 0) {
                        count++;
                    }
                }

                /* Write active count. */
                if (!write_u16(fp, count)) {
                    fprintf(stderr, "Group write failed.\n");
                    fclose(fp);
                    return 0;
                }

                /* Write non-zero entries. */
                for (neighbor = 0;
                     neighbor < BYTE_VALUES;
                     neighbor++) {

                    uint32_t frequency =
                        freq[target][distance]
                            [direction][neighbor];

                    if (frequency == 0) {
                        continue;
                    }

                    if (fputc(neighbor, fp) == EOF ||
                        !write_u32(fp, frequency)) {

                        fprintf(stderr, "Entry write failed.\n");
                        fclose(fp);
                        return 0;
                    }

                    active_entries++;
                }

                total_entries += BYTE_VALUES;
            }
        }
    }

    if (fclose(fp) != 0) {
        fprintf(stderr, "File close failed.\n");
        return 0;
    }

    if (stat(filename, &st) != 0) {
        perror("Cannot get file size");
        return 0;
    }

    printf("-----------------------------------\n");
    printf("N                  : %d\n", N);

    printf("Total possible     : %" PRIu64 "\n",
           total_entries);

    printf("Active entries     : %" PRIu64 "\n",
           active_entries);

    printf("Removed zero       : %" PRIu64 "\n",
           total_entries - active_entries);

    printf("Actual file size   : %" PRIu64 " bytes\n",
           (uint64_t)st.st_size);

    printf("File size          : %.3f MiB\n",
           (double)st.st_size / (1024.0 * 1024.0));

    printf("Output             : %s\n", filename);
    printf("-----------------------------------\n");

    return 1;
}

int main(int argc, char *argv[])
{
    FILE *fp;

    uint8_t *data;

    off_t file_size;
    size_t read_size;

    const char *input_file;

    size_t i;

    char output_filename[64];

    if (argc != 2) {
        printf("Usage: %s <input_file>\n", argv[0]);
        printf("Example: %s enwik9\n", argv[0]);
        return 1;
    }

    input_file = argv[1];

    printf("===================================\n");
    printf("BYTE CORRELATION DATASHEET GENERATOR\n");
    printf("===================================\n");

    /* Open input file. */
    fp = fopen(input_file, "rb");

    if (fp == NULL) {
        perror("Cannot open input file");
        return 1;
    }

    /* Get input file size. */
    if (fseeko(fp, 0, SEEK_END) != 0) {
        perror("File seek failed");
        fclose(fp);
        return 1;
    }

    file_size = ftello(fp);

    if (file_size <= MAX_N) {
        fprintf(stderr, "Input file is too small.\n");
        fclose(fp);
        return 1;
    }

    if ((uintmax_t)file_size > (uintmax_t)SIZE_MAX) {
        fprintf(stderr, "Input file is too large for this system.\n");
        fclose(fp);
        return 1;
    }

    if (fseeko(fp, 0, SEEK_SET) != 0) {
        perror("File seek failed");
        fclose(fp);
        return 1;
    }

    printf("Input file : %s\n", input_file);

    printf("Input size : %.3f MiB\n",
           (double)file_size / (1024.0 * 1024.0));

    /* Load input into RAM. */
    data = (uint8_t *)malloc((size_t)file_size);

    if (data == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        fclose(fp);
        return 1;
    }

    read_size = fread(data, 1, (size_t)file_size, fp);

    fclose(fp);

    if (read_size != (size_t)file_size) {
        fprintf(stderr, "Input file read failed.\n");
        free(data);
        return 1;
    }

    printf("Input loaded successfully.\n");

    /*
     * Analyze once for N = 1 to MAX_N.
     */
    analyze_data(data, read_size);

    free(data);
    data = NULL;

    /*
     * Generate all requested datasheets.
     */
    printf("\nGenerating all test levels...\n");

    for (i = 0; i < NUM_LEVELS; i++) {

        int N = test_levels[i];

        snprintf(output_filename,
                 sizeof(output_filename),
                 "correlation_N%d.bin",
                 N);

        if (!generate_binary(N, output_filename)) {
            fprintf(stderr,
                    "Failed to generate datasheet for N=%d\n",
                    N);
            return 1;
        }
    }

    printf("\n===================================\n");
    printf("ALL DATASHEETS GENERATED SUCCESSFULLY\n");
    printf("===================================\n");

    return 0;
}
