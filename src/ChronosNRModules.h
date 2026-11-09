#ifndef CHRONOSNRMODULES_H_
#define CHRONOSNRMODULES_H_

#include <omnetpp.h>
#include <vector>
#include <string>
#include "ChronosNRMsg_m.h"

using namespace omnetpp;

class PrbChannel : public cSimpleModule {
  private:
    int prbIndex = 0;
    int numUsers = 5;
    simtime_t embbSlotDuration;
    simtime_t urllcMinislotDuration;

    cMessage *endEmbbSlotTimer = nullptr;
    cMessage *endUrllcMinislotTimer = nullptr;

    FiveGBlock *currentEmbbCopy = nullptr;
    FiveGBlock *currentUrllcBlock = nullptr;
    bool overwrittenByUrllc = false;
    int overwriteCountThisSlot = 0;

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void refreshDisplay() const override;
    virtual void finish() override;

  public:
    virtual ~PrbChannel() {}
    bool isCompletelyFree() const { return (currentEmbbCopy == nullptr) && (currentUrllcBlock == nullptr); }
    bool isOccupiedByEmbb() const { return (currentEmbbCopy != nullptr); }
    bool isBusyWithUrllcThisMinislot() const { return (currentUrllcBlock != nullptr); }
};

class EmbbUserBuffer : public cSimpleModule {
  private:
    int userId = 0;
    int userWeight = 1;
    int numPRBs = 10;

    cQueue highPrioRetxQueue;
    cQueue keptQueue;
    cQueue waitingQueue;

    simsignal_t retxQueuedSignal;
    simsignal_t keptDroppedSignal;

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void refreshDisplay() const override;
    virtual void finish() override;

  public:
    virtual ~EmbbUserBuffer() {}
    int getRetxQueueLength() const { return highPrioRetxQueue.getLength(); }
    int getWaitingQueueLength() const { return waitingQueue.getLength(); }
    int getTotalSchedulableLength() const { return highPrioRetxQueue.getLength() + waitingQueue.getLength(); }
    int getUserWeight() const { return userWeight; }
};

class UrllcQueueModule : public cSimpleModule {
  private:
    int numPRBs = 10;
    cQueue urllcWaitingQueue;

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void refreshDisplay() const override;
    virtual void finish() override;

  public:
    virtual ~UrllcQueueModule() {}
    int getQueueLength() const { return urllcWaitingQueue.getLength(); }
};

#endif
