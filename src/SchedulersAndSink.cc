#include "ChronosNRModules.h"
#include <cstdio>

class EmbbScheduler : public cSimpleModule {
  private:
    int numUsers = 5;
    int numPRBs = 10;
    simtime_t cycle;
    std::string schedulingPolicy;
    cMessage *cycleTimer = nullptr;
    long slotCounter = 0;

    std::vector<EmbbUserBuffer*> userBuffers;
    std::vector<int> virtualQueueCredits;
    std::vector<int> virtualRetxPending;
    std::vector<int> virtualWaitPending;
    std::vector<double> userCqi;
    std::vector<double> avgRate;
    int rrPointer = 0;
    int retxPointer = 0;

  protected:
    virtual void initialize() override {
        numUsers = par("numUsers");
        numPRBs = par("numPRBs");
        cycle = SimTime(par("cycle").doubleValueInUnit("s"));
        schedulingPolicy = par("schedulingPolicy").stdstringValue();

        userBuffers.resize(numUsers, nullptr);
        virtualQueueCredits.assign(numUsers, 0);
        virtualRetxPending.assign(numUsers, 0);
        virtualWaitPending.assign(numUsers, 0);
        userCqi.assign(numUsers, 5.0);
        avgRate.assign(numUsers, 1.0);

        for (int u = 0; u < numUsers; u++) {
            cModule *userMod = getParentModule()->getSubmodule("embbUser", u)->getSubmodule("buffer");
            userBuffers[u] = check_and_cast<EmbbUserBuffer *>(userMod);
            virtualQueueCredits[u] = userBuffers[u]->getUserWeight();
        }

        cycleTimer = new cMessage("embbCycleTimer");
        cycleTimer->setSchedulingPriority(2);
        scheduleAt(simTime(), cycleTimer);
    }

    virtual void handleMessage(cMessage *msg) override {
        if (msg == cycleTimer) {
            slotCounter++;
            runEmbbSchedulingCycle();
            scheduleAt(simTime() + cycle, cycleTimer);
        }
    }

    void runEmbbSchedulingCycle() {
        for (int u = 0; u < numUsers; u++) {
            virtualRetxPending[u] = userBuffers[u]->getRetxQueueLength();
            virtualWaitPending[u] = userBuffers[u]->getWaitingQueueLength();
            // Section VI of Anand et al.: Robust users {4..10}, Sensitive users {1..5}
            userCqi[u] = (u < 3) ? intuniform(4, 10) : intuniform(1, 5);
        }

        for (int b = 0; b < numPRBs; b++) {
            PrbChannel *prbMod = check_and_cast<PrbChannel *>(getParentModule()->getSubmodule("prb", b));
            if (!prbMod->isCompletelyFree()) {
                continue;
            }

            int chosenUser = -1;
            bool isRetxGrant = false;

            // PASS 1: Strict High-Priority Retransmission Queue
            for (int i = 0; i < numUsers; i++) {
                int u = (retxPointer + i) % numUsers;
                if (virtualRetxPending[u] > 0) {
                    chosenUser = u;
                    virtualRetxPending[u]--;
                    retxPointer = (u + 1) % numUsers;
                    isRetxGrant = true;
                    break;
                }
            }

            // PASS 2: Normal Queue Allocation (WRR, PF, MAX_CI, RR)
            if (chosenUser == -1) {
                chosenUser = selectNormalUser();
                if (chosenUser != -1) {
                    virtualWaitPending[chosenUser]--;
                }
            }

            if (chosenUser == -1) {
                break;
            }

            // Channel-blind schedulers (RR/WRR) may schedule users during a deep fade (CQI <= 2)
            bool fadeLoss = (!isRetxGrant && userCqi[chosenUser] <= 2 && uniform(0.0, 1.0) < 0.35);

            SchedGrant *grant = new SchedGrant("eMBB_Grant");
            grant->setDisplayString("b=10,10,rect,#f59e0b,#d97706,1");
            grant->setSchedulingPriority(2);
            grant->setAssignedPrb(b);
            grant->setDeepFade(fadeLoss);
            send(grant, "grantOut", chosenUser);
        }
    }

