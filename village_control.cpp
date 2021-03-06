/* Copyright (C) 2013-2014 Michal Brzozowski (rusolis@poczta.fm)

   This file is part of KeeperRL.

   KeeperRL is free software; you can redistribute it and/or modify it under the terms of the
   GNU General Public License as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   KeeperRL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
   even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along with this program.
   If not, see http://www.gnu.org/licenses/ . */

#include "stdafx.h"

#include "village_control.h"
#include "square.h"
#include "level.h"
#include "name_generator.h"
#include "model.h"
#include "creature.h"
#include "collective.h"
#include "pantheon.h"

template <class Archive>
void VillageControl::serialize(Archive& ar, const unsigned int version) {
  ar& SUBCLASS(Task::Callback) & SUBCLASS(CollectiveControl)
    & SVAR(villain)
    & SVAR(location)
    & SVAR(name)
    & SVAR(conquered);
  CHECK_SERIAL;
}

SERIALIZABLE(VillageControl);


VillageControl::VillageControl(Collective* col, Collective* c, const Location* loc)
    : CollectiveControl(col), villain(c), location(loc), name(loc->hasName() ? loc->getName() : "") {
  CHECK(!getCollective()->getCreatures().empty());
}

SERIALIZATION_CONSTRUCTOR_IMPL(VillageControl);

VillageControl::~VillageControl() {
}

const string& VillageControl::getName() const {
  return name;
}

const Collective* VillageControl::getVillain() const {
  return villain;
}

bool VillageControl::isAnonymous() const {
  return name.empty();
}

void VillageControl::tick(double time) {
}

vector<Creature*> VillageControl::getCreatures(MinionTrait trait) const {
  return getCollective()->getCreatures({trait});
}

vector<Creature*> VillageControl::getCreatures() const {
  return getCollective()->getCreatures();
}

class AttackTrigger {
  public:
  AttackTrigger(VillageControl* c) : control(c) {
  }

  virtual void tick(double time) {};
  virtual bool startedAttack(const Creature*) = 0;

  virtual ~AttackTrigger() {}

  SERIALIZATION_CONSTRUCTOR(AttackTrigger);

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar& SVAR(control);
    CHECK_SERIAL;
  }

  protected:
  VillageControl* SERIAL(control);
};

bool VillageControl::currentlyAttacking() const {
  return false;
}

bool VillageControl::isConquered() const {
  return getCreatures(MinionTrait::FIGHTER).empty();
}

Tribe* VillageControl::getTribe() {
  return getCollective()->getTribe();
}

const Tribe* VillageControl::getTribe() const {
  return getCollective()->getTribe();
}


static int expLevelFun(double time) {
  return 10 + max(0.0, time - 1000) / 500;
}

void VillageControl::onCreatureKilled(const Creature* victim, const Creature* killer) {
  if (!isAnonymous() && getCreatures(MinionTrait::FIGHTER).empty() && !conquered) {
    GlobalEvents.addConquerEvent(this);
    conquered = true;
  }
}

class AttackTriggerSet : public AttackTrigger {
  public:
  AttackTriggerSet(VillageControl* c) : AttackTrigger(c) {}

  virtual bool startedAttack(const Creature* c) override {
    return fightingCreatures.count(c);
  }

  SERIALIZATION_CONSTRUCTOR(AttackTriggerSet);

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar& SUBCLASS(AttackTrigger)
      & SVAR(fightingCreatures);
    CHECK_SERIAL;
  }

  protected:
  set<const Creature*> SERIAL(fightingCreatures);
};

class PowerTrigger : public AttackTriggerSet {
  public:
  // How long to wait between being attacked and attacking
  const int attackDelay = 200;

  // How long to wait between consecutive attacks from the same village
  const int myAttacksDelay = 900;

  PowerTrigger(VillageControl* c, double _killedCoeff, double _powerCoeff)
      : AttackTriggerSet(c), killedCoeff(_killedCoeff), powerCoeff(_powerCoeff) {
    double myPower = 0;
    for (const Creature* c : control->getCreatures(MinionTrait::FIGHTER))
      myPower += c->getDifficultyPoints();
    Debug() << "Village " << control->getName() << " power " << myPower;
    for (int i : Range(Random.getRandom(1, 3))) {
      double trigger = myPower * Random.getDouble(0.4, 1.2);
      triggerAmounts.insert(trigger);
      Debug() << "Village " << control->getName() << " trigger " << trigger;
    }
  }

