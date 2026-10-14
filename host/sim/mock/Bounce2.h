#ifndef SIM_BOUNCE2_H
#define SIM_BOUNCE2_H
namespace sim { extern int buttonPresses; }
namespace Bounce2 {
struct Button {
   bool p = false;
   void attach(int, int) {}
   void interval(int) {}
   void setPressedState(int) {}
   void update() { p = sim::buttonPresses > 0; if (p) sim::buttonPresses--; }
   bool pressed() { return p; }
};
}
#endif
