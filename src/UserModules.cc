#include "ChronosNRModules.h"
#include <cstdio>

class PacketGenerator : public cSimpleModule {
  private:
    cMessage *sendTimer = nullptr;
    int trafficClass = 0;
    int userId = 0;
    int userWeight = 1;
    static long globalBlockCounter;

  protected:
    virtual void initialize() override {
        trafficClass = par("trafficClass");
        userId = par("userId");
        userWeight = par("userWeight");
        sendTimer = new cMessage("sendTimer");
        sendTimer->setSchedulingPriority(3);
        scheduleAt(simTime() + par("interArrivalTime"), sendTimer);
    }

    virtual void handleMessage(cMessage *msg) override {
        if (msg == sendTimer) {
            const char *name = (trafficClass == 0) ? "eMBB_Block" : "URLLC_Block";
            FiveGBlock *pkt = new FiveGBlock(name);
            pkt->setSchedulingPriority(3);
            pkt->setTrafficClass(trafficClass);
            pkt->setBlockId(++globalBlockCounter);
            pkt->setUserId(userId);
            pkt->setUserWeight(userWeight);
            pkt->setCreationTime(simTime());

            send(pkt, "out");
            scheduleAt(simTime() + par("interArrivalTime"), sendTimer);
        }
    }

    virtual void finish() override {
        cancelAndDelete(sendTimer);
        sendTimer = nullptr;
    }
};

long PacketGenerator::globalBlockCounter = 0;
Define_Module(PacketGenerator);

Define_Module(EmbbUserBuffer);

void EmbbUserBuffer::initialize() {
    userId = par("userId");
    userWeight = par("userWeight");
    numPRBs = par("numPRBs");

    highPrioRetxQueue.setName("highPrioRetxQueue");
    keptQueue.setName("keptQueue");
    waitingQueue.setName("waitingQueue");

    retxQueuedSignal = registerSignal("retxQueuedSignal");
    keptDroppedSignal = registerSignal("keptDroppedSignal");
}

void EmbbUserBuffer::handleMessage(cMessage *msg) {
    if (msg->arrivedOn("in")) {
        waitingQueue.insert(msg);
    }
    else if (msg->arrivedOn("feedbackIn")) {
        PrbFeedback *fb = check_and_cast<PrbFeedback *>(msg);
        long targetId = fb->getBlockId();
        FiveGBlock *keptOrig = nullptr;

        for (cQueue::Iterator it(keptQueue); !it.end(); ++it) {
            FiveGBlock *cand = check_and_cast<FiveGBlock *>(*it);
            if (cand->getBlockId() == targetId) {
                keptOrig = cand;
                break;
            }
        }

        if (keptOrig != nullptr) {
            keptQueue.remove(keptOrig);
            if (fb->getOverwritten()) {
                keptOrig->setRetxCount(keptOrig->getRetxCount() + 1);
                keptOrig->setOverwritten(false);
                highPrioRetxQueue.insert(keptOrig);
                emit(retxQueuedSignal, (long)1);
                EV_WARN << "[eMBB User " << userId << "] Block #" << targetId
                        << " was overwritten on PRB " << fb->getPrbIndex()
                        << " -> Queued for HIGH-PRIORITY retransmission!\n";
            } else {
                emit(keptDroppedSignal, (long)1);
                delete keptOrig;
            }
        }
        delete fb;
    }
    else if (msg->arrivedOn("grantIn")) {
        SchedGrant *grant = check_and_cast<SchedGrant *>(msg);
        int b = grant->getAssignedPrb();
        delete grant;

        FiveGBlock *origBlock = nullptr;
        if (!highPrioRetxQueue.isEmpty()) {
            origBlock = check_and_cast<FiveGBlock *>(highPrioRetxQueue.pop());
        } else if (!waitingQueue.isEmpty()) {
            origBlock = check_and_cast<FiveGBlock *>(waitingQueue.pop());
        }

        if (origBlock != nullptr) {
            origBlock->setAssignedPrb(b);

            FiveGBlock *copyBlock = origBlock->dup();
            copyBlock->setName("eMBB_Copy");
            copyBlock->setIsCopy(true);
            copyBlock->setSchedulingPriority(2);
            send(copyBlock, "prbOut", b);

            keptQueue.insert(origBlock);
        }
    }
}

void EmbbUserBuffer::refreshDisplay() const {
    char buf[80];
    std::snprintf(buf, sizeof(buf), "W=%d | wait:%d kept:%d retx:%d",
                  userWeight, waitingQueue.getLength(), keptQueue.getLength(), highPrioRetxQueue.getLength());
    getDisplayString().setTagArg("t", 0, buf);
    if (getParentModule() != nullptr) {
        getParentModule()->getDisplayString().setTagArg("t", 0, buf);
    }
}

void EmbbUserBuffer::finish() {
    while (!highPrioRetxQueue.isEmpty()) delete highPrioRetxQueue.pop();
    while (!keptQueue.isEmpty()) delete keptQueue.pop();
    while (!waitingQueue.isEmpty()) delete waitingQueue.pop();
}

Define_Module(UrllcQueueModule);

void UrllcQueueModule::initialize() {
    numPRBs = par("numPRBs");
    urllcWaitingQueue.setName("urllcWaitingQueue");
}

void UrllcQueueModule::handleMessage(cMessage *msg) {
    if (msg->arrivedOn("in")) {
        urllcWaitingQueue.insert(msg);
    }
    else if (msg->arrivedOn("grantIn")) {
        SchedGrant *grant = check_and_cast<SchedGrant *>(msg);
        int b = grant->getAssignedPrb();
        delete grant;

        if (!urllcWaitingQueue.isEmpty()) {
            FiveGBlock *urllcPkt = check_and_cast<FiveGBlock *>(urllcWaitingQueue.pop());
            urllcPkt->setAssignedPrb(b);
            urllcPkt->setSchedulingPriority(4);
            send(urllcPkt, "prbOut", b);
        }
    }
}

void UrllcQueueModule::refreshDisplay() const {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "URLLC wait:%d", urllcWaitingQueue.getLength());
    getDisplayString().setTagArg("t", 0, buf);
    if (getParentModule() != nullptr) {
        getParentModule()->getDisplayString().setTagArg("t", 0, buf);
    }
}

void UrllcQueueModule::finish() {
    while (!urllcWaitingQueue.isEmpty()) delete urllcWaitingQueue.pop();
}
