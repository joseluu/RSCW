#ifndef SIM_ILI9341_T3N_H
#define SIM_ILI9341_T3N_H
#include "Arduino.h"
#define ILI9341_BLACK 0
#define ILI9341_WHITE 1
#define ILI9341_ORANGE 2
#define ILI9341_YELLOW 3
#define ILI9341_GREEN 4
#define ILI9341_CYAN 5
#define ILI9341_MAGENTA 6
#define ILI9341_RED 7
namespace sim { extern std::string tftText; extern long tftPixels; }
struct ILI9341_t3n {
   ILI9341_t3n(int, int, int, int, int) {}
   void begin() {} void setRotation(int) {} void fillScreen(int) {}
   void setCursor(int, int) {} void setTextColor(int) {} void setTextColor(int, int) {} void setFont(int) {}
   void fillRect(int, int, int, int, int) {}
   void drawPixel(int, int, int) { sim::tftPixels++; }
   void drawFastVLine(int, int, int, int) {}
   void print(const char *) {}
   void print(char c) { sim::tftText += c; }
   void print(double, int) {}
};
#endif