    int selectNormalUser() {
        bool anyHasData = false;
        for (int u = 0; u < numUsers; u++) {
            if (virtualWaitPending[u] > 0) {
                anyHasData = true;
                break;
            }
        }
        if (!anyHasData) return -1;

        if (schedulingPolicy == "WRR") {
            for (int pass = 0; pass < 2; pass++) {
                for (int i = 0; i < numUsers; i++) {
                    int u = (rrPointer + i) % numUsers;
                    if (virtualWaitPending[u] > 0 && virtualQueueCredits[u] > 0) {
                        virtualQueueCredits[u]--;
                        if (virtualQueueCredits[u] == 0) {
                            rrPointer = (u + 1) % numUsers;
                        }
                        return u;
                    }
                }
                for (int u = 0; u < numUsers; u++) {
                    virtualQueueCredits[u] = userBuffers[u]->getUserWeight();
                }
            }
        }
        else if (schedulingPolicy == "RR") {
            for (int i = 0; i < numUsers; i++) {
                int u = (rrPointer + i) % numUsers;
                if (virtualWaitPending[u] > 0) {
                    rrPointer = (u + 1) % numUsers;
                    return u;
                }
            }
        }
        else {
            int bestU = -1;
            double bestMetric = -1.0;
            for (int u = 0; u < numUsers; u++) {
                if (virtualWaitPending[u] > 0) {
                    double m = (schedulingPolicy == "MAX_CI")
                        ? userCqi[u]
                        : ((userCqi[u] * (1.0 + 0.15 * virtualWaitPending[u])) / avgRate[u]);
                    if (m > bestMetric) {
                        bestMetric = m;
                        bestU = u;
                    }
                }
            }
            if (bestU != -1) {
                double eps = 0.05;
                for (int u = 0; u < numUsers; u++) {
                    double r = (u == bestU) ? (userCqi[u] / numPRBs) : 0.0;
                    avgRate[u] = (1.0 - eps) * avgRate[u] + eps * r;
                }
            }
            return bestU;
        }
        return -1;
    }

    virtual void refreshDisplay() const override {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "1.0ms Slot #%ld [%s]", slotCounter, schedulingPolicy.c_str());
        getDisplayString().setTagArg("t", 0, buf);
        getDisplayString().setTagArg("t", 2, "#b45309");
    }

    virtual void finish() override {
        cancelAndDelete(cycleTimer);
        cycleTimer = nullptr;
    }
};

Define_Module(EmbbScheduler);

class UrllcScheduler : public cSimpleModule {
  private:
    int numPRBs = 10;
    simtime_t cycle;
    cMessage *cycleTimer = nullptr;
    UrllcQueueModule *urllcBuffer = nullptr;
    long minislotCounter = 0;

    simsignal_t urllcFreeChannelSignal;
    simsignal_t urllcPunctureChannelSignal;

  protected:
    virtual void initialize() override {
        numPRBs = par("numPRBs");
        cycle = SimTime(par("cycle").doubleValueInUnit("s"));
        cModule *bufMod = getParentModule()->getSubmodule("urllcUser")->getSubmodule("buffer");
        urllcBuffer = check_and_cast<UrllcQueueModule *>(bufMod);

        urllcFreeChannelSignal = registerSignal("urllcFreeChannelSignal");
        urllcPunctureChannelSignal = registerSignal("urllcPunctureChannelSignal");

        cycleTimer = new cMessage("urllcCycleTimer");
        cycleTimer->setSchedulingPriority(4);
        scheduleAt(simTime(), cycleTimer);
    }

    virtual void handleMessage(cMessage *msg) override {
        if (msg == cycleTimer) {
            minislotCounter++;
            runUrllcMinislotCycle();
            scheduleAt(simTime() + cycle, cycleTimer);
        }
    }

