/*Compiles and runs on Ubuntu 12.04. Removes vocals from the source wav file.*/
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
	FILE *src;
	FILE *dest;
	if (argc != 3) {
		fprintf(stderr, "Expected 2 arguments\nUsage: remvocals sourcewav destwav\n");
		exit(1);
	}
	if ((src = fopen(argv[1], "r")) == NULL) {
		fprintf(stderr, "Cannot open sourcefile %s\n", argv[1]);
		exit(1);
	}
	
	dest = fopen(argv[2], "w");
	void *ptr = malloc(44);
	if (fread(ptr, 1, 44, src) == 0) {
	  fprintf(stderr, "Error occurred while reading from %s\n", argv[1]);
		exit(1);
	}
	if (fwrite(ptr, 1, 44, dest) == 0) {
    fprintf(stderr, "Error occurred while writing to %s\n", argv[2]);
		exit(1);
	}
	short left, right;
	short *ptrl = malloc(sizeof(short));
	short *ptrr = malloc(sizeof(short));
	clearerr(src);
	while (fread(ptrl, sizeof(short), 1, src) != 0) {
		left = *ptrl;
		if (fread(ptrr, sizeof(short), 1, src) != 0) {
			right = *ptrr;
			short combined = (left - right) / 2;
			void *ptr2;
			ptr2 = &combined;
			if (fwrite(ptr2, sizeof(short), 1, dest) == 0) {
        fprintf(stderr, "Error occurred while writing to %s\n", argv[2]);
				free(ptrl);
				free(ptrr);
				exit(1);
			}
			if (fwrite(ptr2, sizeof(short), 1, dest) == 0) {
        fprintf(stderr, "Error occurred while writing to %s\n", argv[2]);
				free(ptrl);
				free(ptrr);
				exit(1);
			}
		} else {
		  fprintf(stderr, "wav file %s not correctly formatted\n", argv[1]);
			free(ptrl);
			free(ptrr);
			exit(1);
		}
	}
	if (ferror(src) != 0) {
		fprintf(stderr, "Error occurred while reading from %s\n", argv[1]);
		free(ptrl);
		free(ptrr);
		exit(1);
	}
	free(ptrl);
	free(ptrr);
	return 0;
}
