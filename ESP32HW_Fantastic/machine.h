#include "Z80.h"

/*
 * Z80 Hardware Machine Class
 */

class Machine {

public:

  Machine();

  ~Machine();

  void load(int address, uint8_t const * data, int length);

  void attachRAM(int RAMSize);

  void dumpRegs();

  void run(int address);

  static int readByte(void * context, int address);
  static void writeByte(void * context, int address, int value);

  static int readWord(void * context, int addr)              { return readByte(context, addr) | (readByte(context, addr + 1) << 8); }
  static void writeWord(void * context, int addr, int value) { writeByte(context, addr, value & 0xFF); writeByte(context, addr + 1, value >> 8); }

  static int readIO(void * context, int address);
  static void writeIO(void * context, int address, int value);

  void setRealSpeed(bool value) { m_realSpeed = value; }

  void irq_enable_w(void * context, int value);
  
  bool realSpeed() { return m_realSpeed; }

  void draw();

  Z80   m_Z80;
  uint8_t *    m_RAM;

private:

  int nextStep();
  bool         m_realSpeed;
};
