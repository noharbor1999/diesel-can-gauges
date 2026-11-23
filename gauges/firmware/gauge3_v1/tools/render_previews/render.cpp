// Host-side preview renderer: runs the SAME display/ui.cpp + gfx.cpp drawing code
// that runs on the ESP32-S3 and writes 480x480 PPM frames (converted to PNG by make_previews.sh).
#include "../../display/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  static uint16_t fb[480 * 480];
  Canvas cv{fb, 480, 480};
  for (int i = 0; i < ui_demo_count(); i++) {
    G3Packet p; UiLocal L; ui_demo_build(i, p, L);
    ui_render(cv, p, L);
    char path[512]; snprintf(path, sizeof path, "%s/%02d_%s.ppm", dir, i + 1, ui_demo_name(i));
    FILE* f = fopen(path, "wb"); fprintf(f, "P6 480 480 255\n");
    for (int k = 0; k < 480 * 480; k++) {
      uint16_t c = fb[k]; unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)};
      fwrite(rgb, 1, 3, f);
    }
    fclose(f); printf("%s\n", path);
  }
  return 0;
}