    void runUrllcMinislotCycle() {
        int pendingUrllc = urllcBuffer->getQueueLength();
        if (pendingUrllc == 0) return;

        std::vector<bool> assignedThisMinislot(numPRBs, false);

        while (pendingUrllc > 0) {
            int targetPrb = -1;
            for (int b = 0; b < numPRBs; b++) {
                PrbChannel *prbMod = check_and_cast<PrbChannel *>(getParentModule()->getSubmodule("prb", b));
                if (!assignedThisMinislot[b] && prbMod->isCompletelyFree()) {
                    targetPrb = b;
                    break;
                }
            }

            if (targetPrb != -1) {
                assignedThisMinislot[targetPrb] = true;
                pendingUrllc--;
                emit(urllcFreeChannelSignal, 1);

                SchedGrant *grant = new SchedGrant("URLLC_Free_Grant");
                grant->setDisplayString("b=10,10,rect,#06b6d4,#0891b2,2");
                grant->setSchedulingPriority(4);
                grant->setAssignedPrb(targetPrb);
                send(grant, "grantOut");
                continue;
            }

            std::vector<int> occupiedCandidates;
            for (int b = 0; b < numPRBs; b++) {
                PrbChannel *prbMod = check_and_cast<PrbChannel *>(getParentModule()->getSubmodule("prb", b));
                if (!assignedThisMinislot[b] && prbMod->isOccupiedByEmbb() && !prbMod->isBusyWithUrllcThisMinislot()) {
                    occupiedCandidates.push_back(b);
                }
            }

            if (!occupiedCandidates.empty()) {
                int randIdx = intuniform(0, occupiedCandidates.size() - 1);
                targetPrb = occupiedCandidates[randIdx];
                assignedThisMinislot[targetPrb] = true;
                pendingUrllc--;
                emit(urllcPunctureChannelSignal, 1);

                SchedGrant *grant = new SchedGrant("URLLC_Puncture_Grant");
                grant->setDisplayString("b=12,12,rect,#ef4444,#b91c1c,2");
                grant->setSchedulingPriority(4);
                grant->setAssignedPrb(targetPrb);
                send(grant, "grantOut");
            } else {
                break;
            }
        }
    }

    virtual void refreshDisplay() const override {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "0.1ms Minislot #%ld", minislotCounter);
        getDisplayString().setTagArg("t", 0, buf);
        getDisplayString().setTagArg("t", 2, "#b91c1c");
    }

    virtual void finish() override {
        cancelAndDelete(cycleTimer);
        cycleTimer = nullptr;
    }
};

Define_Module(UrllcScheduler);

class RadioSink : public cSimpleModule {
  private:
    int numUsers = 5;
    simsignal_t embbDelaySignal;
    simsignal_t urllcDelaySignal;
    simsignal_t embbRetxCountSignal;
    long totalEmbbDelivered = 0;
    long totalUrllcDelivered = 0;
    simtime_t totalEmbbDelaySum = SIMTIME_ZERO;
    simtime_t totalUrllcDelaySum = SIMTIME_ZERO;
    std::vector<long> userBlocksDelivered;
    std::vector<simtime_t> userDelaySum;
    cTextFigure *hudFigure = nullptr;
    cTextFigure *sinkStatsFigure = nullptr;

  protected:
    virtual void initialize() override {
        numUsers = par("numUsers");
        embbDelaySignal = registerSignal("embbDelaySignal");
        urllcDelaySignal = registerSignal("urllcDelaySignal");
        embbRetxCountSignal = registerSignal("embbRetxCountSignal");
        userBlocksDelivered.assign(numUsers, 0);
        userDelaySum.assign(numUsers, SIMTIME_ZERO);

        cCanvas *canvas = getParentModule()->getCanvas();
        if (canvas != nullptr) {
            hudFigure = check_and_cast_nullable<cTextFigure *>(canvas->getFigure("hudText"));
            sinkStatsFigure = check_and_cast_nullable<cTextFigure *>(canvas->getFigure("sinkStatsText"));
        }
    }

