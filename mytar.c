#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

/* exit codes */
#define	NO_ACTION		2
#define	MULTIPLE_ACTIONS	2
#define	INVALID_OPTION		2
#define	MISSING_ARGUMENT	64
#define	INVALID_FILE		2
#define	UNSUPPORTED_HEADER	2
#define	TRUNCATED_ARCHIVE	2
#define NOT_TAR			2
#define	NO_MEMORY		1
#define	NOT_IMPLEMENTED		0

#define	TAR_BLOCK_SIZE		512
#define	TMAGIC			"ustar"
#define	TOLDMAGIC		"ustar  "
#define	REGTYPE			'0'
#define	AREGTYPE		'\0'

typedef unsigned int uint;
typedef unsigned long ulong;

/* tar header block */
typedef struct {				/* byte offset */
	char name[100];				/*   0 */
	char mode[8];				/* 100 */
	char uid[8];				/* 108 */
	char gid[8];				/* 116 */
	char size[12];				/* 124 */
	char mtime[12];				/* 136 */
	char chksum[8];				/* 148 */
	char typeflag;				/* 156 */
	char linkname[100];			/* 157 */
	char magic[6];				/* 257 */
	char version[2];			/* 263 */
	char uname[32];				/* 265 */
	char gname[32];				/* 297 */
	char devmajr[8];			/* 329 */
	char devminor[8];			/* 337 */
	char prefix[155];			/* 345 */
	char padding[TAR_BLOCK_SIZE - 500];	/* 500 */
} tar_header_t;

/* tar data block */
typedef struct {
	char bytes[TAR_BLOCK_SIZE];
} tar_block_t;

/* global variable is automatically zero initialized */
const tar_block_t null_block;

/* holds user given arguments */
typedef struct {
	char operation;		// t, x or 0 for invalid
	char **files;
	char *archive_file;
	bool verbose;
	int file_count;
} args_t;

typedef void (*operation_t)(tar_header_t *, args_t *, FILE *);

/* -------------------------------------------------------------------------- */

void *get_memory(size_t bytes) {
	void *ptr;
	if ((ptr = malloc(bytes)) == NULL)
		errx(NO_MEMORY, "Out of memory");
	return ptr;
}

/* convert null terminated string representing an octal number to int */
int octal_to_int(char *oct) {
	size_t len = strlen(oct);
	int out = 0;
	for (uint order = 1, i = 0; i < len; ++i) {
		out += (oct[len - i - 1] - '0') * order;
		order *= 8;
	}
	return out;
}

/* parses user provided arguments */
void get_args(int argc, char **argv, args_t *args) {
	memset(args, 0, sizeof(args_t));
	args->files = (char **)(get_memory(argc * sizeof(char *)));

	while (*++argv) {
		char *arg = *argv;
		if (arg[0] == '-') {
			switch (arg[1]) {
			case 'x':
			case 't':
				if (args->operation)
					errx(MULTIPLE_ACTIONS, "A single action (-t or -x) must be specified");
				args->operation = arg[1];
				break;
			case 'f':
				args->archive_file = *++argv;
				break;
			case 'v':
				args->verbose = true;
				break;
			default:
				errx(INVALID_OPTION, "No such option \"%s\"", arg);
			}
		} else {
			(args->files)[args->file_count] = arg;
			++(args->file_count);
		}
	}

	if (!args->operation)
		errx(NO_ACTION, "Expected -x or -t but neither was given");
	if (!args->archive_file)
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
	long cur_pos = ftell(f);
	fseek(f, 0, SEEK_END);
	long out = ftell(f);
	fseek(f, cur_pos, SEEK_SET);
	return out;
}

void check_magic(tar_header_t *header) {
	if (!!memcmp(header->magic, TMAGIC, sizeof(TMAGIC)) && !!memcmp(header->magic, TOLDMAGIC, sizeof(TOLDMAGIC))) {
		warnx("This does not look like a tar archive");
		errx(NOT_TAR, "Exiting with failure status due to previous errors");
	}
}

/* check whether filename found in tar header block is one of those supplied by user */
bool file_in_args(char *filename, args_t *args) {
	for (int i = 0; i < args->file_count; ++i) {
		if (!strcmp(filename, (args->files)[i])) {
			(args->files)[i][0] = '\0';	// mark file as found in archive
			return true;
		}
	}
	return false;
}

/* an implementation of the -t (list files in archive) option */
void list_tar_entry(tar_header_t *header, args_t *args, FILE *archive) {
	printf("%s\n", header->name);
	(void)args; (void)archive;	// a hack to stop compiler from complaining about unused params
}

