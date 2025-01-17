#pragma once

namespace application {

typedef void (*FileDropCallback)(int file_count, const char** paths, void* user_data);

struct Context {
    struct {
        const char* title;
        int width, height;
        bool vsync;
        bool should_close;
        void* ptr;
    } window;

    struct {
        int width;
        int height;
    } framebuffer;

    struct {
        struct {
            int major;
            int minor;
            int revision;
        } version;
    } gl_info;

    struct {
        double delta_s;
        double total_s;
    } timing;

    struct {
        FileDropCallback callback;
        void* user_data;
    } file_drop;
};

// Context
bool initialize(Context* ctx, int width, int height, const char* title);
void shutdown(Context* ctx);
void update(Context* ctx);
void render_imgui(Context* ctx);
void swap_buffers(Context* ctx);

// File Dialog
typedef unsigned int FileDialogFlag;

enum {
    FileDialogFlag_Open = 0x1,
    FileDialogFlag_Save = 0x2,
    FileDialogFlag_Dir  = 0x4
};

// Opens a file system file dialogue which prompts the user to either open or save files/directories.
// returns true if successful and false if it fails or is canceled by the user.
// path_buf is a pointer to a string buffer which the null-terminated path is written to
// path_cap is the capacity of the string buffer
// flags represents the type of dialogue to be opened, e.g. FileDialog_Save to save a file, FileDialog_Open | FileDialog_Dir to open a directory.
// filter is a null-terminated string containing comma separated extensions to be applied as a filter for the files: e.g. "jpg,png,bmp" to limit the scope of files to files with endings .jpg, .png or .bmp
bool file_dialog(char* path_buf, int path_cap, FileDialogFlag flags, const char* filter = 0);

}  // namespace application
