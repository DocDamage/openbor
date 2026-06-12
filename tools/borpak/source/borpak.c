/*
	Copyright 2006 Luigi Auriemma
	Copyright 2009-2011 Bryan Cain

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

	http://www.gnu.org/licenses/gpl.txt
*/

#ifndef WIN32
	#ifndef _FILE_OFFSET_BITS
		#define _FILE_OFFSET_BITS 64
	#endif
	#ifndef _LARGEFILE_SOURCE
		#define _LARGEFILE_SOURCE 1
	#endif
	#ifndef _GNU_SOURCE
		#define _GNU_SOURCE 1
	#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <inttypes.h>

#ifdef WIN32
	#include <direct.h>
	#include <windows.h>
	#include "stristr.h"
	#include "scandir.h"

	#define MKDIR(x)    mkdir(x)
	#define PATHSLASH   '\\'
#else
	#include <unistd.h>

	#define stristr		strcasestr
	#define MKDIR(x)    mkdir(x, 0755)
	#define PATHSLASH   '/'
#endif

#define FNAMEMAX        1024
#define PAK_UINT_MAX 	UINT32_MAX

#define PACK_MAGIC      "PACK"
#define PACK_MAGIC_LEN  4
#define PACK_HEADER_SIZE 8
#define PACK_ENTRY_BASE_SIZE 12

#define PAK64_MAGIC      "PAK64"
#define PAK64_MAGIC_LEN  5
#define PAK64_HEADER_SIZE 9
#define PAK64_ENTRY_BASE_SIZE 20

typedef uint32_t pak_u32;
typedef uint64_t pak_offset_t;
typedef uint64_t pak_size_t;

typedef enum {
	PAK_FORMAT_PACK,
	PAK_FORMAT_PAK64
} pak_format_t;

void file_write(const void *buffer, size_t size, FILE *file_object);
int pack_file(FILE *pak_file_object, char *file_path, pak_format_t format);
void extract_file(FILE *pak_file_object, char *file_path, pak_offset_t data_offset, pak_size_t data_size);
pak_u32 read_le_uint(FILE *pak_file_object, int bits);
uint64_t read_le_u64(FILE *pak_file_object);
void write_le_uint(FILE *pak_file_object, pak_u32 num, int bits);
void write_le_u64(FILE *pak_file_object, uint64_t value);
int pack_directory(FILE *pak_file_object, char *directory_path, pak_format_t format);
void write_err(void);
void std_err(void);
pak_offset_t pak_tell(FILE *pak_file_object);
void pak_seek_relative(FILE *pak_file_object, int64_t offset, int origin);
void pak_seek_to_offset(FILE *pak_file_object, pak_offset_t offset);
int is_unsafe_path(const char *name);
const char *format_name(pak_format_t format);

#ifdef BORPAK_WINPAUSE
	void winpause(void);
#endif

typedef struct {
	pak_u32       record_size;
	pak_offset_t data_offset;
	pak_size_t   data_size;
	char         *name;
} pak_entry_t;

struct pak_entry_node_t {
	pak_entry_t     		entry;
	struct pak_entry_node_t	*next;
};

struct  pak_entry_node_t   *pak_entries = NULL;

const char *format_name(pak_format_t format) {
	return format == PAK_FORMAT_PAK64 ? "PAK64" : "PACK";
}

/*
* Caskey, Damon V.
* 2026-06-10
*
* Writes a fixed-size block to a file.
* Exits through write_err() if the write fails.
*/
void file_write(const void *buffer, size_t size, FILE *file_object) {
	if(fwrite(buffer, size, 1, file_object) != 1) {
		write_err();
	}
}

/*
* Caskey, Damon V.
* 2026-06-10
*
* Returns the current file position as a 64-bit archive offset.
*/
pak_offset_t pak_tell(FILE *pak_file_object) {
#ifdef WIN32
	__int64 pos;

	pos = _ftelli64(pak_file_object);
	if(pos < 0) {
		std_err();
	}

	return (pak_offset_t)pos;
#else
	off_t pos;

	pos = ftello(pak_file_object);
	if(pos < 0) {
		std_err();
	}

	return (pak_offset_t)pos;
#endif
}

