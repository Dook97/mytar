#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

/* exit codes */
#define NO_ACTION		2
#define MULTIPLE_ACTIONS	2
#define INVALID_OPTION		2
#define MISSING_ARGUMENT	64
#define INVALID_FILE		2
#define UNSUPPORTED_HEADER	2
#define TRUNCATED_ARCHIVE	2
#define NOT_IMPLEMENTED		0

#define TAR_BLOCK_SIZE		512
#define TMAGIC			"ustar"
#define TOLDMAGIC		"ustar  " // supposedly deprecated version of the magic field
#define REGTYPE			'0'  // **
#define AREGTYPE		'\0' // permitted values of typeflag

typedef unsigned int uint;
typedef unsigned long ulong;

/* tar header block */
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

/* tar data block */
typedef struct {
	char bytes[TAR_BLOCK_SIZE];
} tar_block_t;

/* global variable is automatically zero initialized */
const tar_block_t null_block;

/* holds user given arguments */
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

/* parses user provided arguments */
void get_args(char **argv, args_t *out) {
	while (*++argv) {
		char *arg = *argv;
		if (arg[0] == '-') {
			switch (arg[1]) {
				case 'x':
				case 't':
					if (out->operation)
						errx(MULTIPLE_ACTIONS, "A single action (-t or -x) must be specified");
					out->operation = arg[1];
					break;
				case 'f':
					if (*++argv)
						arg = *argv;
					if (arg[0] == '-')
						errx(MISSING_ARGUMENT, "Expected archive name following \'-f\'");
					size_t archive_size = (strlen(arg) + 1) * sizeof(char);
					out->archive_file = (char*)(malloc(archive_size));
					strcpy(out->archive_file, *argv);
					break;
				default:
					errx(INVALID_OPTION, "No such option \"%s\"", arg);
			}
		} else {
			size_t filename_size = (strlen(arg) + 1) * sizeof(char);
			out->files[out->file_count] = (char*)(malloc(filename_size));
			strcpy(out->files[out->file_count], *argv);
			++(out->file_count);
		}
	}

	if (!out->operation)
		errx(NO_ACTION, "Expected -x or -t but neither was given");
	if (!out->archive_file)
		errx(MISSING_ARGUMENT, "Expected -f");
}

/* get number of tar block up to offset - ceiled */
ulong get_block_number(size_t offset) {
	return offset / TAR_BLOCK_SIZE + !!(offset % TAR_BLOCK_SIZE);
}

size_t get_entry_size(tar_header_t *header) {
	int reported_size = octal_to_int(header->size);
	return get_block_number(reported_size) * TAR_BLOCK_SIZE;
}

size_t get_filesize(FILE *f) {
	size_t cur_pos = ftell(f);
	fseek(f, 0, SEEK_END);
	size_t out = ftell(f);
	fseek(f, cur_pos, SEEK_SET);
	return out;
}

/* bool check_magic(tar_header_t *header) { */
/* 	return !memcmp(header->magic, TMAGIC, sizeof(TMAGIC)) || !memcmp(header->magic, TOLDMAGIC, sizeof(TOLDMAGIC)); */
/* } */

/* check whether filename found in tar header block is one of those supplied by user */
bool file_in_args(char *filename, args_t *args) {
	for (int i = 0; i < args->file_count; ++i) {
		if (!strcmp(filename, (args->files)[i])) {
			/* mark file as found in archive */
			/* this will be later used by the check_fileargs function to detect invalid user input */
			/* ugly - I know :p */
			((args->files)[i])[0] = '\0';
			return true;
		}
	}
	return false;
}

/* a implementation of the -t (list files in archive) option */
void list_tar_entry(tar_header_t *header, args_t *args) {
	if (args->file_count == 0 || file_in_args(header->name, args))
		printf("%s\n", header->name);
}

/* void extract_tar_entry(tar_header_t *header, args_t *args) { */
/* 	errx(NOT_IMPLEMENTED, "-x not implemented yet"); */
/* 	// TODO */
/* } */

/* get pointer to a function which will be used to process tar header blocks */
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

/* ommits a warning when tar file ends with a lone zero 512B block */
void validate_tar_footer(FILE *archive) {
	struct {
		tar_block_t block1;
		tar_block_t block2;
	} archive_footer;

	fseek(archive, -sizeof(archive_footer), SEEK_END);
	fread(&archive_footer, sizeof(archive_footer), 1, archive);
	if (!memcmp(&null_block, &(archive_footer.block2), TAR_BLOCK_SIZE) && memcmp(&null_block, &(archive_footer.block1), TAR_BLOCK_SIZE)) {
			warnx("A lone zero block at %lu", get_block_number(ftell(archive)));
	}
}

/* a shortcut for opening files */
FILE *get_fptr(char *filename) {
	FILE *fptr;
	if ((fptr = fopen(filename, "r")) == NULL) {
		warnx("%s: Cannot open: No such file or directory", filename);
		errx(INVALID_FILE, "Error is not recoverable: exiting now");
	}
	return fptr;
}

bool reached_EOF(tar_header_t *header, FILE *archive) {
	return !memcmp(&null_block, header, TAR_BLOCK_SIZE) || feof(archive);
}

void check_typeflag(char flag) {
	if (flag != REGTYPE && flag != AREGTYPE)
		errx(UNSUPPORTED_HEADER, "Unsupported header type: %d", (int)flag);
}

/* check whether all file arguments supplied by user were found in the archive */
void check_fileargs(args_t *args) {
	bool err = false;
	for (int i = 0; i < (args->file_count); ++i) {
		if (((args->files)[i])[0] != '\0') {
			warnx("%s: Not found in archive", (args->files)[i]);
			err = true;
		}
	}
	if (err)
		errx(INVALID_FILE, "Exiting with failure status due to previous errors");
}

void check_if_truncated(FILE *archive, size_t entry_size, size_t file_size) {
	if (ftell(archive) + entry_size > file_size) {
		warnx("Unexpected EOF in archive");
		errx(TRUNCATED_ARCHIVE, "Error is not recoverable: exiting now");
	}
}

/* iterate over header blocks and perform on them the operation specified by user */
void iterate_tar(args_t *args) {
	FILE *archive = get_fptr(args->archive_file);
	size_t file_size = get_filesize(archive);
	operation_t operation = get_operation(args);
	tar_header_t header;

	while (fread(&header, TAR_BLOCK_SIZE, 1, archive), !reached_EOF(&header, archive)) {
		check_typeflag(header.typeflag);
		(*operation)(&header, args);
		fflush(stdout);
		int entry_size = get_entry_size(&header);
		check_if_truncated(archive, entry_size, file_size);
		fseek(archive, entry_size, SEEK_CUR);
	}

	validate_tar_footer(archive);
	check_fileargs(args);
}

int main(int argc, char **argv) {
	args_t args = {
		.operation = 0,
		.files = (char**)(malloc((argc - 1) * sizeof(char*))),
		.file_count = 0,
		.archive_file = NULL
	};

	get_args(argv, &args);
	iterate_tar(&args);

	return 0;
}

