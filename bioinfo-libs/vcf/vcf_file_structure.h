#ifndef _VCF_FILE_STRUCTURE_H
#define _VCF_FILE_STRUCTURE_H

#include <sys/types.h>

#include "list.h"

/**
 * Entry in the VCF document header.
 */
typedef struct vcf_header_entry {
	char *name;
	// List of keys of the fields describing the entry (if num_field_keys = 0, then entry = "name=values[0]")
	list_t *keys;
	size_t num_keys;
	// List of values of the fields describing the entry
	list_t *values;
	size_t num_values;
} vcf_header_entry_t;


/**
 * Entry in the VCF document body.
 */
typedef struct vcf_record {
	char* chromosome;
	unsigned long position;
	char* id;
	char* reference;
	char* alternate;
	float quality;
	char* filter;
	char* info;
	char* format;
	
	list_t *samples;
} vcf_record_t;

/**
 * VCF file structure. The physical file is defined by its file descriptor, its 
 * filename and the mode it has been open.
 *
 * It contains a header with several entries, and a body with several records.
 */
typedef struct vcf_file {
	char* filename;
	char* mode;
	char *data;
	size_t data_len;
	
	char* format;
	list_t *header_entries;
	size_t num_header_entries;
	list_t *samples_names;
	size_t num_samples;
	list_t *records;
	size_t num_records;
} vcf_file_t;


#endif