/*
* Seeks using the platform file API.
*
* The offset is signed because SEEK_END needs 
* negative movement when reading archive footers, 
* such as the final 4-byte PACK footer or 8-byte 
* PAK64 footer.
*/
void pak_seek_relative(FILE *pak_file_object, int64_t offset, int origin) {
#ifdef WIN32
	if(_fseeki64(pak_file_object, offset, origin) != 0) {
		std_err();
	}
#else
	if(fseeko(pak_file_object, (off_t)offset, origin) != 0) {
		std_err();
	}
#endif
}

/*
* Seeks to an absolute archive offset.
*
* Archive offsets are stored as unsigned values, 
* but the platform seek APIs use signed offsets. 
* Reject values that cannot be represented safely.
*/
void pak_seek_to_offset(FILE *pak_file_object, pak_offset_t offset) {
	if(offset > (pak_offset_t)INT64_MAX) {
		printf("\nError: archive offset is too large for this platform.\n");
		exit(1);
	}

	pak_seek_relative(pak_file_object, (int64_t)offset, SEEK_SET);
}

/*
* Caskey, Damon V.
* 2026-06-10
*
* Rejects archive paths that could escape the 
* extraction directory.
*
* PAK entries should always be relative paths. 
* Absolute paths, Windows drive-qualified paths, 
* and any ".." path component are considered unsafe.
*/
int is_unsafe_path(const char *name) {
	const char *path_cursor;
	const char *component_start;
	size_t component_length;

	/*
	* Empty or missing names are invalid archive entries.
	*/
	if(!name || !name[0]) {
		return 1;
	}

	/*
	* Absolute Unix or Windows-style paths would write outside the
	* extraction directory.
	*/
	if(name[0] == '/' || name[0] == '\\') {
		return 1;
	}

#ifdef WIN32
	/*
	* Reject Windows drive-qualified paths such as "C:\foo".
	*/
	if(strchr(name, ':')) {
		return 1;
	}
#endif

	path_cursor = name;

	/*
	* Walk each path component and reject ".." anywhere in the path.
	* This catches:
	*
	*   ..
	*   ../file
	*   folder/..
	*   folder/../file
	*   folder\..\file
	*/
	while(*path_cursor) {
		component_start = path_cursor;

		while(*path_cursor && *path_cursor != '/' && *path_cursor != '\\') {
			path_cursor++;
		}

		component_length = (size_t)(path_cursor - component_start);

		if(component_length == 2 &&
			component_start[0] == '.' &&
			component_start[1] == '.') {
			return 1;
		}

		/*
		* Skip the path separator and continue with the next component.
		*/
		if(*path_cursor) {
			path_cursor++;
		}
	}

	return 0;
}