/* an implementaion of the -x (extract) option */
void extract_tar_entry(tar_header_t *header, args_t *args, FILE *archive) {
	FILE *f = fopen(header->name, "w");
	if (!f)
		errx(INVALID_FILE, "Couldn't open file \"%s\" for writing", header->name);
	long cur_pos = ftell(archive);

	int f_size = octal_to_int(header->size);
	tar_block_t block;
	for (int i = 0; i < f_size / TAR_BLOCK_SIZE; ++i) {
		fread(&block, TAR_BLOCK_SIZE, 1, archive);
		fwrite(&block, TAR_BLOCK_SIZE, 1, f);
	}
	fread(&block, f_size % TAR_BLOCK_SIZE, 1, archive);
	fwrite(&block, f_size % TAR_BLOCK_SIZE, 1, f);

	fseek(archive, cur_pos, SEEK_SET);
	fclose(f);

	if (args->verbose)
		printf("%s\n", header->name);
}

/* get pointer to a function which will be used to process tar header blocks */
operation_t get_operation(args_t *args) {
	switch (args->operation) {
		case 't':
			return list_tar_entry;
		case 'x':
			return extract_tar_entry;
	}
	errx(20, "Internal error in function get_operation");
}

/* ommits a warning when tar file ends with a lone zero 512B block */
void validate_tar_footer(FILE *archive) {
	long cur_pos = ftell(archive);
	struct {
		tar_block_t block1;
		tar_block_t block2;
	} archive_footer;

	fseek(archive, -sizeof(archive_footer), SEEK_END);
	fread(&archive_footer, sizeof(archive_footer), 1, archive);
	if (!memcmp(&null_block, &(archive_footer.block2), TAR_BLOCK_SIZE)
	    && memcmp(&null_block, &(archive_footer.block1), TAR_BLOCK_SIZE))
		warnx("A lone zero block at %lu", get_block_number(ftell(archive)));

	fseek(archive, cur_pos, SEEK_SET);
}

bool reached_EOF(tar_header_t *header, FILE *archive) {
	return !memcmp(&null_block, header, TAR_BLOCK_SIZE) || feof(archive);
}

void check_typeflag(char flag) {
	if (flag != REGTYPE && flag != AREGTYPE)
		errx(UNSUPPORTED_HEADER, "Unsupported header type: %d", (int)flag);
}

/* check whether all file arguments supplied by user were found in the archive */
/* this is done by checking the presence of a mark we set when calling file_in_args */
void check_fileargs(args_t *args) {
	bool err = false;
	for (int i = 0; i < args->file_count; ++i) {
		if ((args->files)[i][0] != '\0') {
			warnx("%s: Not found in archive", (args->files)[i]);
			err = true;
		}
	}
	if (err)
		errx(INVALID_FILE, "Exiting with failure status due to previous errors");
}

void check_if_truncated(long cur_pos, size_t entry_size, size_t file_size) {
	if (cur_pos + entry_size > file_size) {
		warnx("Unexpected EOF in archive");
		errx(TRUNCATED_ARCHIVE, "Error is not recoverable: exiting now");
	}
}

/* iterate over header blocks and perform on them the operation specified by user */
void iterate_tar(args_t *args, FILE *archive) {
	size_t file_size = get_filesize(archive);
	operation_t operation = get_operation(args);
	tar_header_t header;
	int entry_size;
	while (fread(&header, TAR_BLOCK_SIZE, 1, archive), !reached_EOF(&header, archive)) {
		if (file_in_args(header.name, args) || args->file_count == 0) {
			check_magic(&header);
			check_typeflag(header.typeflag);
			(*operation)(&header, args, archive);
			fflush(stdout);
		}
		entry_size = get_entry_size(&header);
		fseek(archive, entry_size, SEEK_CUR);
	}
	check_if_truncated(ftell(archive) - entry_size, entry_size, file_size);
}

int main(int argc, char **argv) {
	args_t args;
	get_args(argc, argv, &args);

	FILE *archive = fopen(args.archive_file, "r");
	if (archive == NULL) {
		warnx("%s: Cannot open: No such file or directory", args.archive_file);
		errx(INVALID_FILE, "Error is not recoverable: exiting now");
	}

	iterate_tar(&args, archive);
	validate_tar_footer(archive);
	check_fileargs(&args);

	fclose(archive);

	return 0;
}
