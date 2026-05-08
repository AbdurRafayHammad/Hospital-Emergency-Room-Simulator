/* File: include/utils.h */

#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

#include "common.h"

void print_banner(void);
void print_phase_status(void);
const char *bed_type_to_text(BedType bed_type);
int bed_type_from_text(const char *text, BedType *out_type);
int make_timestamp(char *buffer, size_t buffer_size);
int append_log_line(const char *path, const char *tag, const char *message);

#endif
