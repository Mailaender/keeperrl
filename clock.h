#ifndef _CLOCK_H
#define _CLOCK_H

#include "singleton.h"

class Clock : public Singleton1<Clock> {
  public:
  int getMillis();
  void setMillis(int time);
  void pause();
  void cont();
  bool isPaused();
  int getRealMillis();

  private:
  int pausedTime = 0;
  int lastPause = -1;
};

class Intervalometer {
  public:
  Intervalometer(int frequencyMillis);
  int getCount();

  private:
  int frequency;
  int lastUpdate = 0;
};

#endif
