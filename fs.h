#pragma once

void fs_init();
int fs_read_file(const char *name, char *buf, int offset);
int fs_get_file_name(int index, char *buf, int buf_len);
int fs_get_file_size(int index);