  double getCurrentTrigger(double time) {
    double enemyPoints = killedCoeff * killedPoints + powerCoeff * (control->getVillain()->getWarLevel()
      + max(0.0, (time - 2000) / 2));
    Debug() << "Village " << control->getName() << " enemy points " << enemyPoints;
    double currentTrigger = 0;
    for (double trigger : triggerAmounts)
      if (trigger <= enemyPoints)
        currentTrigger = trigger;
      else
        break;
    return currentTrigger;
  }

  REGISTER_HANDLER(KillEvent, const Creature* victim, const Creature* killer) {
    if (victim->getTribe() == control->getTribe() 
        && (!killer || contains(control->getVillain()->getCreatures(), killer))) {
      killedPoints += victim->getDifficultyPoints();
      lastAttack = victim->getTime();
    }
  }

  vector<Creature*> getSorted(vector<Creature*> ret) {
    sort(ret.begin(), ret.end(), [] (const Creature* c1, const Creature* c2) {
        return c1->getDifficultyPoints() < c2->getDifficultyPoints();
    });
    return ret;
  }

  virtual void tick(double time) override {
    if (control->getCreatures(MinionTrait::FIGHTER).empty())
      return;
    if (lastAttack >= time - attackDelay || lastMyAttack >= time - myAttacksDelay)
      return;
    double lastAttackPoints = 0;
    for (const Creature* c : control->getCreatures(MinionTrait::FIGHTER))
      if (fightingCreatures.count(c))
        lastAttackPoints += c->getDifficultyPoints();
    bool firstAttack = lastAttackPoints == 0;
    double currentTrigger = getCurrentTrigger(time);
    if (lastAttackPoints < currentTrigger) {
      lastMyAttack = time;
      int numCreatures = 0;
      for (const Creature* c : getSorted(control->getCreatures(MinionTrait::FIGHTER)))
        if (!fightingCreatures.count(c) && !c->isDead()) {
          ++numCreatures;
          fightingCreatures.insert(c);
          if ((lastAttackPoints += c->getDifficultyPoints()) >= currentTrigger)
            break;
        }
      if (numCreatures > 0) {
        if (currentTrigger >= *triggerAmounts.rbegin() - 0.001)
          lastAttackLaunched = true;
      }
    }
  }

  SERIALIZATION_CONSTRUCTOR(PowerTrigger);

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar& SUBCLASS(AttackTriggerSet)
      & SVAR(killedPoints)
      & SVAR(killedCoeff)
      & SVAR(powerCoeff)
      & SVAR(lastAttack)
      & SVAR(lastMyAttack)
      & SVAR(lastAttackLaunched)
      & SVAR(triggerAmounts);
    CHECK_SERIAL;
  }

  private:
  double SERIAL2(killedPoints, 0);
  double SERIAL(killedCoeff);
  double SERIAL(powerCoeff);
  double SERIAL2(lastAttack, 0);
  double SERIAL2(lastMyAttack, 0);
  bool SERIAL2(lastAttackLaunched, false);
  set<double> SERIAL(triggerAmounts);
};

DEF_UNIQUE_PTR(AttackTrigger);

class FirstContact : public AttackTrigger {
  public:
  FirstContact(VillageControl* c, PAttackTrigger o) : AttackTrigger(c), other(std::move(o)) {}

  virtual bool startedAttack(const Creature* c) {
    return madeContact < c->getTime() && other->startedAttack(c);
  }

  virtual void tick(double time) override {
    if (madeContact < time)
      other->tick(time);
  }

  REGISTER_HANDLER(CombatEvent, const Creature* c) {
    if (contains(control->getCreatures(), NOTNULL(c)))
      madeContact = min(madeContact, c->getTime() + 50);
  }

  SERIALIZATION_CONSTRUCTOR(FirstContact);

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar& SUBCLASS(AttackTrigger)
      & SVAR(other)
      & SVAR(madeContact);
    CHECK_SERIAL;
  }

  private:
  PAttackTrigger SERIAL(other);
  double SERIAL2(madeContact, 1000000);
};

