typedef struct TextureLevel {
    unsigned int width, height, depth;
    hwaddr vram_addr;
    void *decoded_data;
    size_t decoded_size;
} TextureLevel;

typedef struct TextureLayer {
    TextureLevel levels[16];
} TextureLayer;

typedef struct TextureLayout {
    TextureLayer layers[6];
} TextureLayout;

static __attribute__((noinline)) void cleanup_old(TextureLayout *layout)
{
    if (!layout) {
        return;
    }

    for (size_t layer = 0; layer < ARRAY_SIZE(layout->layers); layer++) {
        for (size_t level = 0;
             level < ARRAY_SIZE(layout->layers[layer].levels); level++) {
            g_free(layout->layers[layer].levels[level].decoded_data);
        }
    }
    g_free(layout);
}

static __attribute__((noinline)) void cleanup_guarded(TextureLayout *layout)
{
    if (!layout) {
        return;
    }

    for (size_t layer = 0; layer < ARRAY_SIZE(layout->layers); layer++) {
        for (size_t level = 0;
             level < ARRAY_SIZE(layout->layers[layer].levels); level++) {
            void *payload = layout->layers[layer].levels[level].decoded_data;
            if (payload) { g_free(payload); }
        }
    }
    g_free(layout);
}

