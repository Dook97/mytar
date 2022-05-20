#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

/* exit codes */
#define NO_ACTION		2
#define MULTIPLE_ACTIONS	2
#define INVALID_OPTION		64
#define MISSING_ARGUMENT	64

typedef struct {
	char operation; // t, x or 0 for invalid
	char **files;
	int file_count;
} job_t;

void err_(int code, char *msg) {
	errx(code, "err (%d): %s", code, msg);
}

job_t get_args(int argc, char **argv) {
	job_t out = {
		.operation = 0,
		.files = (char**)(malloc((argc - 1) * sizeof(char*))),
		.file_count = 0
	};

	char **argv_ = argv;
	bool filemode = false;
	while (*++argv_) {
		char *arg = *argv_;
		if (arg[0] == '-') {
			switch (arg[1]) {
				case 't':
				case 'x':
					if (out.operation != 0)
						err_(MULTIPLE_ACTIONS, "A single action (-t or -x) must be specified.");
					out.operation = arg[1];
					break;
				case 'f':
					filemode = true;
					break;
				default:
					errx(INVALID_OPTION, "err (%d): No such option \"%s\".", INVALID_OPTION, arg);
					break;
			}
		}
		else {
			if (filemode) {
				size_t filename_size = strlen(arg) * sizeof(char);
				*(out.files + out.file_count) = (char*)(malloc(filename_size));
				strcpy(*(out.files + out.file_count), *argv_);
				++(out.file_count);
			} else {
				err_(MISSING_ARGUMENT, "-f was expected before filename.");
			}
		}
	}

	if (!out.operation)
		err_(NO_ACTION, "Expected -x or -t but neither was given.");

	return out;
}

int main(int argc, char **argv) {
	job_t job = get_args(argc, argv);
	printf("action: %c\n", job.operation);
	printf("file count: %d\n", job.file_count);
	printf("filename: ");
	for (int i = 0; i < job.file_count; ++i) {
		printf("%s ", job.files[i]);
	}
	printf("\n");
}