GameInfo::VillageInfo::Village VillageControl::getVillageInfo() const {
  GameInfo::VillageInfo::Village info;
  info.name = name;
  info.tribeName = getTribe()->getName();
  if (currentlyAttacking())
    info.state = "attacking!";
  else if (isConquered())
    info.state = "conquered";
  return info;
}

class FinalTrigger : public AttackTriggerSet {
  public:
  FinalTrigger(VillageControl* c, vector<VillageControl*> _controls) : AttackTriggerSet(c), controls(_controls) {}

  virtual void tick(double time) {
    if (totalWar < 0)
      return;
    bool allConquered = true;
    for (VillageControl* c : controls)
      allConquered &= (c->isConquered() || c->isAnonymous());
    if (allConquered && !control->isConquered() && totalWar == 100000)
      totalWar = time + Random.getRandom(80, 200);
    if (totalWar < time) {
      totalWar = -1;
      for (const Creature* c : control->getCreatures(MinionTrait::FIGHTER))
        fightingCreatures.insert(c);
    }
  }

  SERIALIZATION_CONSTRUCTOR(FinalTrigger);

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar& SUBCLASS(AttackTriggerSet)
      & SVAR(controls)
      & SVAR(totalWar);
    CHECK_SERIAL;
  }

  private:
  vector<VillageControl*> SERIAL(controls);
  double SERIAL2(totalWar, 100000);
};

class PeacefulControl : public VillageControl {
  public:
  PeacefulControl(Collective* col, Collective* villain, const Location* loc)
    : VillageControl(col, villain, loc) {
    for (Vec2 v : location->getBounds())
      if (loc->getLevel()->getSquare(v)->getName() == "bed")
        beds.push_back(v);
  }

  virtual string getAttackMessage() const {
    return "";
  }

  virtual PTask getNewTask(Creature* c) override {
    if (c->getLevel()->getModel()->getSunlightInfo().state == Model::SunlightInfo::NIGHT 
        && !c->isAffected(LastingEffect::SLEEP) && c->isHumanoid()) {
      for (Vec2 v : beds)
        if (c->getLevel()->getSquare(v)->canEnter(c))
          return Task::applySquare(this, {v});
    }
    return nullptr;
  }

  virtual MoveInfo getMove(Creature* c) override {
    if (auto action = c->stayIn(location))
      return {1.0, action};
    else
      return NoMove;
  }

  SERIALIZATION_CONSTRUCTOR(PeacefulControl);

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar& SUBCLASS(VillageControl)
      & SVAR(beds);
    CHECK_SERIAL;
  }

  private:
  vector<Vec2> SERIAL(beds);
};

class TopLevelVillageControl : public PeacefulControl {
  public:
  TopLevelVillageControl(Collective* col, Collective* villain, const Location* location, AttackAction a)
      : PeacefulControl(col, villain, location), action(a) {}

  virtual PTask getNewTask(Creature* c) override {
    if (attackTrigger->startedAttack(c)) {
      villain->addAssaultNotification(c, this);
      Debug() << c->getName() << " " << c->getUniqueId() << " assaulting ";
      PTask t;
      switch (action) {
        case VillageControlInfo::ATTACK_LEADER: t = Task::attackLeader(villain); break;
        case VillageControlInfo::STEAL: t = Task::stealFrom(villain); break;
      }
      if (t->getMove(c))
        return t;
    }
    return PeacefulControl::getNewTask(c);
  }

  virtual string getAttackMessage() const {
    if (getName().empty())
      return "You are being attacked by " + getTribe()->getName() + "!";
    else
      return "You are being attacked by " + getTribe()->getName() + " of " + getName() + "!";
  }

  virtual MoveInfo getMove(Creature* c) override {
    if (!attackTrigger->startedAttack(c))
      return PeacefulControl::getMove(c);
    else
      return NoMove;
  }

  virtual void tick(double time) override {
    attackTrigger->tick(time);
    for (Creature* c : getCreatures())
      if (c->getExpLevel() < expLevelFun(time))
        c->exerciseAttr(chooseRandom<AttrType>());
  }

