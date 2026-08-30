#include <opencv2/opencv.hpp>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

using namespace cv;
using namespace std;

// Input/ouptut image sizes fixed at 600x450p
#define WIDTH        600
#define HEIGHT       450
#define IN_PIXELS    (WIDTH * HEIGHT)	// 270000
#define IN_BYTES     (IN_PIXELS * 1)	// 8-bit input  = 270000 bytes

// Offsets inside the DMA buffer, where input and ouptut images are stored
#define OFF_INPUT    0
#define OFF_GRADX    270000
#define OFF_GRADY    810544

int main(int argc, char *argv[])
{
	int fd;
	FILE *fp;
	char *str = NULL;
	size_t num_of_bytes = 50;
	unsigned int ready;
	size_t dma_size = 0;
	unsigned char *buf;
	Mat image;

	// Loading the input image
	if (argc == 2) {
		image = imread(argv[1], IMREAD_COLOR);
	}
	else {
		image = imread("../data/barcode_01.jpg", IMREAD_COLOR);
	}

	if (image.empty()) {
		cerr << "Could not load image: " << argv[1] << endl;
		return -1;
	}

	Mat gray;
	cvtColor(image, gray, COLOR_BGR2GRAY);

	// IP expects exactly 600x450, 8-bit single channel
	Mat resizedGray;
	resize(gray, resizedGray, Size(WIDTH, HEIGHT));

	cout << "Image loaded, grayscaled and resized to " << WIDTH << "x" << HEIGHT << endl;

	// Read buffer size from the driver
	fp = fopen("/dev/sobel", "r");
	if (fp == NULL) {
		perror("fopen /dev/sobel (for reading the size)");
		return -1;
	}
	str = (char *)malloc(num_of_bytes + 1);
	// driver returns: "ready = X\ndma_buffer_size = Y\n"
	getline(&str, &num_of_bytes, fp);
	sscanf(str, "ready = %u", &ready);
	getline(&str, &num_of_bytes, fp);
	if (sscanf(str, "dma_buffer_size = %zu", &dma_size) != 1 || dma_size == 0) {
		cerr << "Failed to read dma_buffer_size from driver" << endl;
		free(str);
		fclose(fp);
		return -1;
	}
	free(str);
	fclose(fp);

	// open and mmap the DMA buffer
	fd = open("/dev/sobel", O_RDWR);
	if (fd < 0) {
		perror("open /dev/sobel (mmap)");
		return -1;
	}
	buf = (unsigned char *)mmap(NULL, dma_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf == MAP_FAILED) {
		perror("mmap");
		close(fd);
		return -1;
	}

	// copy the grayscale image into the DMA buf input region
	memcpy(buf + OFF_INPUT, resizedGray.data, IN_BYTES);
	cout << "Input image copied into DMA buffer" << endl;

	// start the IP (app and driver blocked until it finishes)
	fp = fopen("/dev/sobel", "w");
	if (fp == NULL) {
		perror("fopen /dev/sobel (start)");
		munmap(buf, dma_size);
		close(fd);
		return -1;
	}
	fputs("start\n", fp);
	if (fclose(fp)) {
		perror("fclose /dev/sobel (start)");
		munmap(buf, dma_size);
		close(fd);
		return -1;
	}
	cout << "IP finished processing" << endl;

	// creating Mats for IP output images that share memory with the DMA buffer
	// Mats are valid until munmap() call
	Mat gradX_hw(HEIGHT, WIDTH, CV_16S, buf + OFF_GRADX);
	Mat gradY_hw(HEIGHT, WIDTH, CV_16S, buf + OFF_GRADY);

	// gradX to text, one value per line
	FILE *fx = fopen("gradx.txt", "w");
	for (int r = 0; r < gradX_hw.rows; r++) {
		for (int c = 0; c < gradX_hw.cols; c++) {
			fprintf(fx, "%d\n", gradX_hw.at<short>(r, c));
		}
	}
	fclose(fx);

	// gradY to text
	FILE *fy = fopen("grady.txt", "w");
	for (int r = 0; r < gradY_hw.rows; r++) {
		for (int c = 0; c < gradY_hw.cols; c++) {
			fprintf(fy, "%d\n", gradY_hw.at<short>(r, c));
		}
	}
	fclose(fy);


	// save images as .jpg for inspecting
	imwrite("hw_gradx.jpg", gradX_hw);
	imwrite("hw_grady.jpg", gradY_hw);
	cout << "Saved hw_gradx.jpg and hw_grady.jpg" << endl;

	// buf free - cant use gradX_hw and gradY_hw anymore
	munmap(buf, dma_size);
	close(fd);

	//cout << "Hardware Sobel complete" << endl;
	return 0;
}