int main(int argc, char *argv[]) {

	struct pak_entry_node_t *entry_node;
	struct pak_entry_node_t *entry_node_free;
	
	pak_entry_t    entry;
	FILE    *pak_file_object;
	pak_offset_t table_offset;
	pak_offset_t table_end_offset;
	pak_offset_t table_start_offset;
	pak_offset_t remaining_table_bytes;
	pak_offset_t header_size;
	pak_u32 entry_base_size;
	int     i;
	size_t	name_length;
	int		list    = 0;
	int		build   = 0;
	int		pak64   = 0;
	pak_u32	packver = 0;
	size_t	file_count   = 0;
	int 	fgetc_result = 0;
	pak_format_t format = PAK_FORMAT_PACK;

	unsigned char magic[PAK64_MAGIC_LEN];
	char	curdir[512];
	char	*input_dir    = NULL;
	char	*pattern      = NULL;
	char	*pak_filename;

	setbuf(stdout, NULL);

#ifdef BORPAK_WINPAUSE
	atexit(winpause);
#endif

	fputs("\n"
		"BOR PAK extractor/builder v0.1\n"
		"originally by Luigi Auriemma\n"
		"e-mail: aluigi@autistici.org\n"
		"web:    aluigi.org\n"
		"\n"
		"Updated to v0.2 by SX\n"
		"e-mail: sumolx@gmail.com\n"
		"web:    https://www.chronocrash.com\n"
		"\n"
		"Updated to v0.3 by Plombo\n"
		"web:    https://www.chronocrash.com\n"
		"\n"
		"Updated to v0.4 by Damon Caskey (dcurrent)\n"
		"web:    https://www.chronocrash.com\n"
		"\n", stdout);

	if(argc < 2) {
		printf("\n"
			"Usage: %s [options] <file.PAK>\n"
			"\n"
			"-d DIR   files folder, default is the current\n"
			"-b       build a PAK from the files folder, default is extraction\n"
			"-6       build a PAK64 archive with 64-bit offsets and sizes\n"
			"--pak64  same as -6\n"
			"-l       list files without extracting\n"
			"-p PAT   extract only the files which contain PAT in their name\n"
			"\n", argv[0]);
		exit(1);
	}

	argc--;
	for(i = 1; i < argc; i++) {
		
		if(argv[i][0] != '-') {
			 break;
		}
		
		if(!strcmp(argv[i], "--pak64")) {
			pak64 = 1;
			continue;
		}

		switch(argv[i][1]) {
			case 'd': 
			
				if(i + 1 >= argc) {
					printf("\nError: missing argument for -d option\n\n");
					exit(1);
				}
			
				input_dir = argv[++i];    break;
			
			case 'b': 
			
				build = 1;            break;
			case '6':

				pak64 = 1;            break;
			case 'l': 
			
				list  = 1;            break;
			case 'p': 
			
				if(i + 1 >= argc) {
					printf("\nError: missing argument for -p option\n\n");
					exit(1);
				}

				pattern  = argv[++i];    break;
			default: {
				printf("\nError: wrong command-line argument (%s)\n\n", argv[i]);
				exit(1);
				} break;
		}
	}
	
	if(i != argc) {
		printf("\nError: expected one archive filename after options\n\n");
		exit(1);
	}

	pak_filename = argv[argc];

	if(!pak_filename || pak_filename[0] == '-') {
		printf("\nError: missing archive filename\n\n");
		exit(1);
	}

	if(build) {
		format = pak64 ? PAK_FORMAT_PAK64 : PAK_FORMAT_PACK;

		if(!input_dir) {
			printf("\n"
				"Error: please specify the files directory with the -d option and don't specify\n"
				"       the current\n\n");
			exit(1);
		}

		printf("- create file: %s\n", pak_filename);
		printf("- format: %s\n", format_name(format));
		printf("- directory: %s\n\n", input_dir);
		pak_file_object = fopen(pak_filename, "rb");
		if(pak_file_object) {
			fclose(pak_file_object);
			printf("- a file with the same name already exists, overwrite? (y/N)\n  ");
			
			fgetc_result = fgetc(stdin);
			if(fgetc_result == EOF || tolower((unsigned char)fgetc_result) != 'y') {
				exit(1);
			}
		}
		pak_file_object = fopen(pak_filename, "wb");

		if(!pak_file_object){
			std_err();
		}

		if(format == PAK_FORMAT_PAK64) {
			file_write(PAK64_MAGIC, PAK64_MAGIC_LEN, pak_file_object);
		} else {
			file_write(PACK_MAGIC, PACK_MAGIC_LEN, pak_file_object);
		}
		write_le_uint(pak_file_object, packver, 32);

		/* 
		* Print header based on format. 
		*/
		if(format == PAK_FORMAT_PAK64) {
			printf(
				"            offset                 size   filename\n"
				"-------------------------------------------------\n");
		} else {
			printf(
				"    offset       size   filename\n"
				"--------------------------------\n");
		}

		if(pack_directory(pak_file_object, input_dir, format) < 0) {
			printf("\nError: an error occurred during the directory scanning\n");
			goto quit;
		}

		table_offset = pak_tell(pak_file_object);
		if(format == PAK_FORMAT_PACK && table_offset > PAK_UINT_MAX) {
			printf("\nError: archive exceeds PACK 32-bit offset limit.\n");
			printf("Use -6 or --pak64 to create a PAK64 archive.\n");
			exit(1);
		}

		if(format == PAK_FORMAT_PAK64) {
			printf("- files info offset: %016" PRIx64 "\n", (uint64_t)table_offset);
		} else {
			printf("- files info offset: %08x\n", (unsigned int)table_offset);
		}

		entry_node = pak_entries;
		while(entry_node) {
			if(format == PAK_FORMAT_PAK64) {
				write_le_uint(pak_file_object, entry_node->entry.record_size,  32);
				write_le_u64(pak_file_object, entry_node->entry.data_offset);
				write_le_u64(pak_file_object, entry_node->entry.data_size);
				file_write(entry_node->entry.name, entry_node->entry.record_size - PAK64_ENTRY_BASE_SIZE, pak_file_object);
			} else {
				write_le_uint(pak_file_object, entry_node->entry.record_size,  32);
				write_le_uint(pak_file_object, (pak_u32)entry_node->entry.data_offset,  32);
				write_le_uint(pak_file_object, (pak_u32)entry_node->entry.data_size, 32);
				file_write(entry_node->entry.name, entry_node->entry.record_size - PACK_ENTRY_BASE_SIZE, pak_file_object);
			}

			entry_node_free = entry_node;
			entry_node     = entry_node->next;
			free(entry_node_free->entry.name);
			free(entry_node_free);
			file_count++;
		}

		if(format == PAK_FORMAT_PAK64) {
			write_le_u64(pak_file_object, table_offset);
		} else {
			pak_offset_t footer_offset;

			/*
			* The legacy PACK footer is a final 32-bit table offset.
			* Make sure writing it will not push the archive beyond the
			* 32-bit PACK size boundary.
			*/
			footer_offset = pak_tell(pak_file_object);

			if(footer_offset > PAK_UINT_MAX - 4) {
				printf("\nError: archive exceeds PACK 32-bit size limit.\n");
				printf("Use -6 or --pak64 to create a PAK64 archive.\n");
				exit(1);
			}

			write_le_uint(pak_file_object, (pak_u32)table_offset, 32);
		}

	} else {

		printf("- open file: %s\n", pak_filename);
		pak_file_object = fopen(pak_filename, "rb");

		if(!pak_file_object){
			std_err();
		}

		if(input_dir) {
			printf("- change directory: %s\n", input_dir);
			
			if(chdir(input_dir) < 0) {
				std_err();
			}
		}

		if(fread(magic, 1, PAK64_MAGIC_LEN, pak_file_object) != PAK64_MAGIC_LEN) {
			goto quit;
		}

		if(!memcmp(magic, PAK64_MAGIC, PAK64_MAGIC_LEN)) {
			format = PAK_FORMAT_PAK64;
			header_size = PAK64_HEADER_SIZE;
			entry_base_size = PAK64_ENTRY_BASE_SIZE;
			packver = read_le_uint(pak_file_object, 32);
		} else if(!memcmp(magic, PACK_MAGIC, PACK_MAGIC_LEN)) {
			format = PAK_FORMAT_PACK;
			header_size = PACK_HEADER_SIZE;
			entry_base_size = PACK_ENTRY_BASE_SIZE;
			pak_seek_relative(pak_file_object, PACK_MAGIC_LEN, SEEK_SET);
			packver = read_le_uint(pak_file_object, 32);
		} else {
			printf("\nError: unsupported pack format\n");
			goto quit;
		}

		printf("- format: %s\n", format_name(format));

		if(format == PAK_FORMAT_PAK64) {
			pak_seek_relative(pak_file_object, -8, SEEK_END);
			table_end_offset = pak_tell(pak_file_object);
			table_offset = read_le_u64(pak_file_object);
		} else {
			pak_seek_relative(pak_file_object, -4, SEEK_END);
			table_end_offset = pak_tell(pak_file_object);
			table_offset = read_le_uint(pak_file_object, 32);
		}

		if(table_offset < header_size || table_offset > table_end_offset) {
			printf("\nError: malformed %s file table offset\n", format_name(format));
			goto quit;
		}

		table_start_offset = table_offset;

		pak_seek_to_offset(pak_file_object, table_offset);

		while(table_offset < table_end_offset) {
			remaining_table_bytes = table_end_offset - table_offset;

			if(remaining_table_bytes < entry_base_size) {
				printf("\nError: malformed %s file table\n", format_name(format));
				goto quit;
			}

			entry.record_size = read_le_uint(pak_file_object, 32);

			if(format == PAK_FORMAT_PAK64) {
				entry.data_offset = read_le_u64(pak_file_object);
				entry.data_size   = read_le_u64(pak_file_object);
			} else {
				entry.data_offset = read_le_uint(pak_file_object, 32);
				entry.data_size   = read_le_uint(pak_file_object, 32);
			}

			if(entry.record_size <= entry_base_size) {
				printf("\nError: malformed %s file table entry\n", format_name(format));
				goto quit;
			}

			if(entry.record_size > remaining_table_bytes) {
				printf("\nError: malformed %s file table\n", format_name(format));
				goto quit;
			}

			name_length = entry.record_size - entry_base_size;

			entry.name = malloc(name_length + 1);
			
			if(!entry.name) { 
				std_err();
			}
			

			if(fread(entry.name, name_length, 1, pak_file_object) != 1) {
				free(entry.name);
				std_err();
			}

			/*
			*  Archive names are stored as raw bytes. Add the terminator
			*  before using the name with normal C string functions.
			*/
			entry.name[name_length] = '\0';

			if(!pattern || (pattern && stristr(entry.name, pattern))) {
				printf("  %s\n", entry.name);
				if(!list) {

					/*
					* Verify safe extraction before writing any files.
					* This prevents malicious or malformed archive entries
					* from escaping the extraction directory.
					*/
					if(is_unsafe_path(entry.name)) {
						printf("  skipping %s: path traversal detected\n", entry.name);
					} else {

						if(entry.data_offset > table_start_offset ||
							entry.data_size > table_start_offset - entry.data_offset) {
							printf("\nError: malformed %s file data range: %s\n", format_name(format), entry.name);
							free(entry.name);
							goto quit;
						}

						extract_file(pak_file_object, entry.name, entry.data_offset, entry.data_size);
					}
				}
				file_count++;
			}

			free(entry.name);
			table_offset += entry.record_size;
			pak_seek_to_offset(pak_file_object, table_offset);
		}
	}

quit:
	fclose(pak_file_object);
	printf("- finished: %zu files\n", file_count);
	printf("- current directory: %s\n", getcwd(curdir, sizeof(curdir)));
	
	return 0;
}

