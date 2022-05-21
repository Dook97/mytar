#define DEBUG

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

#define TAR_BLOCK_SIZE		512
#define TMAGIC			"ustar"
#define TOLDMAGIC		"ustar  "
#define REGTYPE			'0'
#define AREGTYPE		'\0'

typedef unsigned int uint;

typedef struct {                     /* byte offset */
	char name[100];                      /*   0 */
	char mode[8];                        /* 100 */
	char uid[8];                         /* 108 */
	char gid[8];                         /* 116 */
	char size[12];                       /* 124 */
	char mtime[12];                      /* 136 */
	char chksum[8];                      /* 148 */ // simple sum of all fields in the header, excluding the checksum field itself
	char typeflag;                       /* 156 */ // filetype (directory etc) - this implementation only accepts '0' and '\0'
	char linkname[100];                  /* 157 */
	char magic[6];                       /* 257 */
	char version[2];                     /* 263 */
	char uname[32];                      /* 265 */
	char gname[32];                      /* 297 */
	char devmajor[8];                    /* 329 */
	char devminor[8];                    /* 337 */
	char prefix[155];                    /* 345 */
	char padding[TAR_BLOCK_SIZE - 500];  /* 500 */
} tar_header_t;

typedef struct {
	char bytes[TAR_BLOCK_SIZE];
} tar_block_t;

const tar_block_t null_block;

typedef struct {
	char operation; // t, x or 0 for invalid
	char **files;
	int file_count;
	char *archive_file;
} args_t;



/* -------------------------------------------------------------------------------- */



/* convert null terminated string representing an octal number to int */
int octal_to_int(char *oct) {
	size_t len = strlen(oct);
	int order = 1;
	int out = 0;
	for (uint i = 0; i < len; ++i) {
		out += (oct[len - i - 1] - '0') * order;
		order *= 8;
	}
	return out;
}

args_t get_args(int argc, char **argv) {
	args_t out = {
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
					if (out.operation)
						ERR(MULTIPLE_ACTIONS, "A single action (-t or -x) must be specified");
					out.operation = arg[1];
					break;

				case 'f':
					if (*++argv_)
						arg = *argv_;
					if (arg[0] == '-')
						ERR(MISSING_ARGUMENT, "Expected archive name following \'-f\'");
					size_t archive_size = (strlen(arg) + 1) * sizeof(char);
					out.archive_file = (char*)(malloc(archive_size));
					strcpy(out.archive_file, *argv_);
					break;

				default:
					ERR(INVALID_OPTION, "No such option \"%s\"", arg);
			}
		} else {
			size_t filename_size = (strlen(arg) + 1) * sizeof(char);
			out.files[out.file_count] = (char*)(malloc(filename_size));
			strcpy(out.files[out.file_count], *argv_);
			++(out.file_count);
		}
	}

	if (!out.operation)
		ERR(NO_ACTION, "Expected -x or -t but neither was given");
	if (!out.archive_file)
		ERR(MISSING_ARGUMENT, "Expected \'-f ARCHIVE_NAME\'");

	return out;
}



int main(int argc, char **argv) {
	args_t args = get_args(argc, argv);

#ifdef DEBUG
	FILE *archive;
	tar_header_t tar_header;
	if ((archive = fopen(args.archive_file, "r")) == NULL)
		ERR(1, "fopen");
	do {
		fread(&tar_header, sizeof(tar_header_t), 1, archive);
		if (check_magic(&tar_header))
			printf("file name: %s\n", tar_header.name);
	} while (memcmp(&null_block, &tar_header, TAR_BLOCK_SIZE));
	fclose(archive);


	printf("archive name: %s\n", args.archive_file);
	printf("action: %c\n", args.operation);
	printf("file count: %d\n", args.file_count);
	printf("files: ");
	for (int i = 0; i < args.file_count; ++i) {
		printf("\'%s\' ", args.files[i]);
	}
	printf("\n");
#endif
}

