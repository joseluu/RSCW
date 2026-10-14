/* Bouchon Arduino minimal pour simuler RSCW_Teensy.ino sur PC (host/sim). */
#ifndef SIM_ARDUINO_H
#define SIM_ARDUINO_H
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <cstdarg>
#include <string>
#include <algorithm>
using std::min; using std::max;

#define ARDUINO 10819
typedef bool boolean;
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#ifndef PI
#define PI 3.14159265358979323846
#endif
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

namespace sim {
extern double nowUs;                 /* horloge simulée */
extern int pins[64];
extern std::string serialIn, serialOut;
extern bool echo;
}
inline unsigned long millis() { return (unsigned long)(sim::nowUs / 1000); }
inline unsigned long micros() { return (unsigned long)sim::nowUs; }
inline void delay(unsigned long ms) { sim::nowUs += ms * 1000.0; }
inline int digitalRead(int p) { return sim::pins[p]; }
inline int digitalReadFast(int p) { return sim::pins[p]; }
inline void digitalWrite(int p, int v) { if (p != 14 && p != 15) sim::pins[p] = v; }
inline void pinMode(int, int) {}

class SimSerial {
public:
   void begin(long) {}
   int available() { return (int)sim::serialIn.size(); }
   int read() { if (sim::serialIn.empty()) return -1; int c = (unsigned char)sim::serialIn[0]; sim::serialIn.erase(0, 1); return c; }
   void out(const char *s) { sim::serialOut += s; if (sim::echo) fputs(s, stdout); }
   void print(const char *s) { out(s); }
   void print(char c) { char b[2] = {c, 0}; out(b); }
   void print(int v) { char b[32]; snprintf(b, sizeof b, "%d", v); out(b); }
   void print(unsigned long v) { char b[32]; snprintf(b, sizeof b, "%lu", v); out(b); }
   void print(double v, int d = 2) { char b[64]; snprintf(b, sizeof b, "%.*f", d, v); out(b); }
   void println() { out("\r\n"); }
   template <typename T> void println(T v) { print(v); println(); }
   void println(double v, int d) { print(v, d); println(); }
   void printf(const char *fmt, ...) { char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); out(b); }
};
extern SimSerial Serial;
#endif
