#ifndef SIM_ENCODERTOOL_H
#define SIM_ENCODERTOOL_H
namespace sim { extern int encoderValue; }
namespace EncoderTool {
struct Encoder {
   int last = 0;
   void begin(int, int) {}
   bool valueChanged() { return sim::encoderValue != last; }
   int getValue() { last = sim::encoderValue; return last; }
};
}
#endif
