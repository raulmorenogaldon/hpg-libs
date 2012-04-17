#include "vcf_file.h"


//====================================================================================
//  vcf_file.c
//  vcf file management functions
//====================================================================================

//-----------------------------------------------------
// vcf_open
//-----------------------------------------------------

void *mmap_file(size_t *len, const char *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0) 
	{
		fprintf(stderr, "Error opening file: %s\n", filename);
		exit(1);
	}
	
	struct stat st[1];
	if (fstat(fd, st))
	{
		fprintf(stderr, "Error while getting file information: %s\n", filename);
		exit(1);	
	}
	*len = (size_t) st->st_size;

	if (!*len) {
		close(fd);
		return NULL;
	}

	void *map = mmap(NULL, *len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (MAP_FAILED == map)
	{
		fprintf(stderr, "mmap failed for %s\n", filename);
		exit(1);
	}
	close(fd);
	
	return map;
}


vcf_file_t *vcf_open(char *filename) 
{
	size_t len;
	char *data = mmap_file(&len, filename);

	vcf_file_t *vcf_file = (vcf_file_t *) malloc(sizeof(vcf_file_t));

	vcf_file->filename = filename;
	vcf_file->data = data;
	vcf_file->data_len = len;

	// Initialize header
	vcf_file->header_entries = (list_t*) malloc (sizeof(list_t));
	list_init("headers", 1, INT_MAX, vcf_file->header_entries);
	vcf_file->num_header_entries = 0;
	
	// Initialize samples names list
	vcf_file->samples_names = (list_t*) malloc (sizeof(list_t));
	list_init("samples", 1, INT_MAX, vcf_file->samples_names);
	vcf_file->num_samples = 0;
	
	// Initialize records
	vcf_file->records = (list_t*) malloc (sizeof(list_t));
	list_init("records", 1, INT_MAX, vcf_file->records);
	vcf_file->num_records = 0;
	
	return vcf_file;
}


//-----------------------------------------------------
// vcf_close and memory freeing
//-----------------------------------------------------

void vcf_close(vcf_file_t *vcf_file) 
{
	// Free file format
	free(vcf_file->format);
	// Free samples names
	list_item_t* item = NULL;
	while ( (item = list_remove_item_async(vcf_file->samples_names)) != NULL ) 
	{
		free(item->data_p);
		list_item_free(item);
	}
	// Free header entries
	item = NULL;
	while ( (item = list_remove_item_async(vcf_file->header_entries)) != NULL ) 
	{
		vcf_header_entry_free(item->data_p);
		list_item_free(item);
	}
	free(vcf_file->header_entries);
	// Free samples names
	free(vcf_file->samples_names);
	
	// TODO Free records list? they are freed via batches
// 	item = NULL;
// 	while ( (item = list_remove_item_async(vcf_file->records)) != NULL ) 
// 	{
// 		vcf_record_free(item->data_p);
// 		list_item_free(item);
// 	}
	free(vcf_file->records);

	munmap((void*) vcf_file->data, vcf_file->data_len);
	free(vcf_file);
}

void vcf_header_entry_free(vcf_header_entry_t *vcf_header_entry)
{
	// Free entry name
	free(vcf_header_entry->name);
	// Free list of keys
	list_item_t* item = NULL;
	while ( (item = list_remove_item_async(vcf_header_entry->keys)) != NULL ) 
	{
		free(item->data_p);
		list_item_free(item);
	}
	free(vcf_header_entry->keys);
	// Free list of values
	item = NULL;
	while ( (item = list_remove_item_async(vcf_header_entry->values)) != NULL ) 
	{
		free(item->data_p);
		list_item_free(item);
	}
	free(vcf_header_entry->values);
	
	free(vcf_header_entry);
}

void vcf_record_free(vcf_record_t *vcf_record)
{
	free(vcf_record->chromosome);
	free(vcf_record->id);
	free(vcf_record->reference);
	free(vcf_record->alternate);
	free(vcf_record->filter);
	free(vcf_record->info);
	free(vcf_record->format);
	
	// Free list of samples
	list_item_t *item = NULL;
	while ( (item = list_remove_item_async(vcf_record->samples)) != NULL ) 
	{
		free(item->data_p);
		list_item_free(item);
	}
	free(vcf_record->samples);
	
	free(vcf_record);
}


//-----------------------------------------------------
// I/O operations (read and write) in various ways
//-----------------------------------------------------

int vcf_read(vcf_file_t *vcf_file) 
{
	return vcf_ragel_read(NULL, 1, vcf_file, 0);
}

int vcf_read_batches(list_t *batches_list, size_t batch_size, vcf_file_t *vcf_file, int read_samples)
{
	return vcf_ragel_read(batches_list, batch_size, vcf_file, read_samples);
}

int vcf_write(vcf_file_t *vcf_file, char *filename)
{
	FILE *fd = fopen(filename, "w");
	if (fd < 0) 
	{
		fprintf(stderr, "Error opening file: %s\n", filename);
		exit(1);
	}
	
	if (vcf_write_to_file(vcf_file, fd))
	{
		fprintf(stderr, "Error writing file: %s\n", filename);
		fclose(fd);
		exit(1);
	}
	
	fclose(fd);
	return 0;
}

//-----------------------------------------------------
// load data into the vcf_file_t
//-----------------------------------------------------

int add_header_entry(vcf_header_entry_t *header_entry, vcf_file_t *vcf_file)
{
	list_item_t *item = list_item_new(vcf_file->num_header_entries, 1, header_entry);
	int result = list_insert_item(item, vcf_file->header_entries);
	if (result) {
		vcf_file->num_header_entries++;
		dprintf("header entry %zu\n", vcf_file->num_header_entries);
	} else {
		dprintf("header entry %zu not inserted\n", vcf_file->num_header_entries);
	}
	return result;
}

int add_sample_name(char *name, vcf_file_t *vcf_file)
{
	list_item_t *item = list_item_new(vcf_file->num_samples, 1, name);
	int result = list_insert_item(item, vcf_file->samples_names);
	if (result) {
		(vcf_file->num_samples)++;
		dprintf("sample %zu is %s\n", vcf_file->num_samples, name);
	} else {
		dprintf("sample %zu not inserted\n", vcf_file->num_samples);
	}
	return result;
}

int add_record(vcf_record_t* record, vcf_file_t *vcf_file)
{
	list_item_t *item = list_item_new(vcf_file->num_records, 1, record);
	int result = list_insert_item(item, vcf_file->records);
	if (result) {
		vcf_file->num_records++;
		dprintf("record %zu\n", vcf_file->num_records);
	} else {
		dprintf("record %zu not inserted\n", vcf_file->num_records);
	}
	return result;
}

