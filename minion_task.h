#ifndef _MINION_TASK_H
#define _MINION_TASK_H

#include "util.h"

RICH_ENUM(MinionTask,
  SLEEP,
  GRAVE,
  TRAIN,
  WORKSHOP,
  STUDY,
  LABORATORY,
  PRISON,
  TORTURE,
  //SACRIFICE,
  EXECUTE,
  WORSHIP,
  LAIR,
  EXPLORE,
  COPULATE,
  CONSUME,
  RITUAL
);

RICH_ENUM(MinionTrait,
  LEADER,
  FIGHTER,
  WORKER,
  PRISONER,
  NO_EQUIPMENT
);


#endif
