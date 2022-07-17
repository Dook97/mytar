#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <errno.h>
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
#define NOT_TAR			2
#define NO_MEMORY		1

/* tar constants */
#define TAR_BLOCK_SIZE		512
#define TMAGIC			"ustar"
#define TOLDMAGIC		"ustar  "
#define REGTYPE			'0'
#define AREGTYPE		'\0'

/* avoid problems with stdout being buffered while stderr isnt */
#define WARN(msg, ...)		{fflush(stdout);warn((msg), ## __VA_ARGS__);}
#define WARNX(msg, ...)		{fflush(stdout);warnx((msg), ## __VA_ARGS__);}
#define ERR(code, msg, ...)	{fflush(stdout);err((code), (msg), ## __VA_ARGS__);}
#define ERRX(code, msg, ...)	{fflush(stdout);errx((code), (msg), ## __VA_ARGS__);}

/* convenience stuff */
#define	min(a, b)		(((a) < (b)) ? (a) : (b))
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
/* a null tar block will be useful for detecting end of archive and such */
const tar_block_t null_block;

/* holds user given arguments */
typedef struct args {
	int file_count;
	bool verbose;
	char **files;
	char *archive_file;
	void (*operation)(tar_header_t *, struct args *, FILE *);
} args_t;

/* -------------------------------------------------------------------------- */

void *get_memory(size_t bytes) {
	void *ptr = malloc(bytes);
	if (ptr == NULL)
		ERR(NO_MEMORY, "Couldn't allocate memory");
	return ptr;
}

/* convert null terminated string representing an octal number to int */
int octal_to_int(char *oct) {
	size_t len = strlen(oct);
	int out = 0;
	for (uint order = 1, i = 0; i < len; ++i, order *= 8)
		out += (oct[len - i - 1] - '0') * order;
	return out;
}

/* get number of tar block up to offset - ceiled */
ulong get_block_number(size_t offset) {
	return offset / TAR_BLOCK_SIZE + !!(offset % TAR_BLOCK_SIZE);
}

/* get total size of blocks in an entry */
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

