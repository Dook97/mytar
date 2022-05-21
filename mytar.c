/* #define DEBUG */

#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define LEN(arr) (sizeof(arr) / sizeof(arr[0]))

/* exit codes */
#define NO_ACTION		2
#define MULTIPLE_ACTIONS	2
#define INVALID_OPTION		64
#define MISSING_ARGUMENT	64
#define INVALID_FILE		2
#define NOT_IMPLEMENTED		0

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

typedef void (*operation_t)(tar_header_t*, args_t*);



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
						errx(MULTIPLE_ACTIONS, "A single action (-t or -x) must be specified");
					out.operation = arg[1];
					break;
				case 'f':
					if (*++argv_)
						arg = *argv_;
					if (arg[0] == '-')
						errx(MISSING_ARGUMENT, "Expected archive name following \'-f\'");
					size_t archive_size = (strlen(arg) + 1) * sizeof(char);
					out.archive_file = (char*)(malloc(archive_size));
					strcpy(out.archive_file, *argv_);
					break;
				default:
					errx(INVALID_OPTION, "No such option \"%s\"", arg);
			}
		} else {
			size_t filename_size = (strlen(arg) + 1) * sizeof(char);
			out.files[out.file_count] = (char*)(malloc(filename_size));
			strcpy(out.files[out.file_count], *argv_);
			++(out.file_count);
		}
	}

	if (!out.operation)
		errx(NO_ACTION, "Expected -x or -t but neither was given");
	if (!out.archive_file)
		errx(MISSING_ARGUMENT, "Expected \'-f ARCHIVE_NAME\'");

	return out;
}

int get_entry_size(tar_header_t *header) {
	int reported_size = octal_to_int(header->size);
	return (reported_size / TAR_BLOCK_SIZE + !!(reported_size % TAR_BLOCK_SIZE)) * TAR_BLOCK_SIZE;
}

bool check_magic(tar_header_t *header) {
	return !strcmp(header->magic, TMAGIC) || !strcmp(header->magic, TOLDMAGIC);
}

bool file_in_args(char *filename, args_t *args) {
	for (int i = 0; i < (args->file_count); ++i) {
		if (strcmp(filename, (args->files)[i]) == 0)
			return true;
	}
	return false;
}

void list_tar_entry(tar_header_t *header, args_t *args) {
	if ((args->file_count == 0) || file_in_args(header->name, args))
		printf("%s\n", header->name);
}

/* void extract_tar_entry(tar_header_t *header, args_t *args) { */
/* 	errx(NOT_IMPLEMENTED, "-x not implemented yet"); */
/* 	// TODO */
/* } */

operation_t get_operation(args_t *args) {
	switch (args->operation) {
		case 't':
			return list_tar_entry;
		/* case 'x': */
		/* 	return extract_tar_entry; */
		default:
			/* this really shouldn't happen */
			errx(29, "YOU GOT A PROBLEM CHIEF");
	}
}

void iterate_tar(args_t *args) {
	FILE *archive;
	tar_header_t header;
	operation_t operation = get_operation(args);
	if ((archive = fopen(args->archive_file, "r")) == NULL) {
		warnx("%s: Cannot open: No such file or directory", args->archive_file);
		errx(INVALID_FILE, "Error is not recoverable: exiting now");
	}

	fread(&header, sizeof(tar_header_t), 1, archive);
	while (memcmp(&null_block, &header, TAR_BLOCK_SIZE)) {
		(*operation)(&header, args);
		int entry_size = get_entry_size(&header);
		fseek(archive, entry_size, SEEK_CUR);
		fread(&header, sizeof(tar_header_t), 1, archive);
	}
}

int main(int argc, char **argv) {
	args_t args = get_args(argc, argv);
	iterate_tar(&args);

#ifdef DEBUG
	FILE *archive;
	tar_header_t header;
	if ((archive = fopen(args.archive_file, "r")) == NULL)
		errx(1, "fopen");
	do {
		fread(&header, sizeof(tar_header_t), 1, archive);
		if (check_magic(&header))
			list_tar_entry(&header, &args);
	} while (memcmp(&null_block, &header, TAR_BLOCK_SIZE));
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

	return 0;
}

