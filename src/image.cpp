#include <image.h>

#include <core/md_log.h>
#include <core/md_os.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

uint8_t* image_decode_rgba(const void* data, size_t size, int* width, int* height) {
    int channels = 0;
    return stbi_load_from_memory((const stbi_uc*)data, (int)size, width, height, &channels, 4);
}

void image_free(void* pixels) {
    stbi_image_free(pixels);
}

static void write_func(void* context, void* data, int size) {
    ASSERT(context);
    md_file_t* file = (md_file_t*)context;
    md_file_write(*file, data, size);
}

static inline md_file_t open_file(str_t filename) {
    md_file_t file = {0};
    if (!md_file_open(&file, filename, MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE)) {
        MD_LOG_ERROR("Failed to open file");
    }
    return file;
}

bool image_write_jpg(str_t filename, const void* rgba, int width, int height, int quality) {
    md_file_t file = open_file(filename);
    bool result = false;
    if (md_file_valid(file)) {
        result = stbi_write_jpg_to_func(write_func, &file, width, height, 4, rgba, quality) != 0;
        md_file_close(&file);
    }
    return result;
}

bool image_write_png(str_t filename, const void* rgba, int width, int height) {
    md_file_t file = open_file(filename);
    bool result = false;
    if (md_file_valid(file)) {
        result = stbi_write_png_to_func(write_func, &file, width, height, 4, rgba, width * sizeof(uint32_t)) != 0;
        md_file_close(&file);
    }
    return result;
}

bool image_write_bmp(str_t filename, const void* rgba, int width, int height) {
    md_file_t file = open_file(filename);
    bool result = false;
    if (md_file_valid(file)) {
        result = stbi_write_bmp_to_func(write_func, &file, width, height, 4, rgba) != 0;
        md_file_close(&file);
    }
    return result;
}
