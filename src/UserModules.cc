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
        if (trafficClass == 0 && userId == 0) globalBlockCounter = 0;
        sendTimer = new cMessage("sendTimer");
        sendTimer->setSchedulingPriority(3);
        scheduleAt(simTime() + SimTime(par("interArrivalTime").doubleValueInUnit("s")), sendTimer);
    }

    virtual void handleMessage(cMessage *msg) override {
        if (msg == sendTimer) {
            const char *name = (trafficClass == 0) ? "eMBB_Block" : "URLLC_Block";
            FiveGBlock *pkt = new FiveGBlock(name);
            pkt->setDisplayString((trafficClass == 0)
                ? "b=14,14,oval,#3b82f6,#1d4ed8,2"
                : "b=16,16,oval,#ef4444,#b91c1c,3");
            pkt->setSchedulingPriority(3);
            pkt->setTrafficClass(trafficClass);
            pkt->setBlockId(++globalBlockCounter);
            pkt->setUserId(userId);
            pkt->setUserWeight(userWeight);
            pkt->setCreationTime(simTime());

            send(pkt, "out");
            scheduleAt(simTime() + SimTime(par("interArrivalTime").doubleValueInUnit("s")), sendTimer);
        }
    }

    virtual void finish() override {
        cancelAndDelete(sendTimer);
        sendTimer = nullptr;
    }
};

long PacketGenerator::globalBlockCounter = 0;
Define_Module(PacketGenerator);

long EmbbUserBuffer::totalRetxGlobal = 0;
Define_Module(EmbbUserBuffer);

void EmbbUserBuffer::initialize() {
    userId = par("userId");
    userWeight = par("userWeight");
    numPRBs = par("numPRBs");
    if (userId == 0) totalRetxGlobal = 0;

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
                totalRetxGlobal++;
                emit(retxQueuedSignal, 1);
                if (hasGUI() && getParentModule() != nullptr) {
                    getParentModule()->bubble("HIGH-PRIO RETX QUEUED!");
                }
            } else {
                emit(keptDroppedSignal, 1);
                delete keptOrig;
            }
        }
        delete fb;
    }
    else if (msg->arrivedOn("grantIn")) {
        SchedGrant *grant = check_and_cast<SchedGrant *>(msg);
        int b = grant->getAssignedPrb();
        bool deepFade = grant->getDeepFade();
        delete grant;

        FiveGBlock *origBlock = nullptr;
        bool isRetx = false;
        if (!highPrioRetxQueue.isEmpty()) {
            origBlock = check_and_cast<FiveGBlock *>(highPrioRetxQueue.pop());
            isRetx = true;
        } else if (!waitingQueue.isEmpty()) {
            origBlock = check_and_cast<FiveGBlock *>(waitingQueue.pop());
        }

        if (origBlock != nullptr) {
            origBlock->setAssignedPrb(b);

            FiveGBlock *copyBlock = origBlock->dup();
            copyBlock->setName(isRetx ? "eMBB_RetxCopy" : "eMBB_Copy");
            copyBlock->setDisplayString(isRetx
                ? "b=16,16,oval,#f59e0b,#b45309,3"
                : "b=14,14,oval,#3b82f6,#1d4ed8,2");
            copyBlock->setIsCopy(true);
            copyBlock->setOverwritten(deepFade);
            copyBlock->setSchedulingPriority(2);
            send(copyBlock, "prbOut", b);

            keptQueue.insert(origBlock);
        }
    }
}

void EmbbUserBuffer::refreshDisplay() const {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "W=%d | wait:%d | kept:%d | retx:%d",
                  userWeight, waitingQueue.getLength(), keptQueue.getLength(), highPrioRetxQueue.getLength());
    getDisplayString().setTagArg("t", 0, buf);
    if (getParentModule() != nullptr) {
        getParentModule()->getDisplayString().setTagArg("t", 0, buf);
        getParentModule()->getDisplayString().setTagArg("t", 2,
            (highPrioRetxQueue.getLength() > 0) ? "#dc2626" : "#1e3a8a");
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
            urllcPkt->setDisplayString("b=16,16,oval,#ef4444,#991b1b,3");
            urllcPkt->setAssignedPrb(b);
            urllcPkt->setSchedulingPriority(4);
            send(urllcPkt, "prbOut", b);
        }
    }
}

void UrllcQueueModule::refreshDisplay() const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "URLLC wait:%d (0.1ms cycle)", urllcWaitingQueue.getLength());
    getDisplayString().setTagArg("t", 0, buf);
    if (getParentModule() != nullptr) {
        getParentModule()->getDisplayString().setTagArg("t", 0, buf);
        getParentModule()->getDisplayString().setTagArg("t", 2, "#b91c1c");
    }
}

void UrllcQueueModule::finish() {
    while (!urllcWaitingQueue.isEmpty()) delete urllcWaitingQueue.pop();
}
