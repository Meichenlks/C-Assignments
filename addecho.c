/*Compiles and runs on Ubuntu 12.04. Adds echo to a mono wav file. Users have the option to prompt
  the volume scale and/or delay of the echo.*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#define HEADER_SIZE 22

int main(int argc, char *argv[]) {
	int sw;
	int delay = 8000;
	int vol = 4;
	FILE *src;
	FILE *dest;
	FILE *delaysamp;
	short header[HEADER_SIZE];
	
	while ((sw = getopt(argc, argv, "d:v:")) != -1){
		switch (sw) {
			case 'd':
				errno = 0;
				delay = strtol(optarg, NULL, 10);
				if ((errno != 0)||!(delay > 0)) {
					fprintf(stderr, "Option -d (delay) requires a positive integer argument\n");
					exit(1);
				}
				break;
			case 'v':
				errno = 0;
				vol = strtol(optarg, NULL, 10);
				if ((errno != 0)||!(vol > 0)) {
					fprintf(stderr, "Option -v (volume scale) requires a positive integer argument\n");
					exit(1);
				}
				break;
			default:
				fprintf(stderr, "Usage: addecho [-d delay] [-v volume_scale] sourcewav destwav\n");
				exit(1);
		}
	}
	if (optind != (argc - 2)) {
                fprintf(stderr, "Expected 2 arguments excluding options, found %d\nUsage: addecho [-d delay] [-v volume_scale] sourcewav destwav\n", (argc-optind));
		exit(1);
	}
	if ((src = fopen(argv[optind], "r")) == NULL) {
                fprintf(stderr, "Cannot open sourcefile %s\n",argv[optind]);
		exit(1);
	}
	if ((delaysamp = fopen(argv[optind], "r")) == NULL) {
                fprintf(stderr, "Cannot open sourcefile %s\n",argv[optind]);
		exit(1);
	}
	dest = fopen(argv[optind + 1], "w");
	if (fread(header, 1, 44, src) == 0) {
        	fprintf(stderr, "Error occurred while reading from %s\n", argv[optind]);
		exit(1);
	}//Store the header
	unsigned int * sizeptr;
	sizeptr = (unsigned int *)(header + 2);
	*sizeptr = *sizeptr + delay * 2;
	sizeptr = (unsigned int *)(header + 20);
	*sizeptr = *sizeptr + delay * 2;
	if (fwrite(header, 1, 44, dest) == 0) {
                fprintf(stderr, "Error occurred while writing to %s\n", argv[optind+1]);
		exit(1);
	}//Write modified header to destwav
	
	long cur_pos = ftell(src);
	fseek(src, 0, SEEK_END);
	long size = (ftell(src) - cur_pos) / sizeof(short);
	fseek(src, cur_pos, SEEK_SET);//Store the number of samples in the source file;
	fseek(delaysamp, cur_pos, SEEK_SET);//Set delay at the first sample;
	short *orig = malloc(sizeof(short));
	short *echo = malloc(sizeof(short));
	long exceed = delay - size;
	long i;
	clearerr(src);
	clearerr(delaysamp);
	if (exceed <= 0) {
	  for (i=0;i<delay;i++) {
	    if (fread(orig, sizeof(short), 1, src) == 0) {
	      fprintf(stderr, "Error occurred while reading from %s\n", argv[optind]);
	      free(orig);
	      free(echo);
	      exit(1);
	    }
	    if (fwrite(orig, sizeof(short), 1, dest) == 0) {
	      fprintf(stderr, "Error occurred while writing to %s\n", argv[optind+1]);
	      free(orig);
	      free(echo);
	      exit(1);
	    }
	  }
	} else {
	  short *zero_writer;
	  short zero = 0;
	  zero_writer = &zero;
	  for (i=0;i<size;i++) {
	    if (fread(orig, sizeof(short), 1, src) == 0) {
	      fprintf(stderr, "Error occurred while reading from %s\n", argv[optind]);
	      free(orig);
	      free(echo);
	      exit(1);
	    }
	    if (fwrite(orig, sizeof(short), 1, dest) == 0) {
	      fprintf(stderr, "Error occurred while writing to %s\n", argv[optind+1]);
	      free(orig);
	      free(echo);
	      exit(1);
	    }
	  }
	  for (i=0;i<exceed;i++) {
	    if (fwrite(zero_writer, sizeof(short), 1, dest) == 0) {
	      fprintf(stderr, "Error occurred while writing to %s\n", argv[optind+1]);
	      free(orig);
	      free(echo);
	      exit(1);
	    }
	  }
	  while (fread(echo, sizeof(short), 1, delaysamp) != 0) {
	    *echo = *echo / vol;
	    if (fwrite(echo, sizeof(short), 1, dest) == 0) {
	      fprintf(stderr, "Error occurred while writing to %s\n", argv[optind+1]);
	      free(orig);
	      free(echo);
	      exit(1);
	    }
	  }
	  if (ferror(delaysamp) != 0) {
	      fprintf(stderr, "Error occurred while reading from %s\n", argv[optind]);
	      free(orig);
	      free(echo);
	      exit(1);
	  }
	  free(orig);
	  free(echo);
	  return 0;//End of procecssing the case where delay exceeds total number of samples
	}
	//Continue the case where delay is not larger than the number of samples
	short mixed;
	short *mixed_writer;
	mixed_writer = &mixed;
	while (fread(orig, sizeof(short), 1, src) != 0) {
	  if (fread(echo, sizeof(short), 1, delaysamp) == 0) {
	      fprintf(stderr, "Error occurred while reading from %s\n", argv[optind]);
	      free(orig);
	      free(echo);
	      exit(1);
	  }
	  mixed = *echo / vol  + *orig;
	  if (fwrite(mixed_writer, sizeof(short), 1, dest) == 0) {
	    fprintf(stderr, "Error occurred while writing to %s\n", argv[optind+1]);
	    free(orig);
	    free(echo);
	    exit(1);
	  }
	}
	if (ferror(src) != 0) {
	  fprintf(stderr, "Error occurred while reading from %s\n", argv[optind]);
	  free(orig);
	  free(echo);
	  exit(1);
	}
	while (fread(echo, sizeof(short), 1, delaysamp) != 0) {
	  //Read and write the rest of the echo.
	  *echo = *echo / vol;
	  if (fwrite(echo, sizeof(short), 1, dest) == 0) {
	      fprintf(stderr, "Error occurred while writig to %s\n", argv[optind+1]);
	      free(orig);
	      free(echo);
	      exit(1);
	  }
	}
	if (ferror(delaysamp) != 0) {
	  fprintf(stderr, "Error occurred while reading from %s\n", argv[optind]);
	  free(orig);
	  free(echo);
	  exit(1);
	}
	free(orig);
	free(echo);
	return 0;
}
