#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define LEN(arr) (sizeof(arr) / sizeof(arr[0]))

#define ERR(ERR, FORMAT, ...) errx(ERR, "err (%d): " FORMAT, ERR, ## __VA_ARGS__)
#define WARN(ERR, FORMAT, ...) warnx(ERR, "warn (%d): " FORMAT, ERR, ## __VA_ARGS__)

/* exit codes */
#define NO_ACTION		2
#define MULTIPLE_ACTIONS	2
#define INVALID_OPTION		64
#define MISSING_ARGUMENT	64

typedef struct {
	char operation; // t, x or 0 for invalid
	char **files;
	int file_count;
	char *archive_file;
} args;

args get_args(int argc, char **argv) {
	args out = {
		.operation = 0,
		.files = (char**)(malloc((argc - 1) * sizeof(char*))),
		.file_count = 0,
		.archive_file = NULL
	};

	char **argv_ = argv;
	while (*++argv_) {
		char *arg = *argv_;
		if (arg[0] == '-') {
			switch (arg[1]) {
				case 'x':
				case 't':
					if (out.operation != 0)
						ERR(MULTIPLE_ACTIONS, "A single action (-t or -x) must be specified.");
					out.operation = arg[1];
					break;

				case 'f':
					if (*++argv_)
						arg = *argv_;
					if (arg[0] == '-')
						ERR(MISSING_ARGUMENT, "Expected archive name following \'-f\'.");
					size_t archive_size = (strlen(arg) + 1) * sizeof(char);
					out.archive_file = (char*)(malloc(archive_size));
					strcpy(out.archive_file, *argv_);
					break;

				default:
					ERR(INVALID_OPTION, "No such option \"%s\".", arg);
			}
		} else {
			size_t filename_size = (strlen(arg) + 1) * sizeof(char);
			out.files[out.file_count] = (char*)(malloc(filename_size));
			strcpy(out.files[out.file_count], *argv_);
			++(out.file_count);
		}
	}

	if (!out.operation)
		ERR(NO_ACTION, "Expected -x or -t but neither was given.");
	if (!out.archive_file)
		ERR(MISSING_ARGUMENT, "Expected \'-f ARCHIVE_NAME\'.");

	return out;
}

int main(int argc, char **argv) {
	args job = get_args(argc, argv);

	printf("archive name: %s\n", job.archive_file);
	printf("action: %c\n", job.operation);
	printf("file count: %d\n", job.file_count);
	printf("files: ");
	for (int i = 0; i < job.file_count; ++i) {
		printf("%s ", job.files[i]);
	}
	printf("\n");
}