int pack_file(FILE *pak_file_object, char *file_path, pak_format_t format) {

	struct  pak_entry_node_t   *entry_node;
	FILE    *source_file_object;
	size_t  bytes_read;
	size_t  name_length;
	pak_offset_t data_offset;
	pak_size_t	data_size;
	pak_u32 entry_base_size;

	unsigned char 	copy_buffer[8192];
	char	*path_cursor;

	// printf("  %s\r", file_path);
	source_file_object = fopen(file_path, "rb");
	if(!source_file_object){ 
		std_err();
	}

	/*
	* Skip leading "./" if present.
	*/
	if(file_path[0] == '.' && (file_path[1] == '/' || file_path[1] == '\\')) {
		file_path += 2;
	}

	for(path_cursor = file_path; *path_cursor; path_cursor++) {           // WIN mode
		if(*path_cursor == '/') {
			*path_cursor = '\\';
		} /*else {
			*path_cursor = toupper(*path_cursor);
		} */
	}

	data_offset = pak_tell(pak_file_object);
	if(format == PAK_FORMAT_PACK && data_offset > PAK_UINT_MAX) {
		printf("\nError: archive exceeds PACK 32-bit offset limit.\n");
		printf("Use -6 or --pak64 to create a PAK64 archive.\n");
		exit(1);
	}

	data_size = 0;

	while((bytes_read = fread(copy_buffer, 1, sizeof(copy_buffer), source_file_object)) != 0) {
		if(format == PAK_FORMAT_PACK && data_size > PAK_UINT_MAX - bytes_read) {
			printf("\nError: file too large for PACK: %s\n", file_path);
			printf("Use -6 or --pak64 to create a PAK64 archive.\n");
			exit(1);
		}

		file_write(copy_buffer, bytes_read, pak_file_object);
		data_size += bytes_read;
	}

	if(ferror(source_file_object)) {
		std_err();
	}

	fclose(source_file_object);

	for(entry_node = pak_entries; entry_node && entry_node->next; entry_node = entry_node->next);

	if(entry_node) {
		entry_node->next = malloc(sizeof(struct pak_entry_node_t));
		
		if(!entry_node->next) { 
			std_err();
		}
		entry_node       = entry_node->next;

	} else {
		pak_entries      = malloc(sizeof(struct pak_entry_node_t));
		
		if(!pak_entries) { 
			std_err();
		}
		entry_node       = pak_entries;
	}
	entry_node->next = NULL;

	entry_base_size = format == PAK_FORMAT_PAK64 ? PAK64_ENTRY_BASE_SIZE : PACK_ENTRY_BASE_SIZE;
	name_length = strlen(file_path) + 1;

	if(name_length > PAK_UINT_MAX - entry_base_size) {
		printf("\nError: file name too long for %s format: %s\n", format_name(format), file_path);
		exit(1);
	}

	entry_node->entry.record_size = (pak_u32)(entry_base_size + name_length);
	entry_node->entry.data_offset = data_offset;
	entry_node->entry.data_size = data_size;
	entry_node->entry.name = malloc(name_length);
	
	if(!entry_node->entry.name) { 
		std_err();
	}

	memcpy(entry_node->entry.name, file_path, name_length);

	if(format == PAK_FORMAT_PAK64) {
		printf("  %016" PRIx64 " %20" PRIu64 "   %s\n", (uint64_t)data_offset, (uint64_t)data_size, file_path);
	} else {
		printf("  %08x %10" PRIu64 "   %s\n", (unsigned int)data_offset, (uint64_t)data_size, file_path);
	}

	return(0);
}

