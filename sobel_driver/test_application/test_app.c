#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define WIDTH        600
#define HEIGHT       450
#define IN_PIXELS    (WIDTH * HEIGHT)
#define OUT_BYTES    (IN_PIXELS * 2)

#define OFF_INPUT    0
#define OFF_GRADX    270000
#define OFF_GRADY    810544

int main()
{
	int fd;
	FILE *fp;
	char *str;
	size_t num_of_bytes = 50;
	unsigned int ready;
	size_t dma_size;
	unsigned char *buf;
	unsigned char *input;
	int16_t *gradx;
	int16_t *grady;
	int i;

	// Reading the size of the buffer from /dev/sobel
	fp = fopen("/dev/sobel", "r");
	if (fp == NULL) {
		puts("Problem with opening /dev/sobel");
		return -1;
	}

	str = (char *)malloc(num_of_bytes + 1);
	getline(&str, &num_of_bytes, fp);
	sscanf(str, "ready = %u", &ready);

	getline(&str, &num_of_bytes, fp);
	sscanf(str, "dma_buffer_size = %zu", &dma_size);

	free(str);

	if (fclose(fp)) {
		puts("Problem with closing /dev/sobel");
		return -1;
	}

	// For mmap(), a file descriptor is necessary, not stream (open, not fopen)
	fd = open("/dev/sobel", O_RDWR);
	if (fd < 0) {
		puts("Problem with opening /dev/sobel za mmap");
		return -1;
	}
	buf = mmap(NULL, dma_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf == MAP_FAILED) {
		puts("Problem with mmap-ing /dev/sobel");
		close(fd);
		return -1;
	}

	input = (unsigned char *)(buf + OFF_INPUT);
	gradx = (int16_t *)(buf + OFF_GRADX);
	grady = (int16_t *)(buf + OFF_GRADY);

	// input image: rows 0-199 = 0, rows 200-449 = 255
	for (i = 0; i < IN_PIXELS; i++) {
		int row = i / WIDTH;
		input[i] = (row < 200) ? 0 : 255;
	}
	printf("Input image written: rows 0-199 = 0, rows 200-449 = 255\n");

	// Starting IP (write block until IP finishes)
	fp = fopen("/dev/sobel", "w");
	if (fp == NULL) {
		puts("Problem with opening /dev/sobel");
		munmap(buf, dma_size);
		close(fd);
		return -1;
	}
	fputs("start\n", fp);
	if (fclose(fp)) {
		puts("Problem with closing /dev/sobel");
		munmap(buf, dma_size);
		close(fd);
		return -1;
	}
	printf("IP finished\n");

	// Checking some of output values
	printf("gradX row 100: %d   gradY row 100: %d\n", gradx[100 * WIDTH + 300], grady[100 * WIDTH + 300]);
	printf("gradX row 200: %d   gradY row 200: %d\n", gradx[200 * WIDTH + 300], grady[200 * WIDTH + 300]);
	printf("gradX row 350: %d   gradY row 350: %d\n", gradx[350 * WIDTH + 300], grady[350 * WIDTH + 300]);

	// Output pictures stored into .txt files
	fp = fopen("gradx.txt", "w");
	for (i = 0; i < IN_PIXELS; i++)
		fprintf(fp, "%d\n", gradx[i]);
	fclose(fp);

	fp = fopen("grady.txt", "w");
	for (i = 0; i < IN_PIXELS; i++)
		fprintf(fp, "%d\n", grady[i]);
	fclose(fp);

	munmap(buf, dma_size);
	close(fd);
	return 0;
}
