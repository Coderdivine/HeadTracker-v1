#pragma once

#define STORAGE_PAGES 4

int socWriteFlash(const char *data, int len);
void socClearFlash();

const char *get_flashSpace();