void extract_file(FILE *pak_file_object, char *file_path, pak_offset_t data_offset, pak_size_t data_size) {
	FILE    *output_file_object;
	size_t  chunk_size;
	size_t  bytes_read;
	pak_size_t 	bytes_remaining;
	unsigned char  copy_buffer[8192];
	char	*path_cursor;

	pak_seek_to_offset(pak_file_object, data_offset);

	for(path_cursor = file_path; *path_cursor; path_cursor++) {
		if((*path_cursor == '\\') || (*path_cursor == '/')) {
			*path_cursor = 0;
			MKDIR(file_path);
			*path_cursor = PATHSLASH;
		} else {
			*path_cursor = (char)tolower((unsigned char)*path_cursor);
		}
	}

	output_file_object = fopen(file_path, "wb");
	if(!output_file_object){ 
		std_err();
	}

	bytes_remaining = data_size;

	while(bytes_remaining) {
		chunk_size = sizeof(copy_buffer);

		if(bytes_remaining < (pak_size_t)chunk_size) {
			chunk_size = (size_t)bytes_remaining;
		}

		bytes_read = fread(copy_buffer, 1, chunk_size, pak_file_object);

		if(bytes_read != chunk_size) {
			printf("\nError: unexpected end of archive while extracting %s\n", file_path);
			exit(1);
		}

		file_write(copy_buffer, bytes_read, output_file_object);
		bytes_remaining -= bytes_read;
	}

	fclose(output_file_object);
}

