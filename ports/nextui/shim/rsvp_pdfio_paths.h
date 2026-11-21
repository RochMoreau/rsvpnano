#pragma once
// PdfConverter hands pdfio a VFS path under the firmware mount point ("/sdcard/..."). On Linux hosts
// that prefix is remapped onto the data root before pdfio opens the file.
#include <pdfio.h>

const char* rsvp_host_vfs_path(const char* path);

static inline pdfio_file_t* rsvp_pdfioFileOpen(const char* filename, pdfio_password_cb_t password_cb,
                                              void* password_data, pdfio_error_cb_t error_cb, void* error_data) {
    return pdfioFileOpen(rsvp_host_vfs_path(filename), password_cb, password_data, error_cb, error_data);
}
#define pdfioFileOpen rsvp_pdfioFileOpen
