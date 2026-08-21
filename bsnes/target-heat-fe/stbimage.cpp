#include "shader.hpp"

// CRT-Royale's mask LUTs are PNGs; SDL already vendors a decoder, so reuse it
#if __has_include(<stb_image.h>)
  #define STB_IMAGE_IMPLEMENTATION
  #define STB_IMAGE_STATIC
  #define STBI_ONLY_PNG
  #define STBI_NO_STDIO
  #define STBI_NO_LINEAR
  #define STBI_NO_HDR
  #define STBI_NO_THREAD_LOCALS
  #define STBI_FAILURE_USERMSG
  #define STBI_ASSERT(x) SDL_assert(x)
  #include <stb_image.h>
  #define HAVE_STB_IMAGE
#endif

uint8_t* decodeImage(const void* data, size_t size, int& width, int& height) {
#ifdef HAVE_STB_IMAGE
  int channels = 0;
  return stbi_load_from_memory((const stbi_uc*)data, (int)size, &width, &height, &channels, 4);
#else
  (void)data; (void)size;
  width = height = 0;
  return nullptr;
#endif
}

void freeImage(void* pixels) {
#ifdef HAVE_STB_IMAGE
  stbi_image_free(pixels);
#else
  (void)pixels;
#endif
}