pak_u32 read_le_uint(FILE *pak_file_object, int bits) {
	pak_u32   num;
	int     i;
	int 	bytes;
	unsigned char  tmp[sizeof(pak_u32)];
	
	bytes = bits >> 3;
	
	if(fread(tmp, bytes, 1, pak_file_object) != 1) {
		std_err();
	}

	for(num = i = 0; i < bytes; i++) {
		num |= ((pak_u32)tmp[i] << (i << 3));
	}

	return(num);
}

uint64_t read_le_u64(FILE *pak_file_object) {
	uint64_t num;
	int i;
	unsigned char tmp[sizeof(uint64_t)];

	if(fread(tmp, sizeof(tmp), 1, pak_file_object) != 1) {
		std_err();
	}

	for(num = i = 0; i < (int)sizeof(tmp); i++) {
		num |= ((uint64_t)tmp[i] << (i << 3));
	}

	return num;
}

void write_le_uint(FILE *pak_file_object, pak_u32 num, int bits) {
	int     i;
	int bytes;
	unsigned char  tmp[sizeof(pak_u32)];
	
	bytes = bits >> 3;
	for(i = 0; i < bytes; i++) {
		tmp[i] = (num >> (i << 3)) & 0xff;
	}
	file_write(tmp, bytes, pak_file_object);
}