    virtual void handleMessage(cMessage *msg) override {
        FiveGBlock *pkt = check_and_cast<FiveGBlock *>(msg);
        simtime_t delay = simTime() - pkt->getCreationTime();

        if (pkt->getTrafficClass() == 0) {
            totalEmbbDelivered++;
            totalEmbbDelaySum += delay;
            int u = pkt->getUserId();
            if (u >= 0 && u < numUsers) {
                userBlocksDelivered[u]++;
                userDelaySum[u] += delay;
            }
            emit(embbDelaySignal, delay);
            emit(embbRetxCountSignal, pkt->getRetxCount());
        } else {
            totalUrllcDelivered++;
            totalUrllcDelaySum += delay;
            emit(urllcDelaySignal, delay);
        }
        delete pkt;
    }

    virtual void refreshDisplay() const override {
        double embbAvgMs = (totalEmbbDelivered > 0) ? (totalEmbbDelaySum.dbl() * 1000.0 / totalEmbbDelivered) : 0.0;
        double urllcAvgMs = (totalUrllcDelivered > 0) ? (totalUrllcDelaySum.dbl() * 1000.0 / totalUrllcDelivered) : 0.0;

        getDisplayString().setTagArg("t", 0, "5G gNB Receiver");
        getDisplayString().setTagArg("t", 2, "#581c87");

        if (sinkStatsFigure != nullptr) {
            char cardBuf[256];
            std::snprintf(cardBuf, sizeof(cardBuf),
                "gNB RECEIVER TELEMETRY\neMBB Blocks : %ld (%.2f ms)\nURLLC Blocks: %ld (%.3f ms)\nPunctures   : %ld | Retx: %ld",
                totalEmbbDelivered, embbAvgMs, totalUrllcDelivered, urllcAvgMs,
                PrbChannel::getTotalPuncturesGlobal(), EmbbUserBuffer::getTotalRetxGlobal());
            sinkStatsFigure->setText(cardBuf);
        }

        if (hudFigure != nullptr) {
            char hudBuf[256];
            std::snprintf(hudBuf, sizeof(hudBuf),
                "t = %.2f ms | eMBB: %ld (%.2f ms) | URLLC: %ld (%.3f ms) | Punctured: %ld | High-Prio Retx: %ld",
                simTime().dbl() * 1000.0, totalEmbbDelivered, embbAvgMs,
                totalUrllcDelivered, urllcAvgMs,
                PrbChannel::getTotalPuncturesGlobal(), EmbbUserBuffer::getTotalRetxGlobal());
            hudFigure->setText(hudBuf);
        }
    }

    virtual void finish() override {
        recordScalar("Total_eMBB_Blocks_Delivered", totalEmbbDelivered);
        recordScalar("Total_URLLC_Blocks_Delivered", totalUrllcDelivered);

        double sysEmbbMeanMs = (totalEmbbDelivered > 0)
                               ? (totalEmbbDelaySum.dbl() * 1000.0 / totalEmbbDelivered) : 0.0;
        double sysUrllcMeanMs = (totalUrllcDelivered > 0)
                                ? (totalUrllcDelaySum.dbl() * 1000.0 / totalUrllcDelivered) : 0.0;
        recordScalar("System_eMBB_Mean_Delay_ms", sysEmbbMeanMs);
        recordScalar("System_URLLC_Mean_Delay_ms", sysUrllcMeanMs);

        for (int u = 0; u < numUsers; u++) {
            std::string prefix = "User_" + std::to_string(u) + "_";
            recordScalar((prefix + "Blocks_Delivered").c_str(), userBlocksDelivered[u]);
            double meanDelayMs = (userBlocksDelivered[u] > 0)
                                 ? (userDelaySum[u].dbl() * 1000.0 / userBlocksDelivered[u]) : 0.0;
            recordScalar((prefix + "Mean_Delay_ms").c_str(), meanDelayMs);
        }
    }
};

Define_Module(RadioSink);