  void setPowerTrigger(double killedCoeff, double powerCoeff) {
    attackTrigger.reset(new PowerTrigger(this, killedCoeff, powerCoeff));
  }

  void setFinalTrigger(vector<VillageControl*> otherControls) {
    attackTrigger.reset(new FinalTrigger(this, otherControls));
  }

  void setOnFirstContact() {
    attackTrigger.reset(new FirstContact(this, std::move(attackTrigger)));
  }

  virtual bool currentlyAttacking() const override {
    for (const Creature* c : getCreatures())
      if (attackTrigger->startedAttack(c))
        return true;
    return false;
  }


  SERIALIZATION_CONSTRUCTOR(TopLevelVillageControl);

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar& SUBCLASS(PeacefulControl)
      & SVAR(attackTrigger);
    CHECK_SERIAL;
  }

  private:
  unique_ptr<AttackTrigger> SERIAL(attackTrigger);
  AttackAction SERIAL(action);
};

namespace {

class DragonControl : public VillageControl {
  public:
  DragonControl(Collective* col, Collective* villain, const Location* location)
      : VillageControl(col, villain, location) {}

  const int waitTurns = 2000;

  const int populationLimit = 25;

  const int minVictims = 5;
  const int maxVictims = 10;

  virtual string getAttackMessage() const override {
    const Creature* dragon = getOnlyElement(getCollective()->getCreatures());
    return "You are under attack by " + dragon->getName() + " the " + dragon->getSpeciesName() + "!";
  }

  virtual PTask getNewTask(Creature* c) override {
    if (c->getLevel() != villain->getLevel())
      return nullptr;
    int population = villain->getCreatures(MinionTrait::FIGHTER).size();
    if (population > populationLimit && c->getTime() >= nextAttack) {
      nextAttack = c->getTime() + waitTurns;
      villain->addAssaultNotification(c, this);
      return Task::killFighters(villain, c, Random.getRandom(minVictims, maxVictims));
    } else {
      villain->removeAssaultNotification(c, this);
      return Task::stayInLocationUntil(location, nextAttack);
    }

  }

  virtual void tick(double time) {
  }

  SERIALIZATION_CONSTRUCTOR(DragonControl);

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar& SUBCLASS(VillageControl)
      & SVAR(nextAttack);
    CHECK_SERIAL;
  }

  private:
  double SERIAL2(nextAttack, -10000);
};

}

PVillageControl VillageControl::get(VillageControlInfo info, Collective* col, Collective* villain,
    const Location* location) {
  switch (info.id) {
    case VillageControlInfo::DRAGON: return PVillageControl(new DragonControl(col, villain, location));
    case VillageControlInfo::PEACEFUL: return PVillageControl(new PeacefulControl(col, villain, location));
    case VillageControlInfo::POWER_BASED_DISCOVER:
    case VillageControlInfo::POWER_BASED: {
      TopLevelVillageControl* ret = new TopLevelVillageControl(col, villain, location, info.action);
      ret->setPowerTrigger(info.killedCoeff, info.powerCoeff);
      if (info.id == VillageControlInfo::POWER_BASED_DISCOVER)
        ret->setOnFirstContact();
      return PVillageControl(ret);
      }
    default: FAIL << "Unsupported " << int(info.id); return PVillageControl();
  }
}

PVillageControl VillageControl::getFinalAttack(Collective* col, Collective* villain, const Location* location,
      vector<VillageControl*> otherControls) {
  TopLevelVillageControl* ret = new TopLevelVillageControl(col, villain, location, VillageControlInfo::ATTACK_LEADER);
  ret->setFinalTrigger(otherControls);
  return PVillageControl(ret);
}


template <class Archive>
void VillageControl::registerTypes(Archive& ar) {
  REGISTER_TYPE(ar, TopLevelVillageControl);
  REGISTER_TYPE(ar, PowerTrigger);
  REGISTER_TYPE(ar, AttackTriggerSet);
  REGISTER_TYPE(ar, FirstContact);
  REGISTER_TYPE(ar, FinalTrigger);
  REGISTER_TYPE(ar, PeacefulControl);
  REGISTER_TYPE(ar, DragonControl);
}

REGISTER_TYPES(VillageControl);