void write_le_u64(FILE *pak_file_object, uint64_t value) {
	int i;
	unsigned char tmp[sizeof(uint64_t)];

	for(i = 0; i < (int)sizeof(tmp); i++) {
		tmp[i] = (unsigned char)((value >> (i << 3)) & 0xff);
	}

	file_write(tmp, sizeof(tmp), pak_file_object);
}

int pack_directory(FILE *pak_file_object, char *directory_path, pak_format_t format) {
	char  child_path[FNAMEMAX];

	struct  stat    path_status;
	struct  dirent  **namelist;
	int             n,
					i;
	int written;

	n = scandir(directory_path, &namelist, NULL, alphasort);
	if(n < 0) {
		
		if(stat(directory_path, &path_status) < 0) {
			printf("**** %s", directory_path);
			std_err();
		}

		if(pack_file(pak_file_object, directory_path, format) < 0) {
			return(-1);
		}

	} else {
		for(i = 0; i < n; i++) {    // Changed by Plombo
			
			/*
			*  scandir() allocates every returned entry, including "." and "..".
			*  Free skipped entries before continuing.
			*/
			if(!strcmp(namelist[i]->d_name, ".") || !strcmp(namelist[i]->d_name, "..")) {
				free(namelist[i]);
				continue;
			}

			/*
			*  Construct the full path for the current entry.
			*  Check for path length overflow.
			*/
			

			written = snprintf(child_path, sizeof(child_path), "%s/%s", directory_path, namelist[i]->d_name);
			if(written < 0 || (size_t)written >= sizeof(child_path)) {
				printf("\nError: path too long: %s/%s\n", directory_path, namelist[i]->d_name);
				goto quit;
			}

			if(stat(child_path, &path_status) < 0) {
				printf("**** %s", child_path);
				std_err();
			}
			if(S_ISDIR(path_status.st_mode)) {
				if(pack_directory(pak_file_object, child_path, format) < 0) {
					goto quit;
				}
			} else {
				if(pack_file(pak_file_object, child_path, format) < 0) {
					goto quit;
				}
			}
			free(namelist[i]);
		}
		free(namelist);
	}

	return(0);
quit:
	for(; i < n; i++) {
		free(namelist[i]);
	}
	free(namelist);
	return(-1);
}

void write_err(void) {
	printf("\nError: write error; probably out of disk space\n\n");
	exit(1);
}

void std_err(void) {
	perror("\nError");
	exit(1);
}

#ifdef BORPAK_WINPAUSE
void winpause(void) {
	system("pause");
}
#endif
