#include "utest.h"

#include <image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

UTEST(viamd_image, a_written_png_decodes_to_the_same_pixels) {
    const int W = 3, H = 2;
    const uint8_t src[W * H * 4] = {
        255, 0, 0, 255,   0, 255, 0, 128,   0, 0, 255, 0,
        10, 20, 30, 255,  40, 50, 60, 200,  70, 80, 90, 1,
    };

    char path[] = "/tmp/viamd_image_testXXXXXX";
    const int fd = mkstemp(path);
    ASSERT_TRUE(fd >= 0);
    fclose(fdopen(fd, "wb"));
    ASSERT_TRUE(image_write_png(str_from_cstr(path), src, W, H));

    FILE* f = fopen(path, "rb");
    ASSERT_TRUE(f != nullptr);
    std::vector<uint8_t> bytes;
    uint8_t buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    fclose(f);
    remove(path);

    int w = 0, h = 0;
    uint8_t* pixels = image_decode_rgba(bytes.data(), bytes.size(), &w, &h);
    ASSERT_TRUE(pixels != nullptr);
    EXPECT_EQ(W, w);
    EXPECT_EQ(H, h);
    EXPECT_EQ(0, memcmp(src, pixels, sizeof(src)));
    image_free(pixels);
}

UTEST(viamd_image, something_that_is_not_an_image_is_refused) {
    const char text[] = "this is not a picture";
    int w = 0, h = 0;
    EXPECT_TRUE(image_decode_rgba(text, sizeof(text), &w, &h) == nullptr);
}