/* test whether a 512B block contains the expected magic field */
void check_magic(tar_header_t *header) {
	if (memcmp(header->magic, TMAGIC, sizeof(TMAGIC)) && memcmp(header->magic, TOLDMAGIC, sizeof(TOLDMAGIC))) {
		WARNX("This does not look like a tar archive");
		ERRX(NOT_TAR, "Exiting with failure status due to previous errors");
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
/* the archive file pointer is presumed to be seeked at the beginning of the data which is to be extracted */
/* the function restores the original seek position after its done */
void extract_tar_entry(tar_header_t *header, args_t *args, FILE *archive) {
	long cur_pos = ftell(archive);
	FILE *f = fopen(header->name, "wb");
	if (f == NULL)
		ERR(INVALID_FILE, "Couldn't open file '%s' for writing", header->name);

	tar_block_t block;
	for (int remaining = octal_to_int(header->size); remaining > 0; remaining -= TAR_BLOCK_SIZE)
		fwrite(&block, 1, fread(&block, 1, min(remaining, TAR_BLOCK_SIZE), archive), f);

	fclose(f);
	fseek(archive, cur_pos, SEEK_SET);

	if (args->verbose)
		printf("%s\n", header->name);
}

/* ommits a warning when tar file ends with a lone zero 512B block */
/* restores the original seek position when its done */
void validate_tar_footer(FILE *archive) {
	/* a file of this size cannot have a lone zero block and still be a tar archive */
	if (get_filesize(archive) < 2 * TAR_BLOCK_SIZE)
		return;

	long cur_pos = ftell(archive);
	struct {
		tar_block_t block1;
		tar_block_t block2;
	} archive_footer;

	fseek(archive, -sizeof(archive_footer), SEEK_END);
	fread(&archive_footer, sizeof(archive_footer), 1, archive);
	if (!memcmp(&null_block, &(archive_footer.block2), TAR_BLOCK_SIZE)
	    && memcmp(&null_block, &(archive_footer.block1), TAR_BLOCK_SIZE))
		WARNX("A lone zero block at %lu", get_block_number(ftell(archive)));

	fseek(archive, cur_pos, SEEK_SET);
}

/* returns true if it is detected that there will be no more tar headers in the archive file */
bool reached_tar_end(tar_header_t *header, FILE *archive) {
	return !memcmp(&null_block, header, TAR_BLOCK_SIZE) || feof(archive);
}

/* raises an error when a file inside the archive is not of the regular type */
void check_typeflag(char flag) {
	if (flag != REGTYPE && flag != AREGTYPE)
		ERRX(UNSUPPORTED_HEADER, "Unsupported header type: %d", flag);
}

/* check whether all file arguments supplied by user were found in the archive */
/* this is done by checking the presence of a mark we set when calling file_in_args */
void check_fileargs(args_t *args) {
	bool err = false;
	for (int i = 0; i < args->file_count; ++i) {
		if ((args->files)[i][0] != '\0') {
			WARNX("%s: Not found in archive", (args->files)[i]);
			err = true;
		}
	}
	if (err)
		ERRX(INVALID_FILE, "Exiting with failure status due to previous errors");
}

/* checks whether the last entry of the archive is truncated */
void check_if_truncated(long cur_pos, size_t entry_size, size_t file_size) {
	if (cur_pos + entry_size > file_size) {
		WARNX("Unexpected EOF in archive");
		ERRX(TRUNCATED_ARCHIVE, "Error is not recoverable: exiting now");
	}
}

/* parses user provided arguments into provided args_t struct */
void get_args(int argc, char **argv, args_t *args) {
	memset(args, 0, sizeof(args_t));	// args must be zeroed for error checking
	args->files = get_memory(argc * sizeof(char *));

	while (--argc) {
		char *arg = *++argv;
		if (arg[0] == '-') {
			switch (arg[1]) {
			case 'x':
			case 't':
				if (args->operation)
					ERRX(MULTIPLE_ACTIONS, "A single action (-t or -x) must be specified");
				args->operation = (arg[1] == 'x' ? extract_tar_entry : list_tar_entry);
				break;
			case 'f':
				args->archive_file = *++argv;
				--argc;
				break;
			case 'v':
				args->verbose = true;
				break;
			default:
				ERRX(INVALID_OPTION, "No such option '%s'", arg);
			}
		} else {
			(args->files)[args->file_count] = arg;
			++(args->file_count);
		}
	}

	if (args->operation == NULL)
		ERRX(NO_ACTION, "Expected -x or -t but neither was given");
	if (args->archive_file == NULL)
		ERRX(MISSING_ARGUMENT, "Expected -f");
}

/* iterate over header blocks and perform on them the operation specified by user */
/* always leaves the file pointer at the first byte of the file data when calling operation */
/* operation must end with the file pointer at the same position as it was given */
void iterate_tar(args_t *args, FILE *archive) {
	tar_header_t header;
	int entry_size;
	while (fread(&header, TAR_BLOCK_SIZE, 1, archive) && !reached_tar_end(&header, archive)) {
		if (!args->file_count || file_in_args(header.name, args)) {
			check_magic(&header);
			check_typeflag(header.typeflag);
			(*(args->operation))(&header, args, archive);
		}
		entry_size = get_entry_size(&header);
		fseek(archive, entry_size, SEEK_CUR);	// jump to the next header
	}
	check_if_truncated(ftell(archive) - entry_size, entry_size, get_filesize(archive));
}

int main(int argc, char **argv) {
	args_t args;
	get_args(argc, argv, &args);

	FILE *archive = fopen(args.archive_file, "rb");
	if (archive == NULL) {
		WARN("%s: Cannot open", args.archive_file);
		ERRX(INVALID_FILE, "Error is not recoverable: exiting now");
	}

	iterate_tar(&args, archive);
	validate_tar_footer(archive);
	check_fileargs(&args);

	free(args.files);
	fclose(archive);

	return 0;
}
