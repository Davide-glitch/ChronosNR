#include "ChronosNRModules.h"
#include <cstdio>

class EmbbScheduler : public cSimpleModule {
  private:
    int numUsers = 5;
    int numPRBs = 10;
    simtime_t cycle;
    std::string schedulingPolicy;
    cMessage *cycleTimer = nullptr;

    std::vector<EmbbUserBuffer*> userBuffers;
    std::vector<int> virtualQueueCredits;
    std::vector<int> virtualRetxPending;
    std::vector<int> virtualWaitPending;
    std::vector<double> userCqi;
    std::vector<double> avgRate;
    int rrPointer = 0;

  protected:
    virtual void initialize() override {
        numUsers = par("numUsers");
        numPRBs = par("numPRBs");
        cycle = par("cycle");
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
            runEmbbSchedulingCycle();
            scheduleAt(simTime() + cycle, cycleTimer);
        }
    }

    void runEmbbSchedulingCycle() {
        for (int u = 0; u < numUsers; u++) {
            virtualRetxPending[u] = userBuffers[u]->getRetxQueueLength();
            virtualWaitPending[u] = userBuffers[u]->getWaitingQueueLength();
            userCqi[u] = (u < numUsers / 2) ? intuniform(4, 10) : intuniform(1, 5);
        }

        for (int b = 0; b < numPRBs; b++) {
            PrbChannel *prbMod = check_and_cast<PrbChannel *>(getParentModule()->getSubmodule("prb", b));
            if (!prbMod->isCompletelyFree()) {
                continue;
            }

            int chosenUser = -1;

            for (int i = 0; i < numUsers; i++) {
                int u = (rrPointer + i) % numUsers;
                if (virtualRetxPending[u] > 0) {
                    chosenUser = u;
                    virtualRetxPending[u]--;
                    rrPointer = (u + 1) % numUsers;
                    break;
                }
            }

            if (chosenUser == -1) {
                chosenUser = selectNormalUser();
                if (chosenUser != -1) {
                    virtualWaitPending[chosenUser]--;
                }
            }

            if (chosenUser == -1) {
                break;
            }

            SchedGrant *grant = new SchedGrant("eMBB_Grant");
            grant->setSchedulingPriority(2);
            grant->setAssignedPrb(b);
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
                    double m = (schedulingPolicy == "MAX_CI") ? userCqi[u] : (userCqi[u] / avgRate[u]);
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

    virtual void finish() override {
        cancelAndDelete(cycleTimer);
    }
};

Define_Module(EmbbScheduler);

class UrllcScheduler : public cSimpleModule {
  private:
    int numPRBs = 10;
    simtime_t cycle;
    cMessage *cycleTimer = nullptr;
    UrllcQueueModule *urllcBuffer = nullptr;

    simsignal_t urllcFreeChannelSignal;
    simsignal_t urllcPunctureChannelSignal;

  protected:
    virtual void initialize() override {
        numPRBs = par("numPRBs");
        cycle = par("cycle");
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
                emit(urllcFreeChannelSignal, (long)1);

                SchedGrant *grant = new SchedGrant("URLLC_Free_Grant");
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
                int randIdx = intuniform(0, (int)occupiedCandidates.size() - 1);
                targetPrb = occupiedCandidates[randIdx];
                assignedThisMinislot[targetPrb] = true;
                pendingUrllc--;
                emit(urllcPunctureChannelSignal, (long)1);

                SchedGrant *grant = new SchedGrant("URLLC_Puncture_Grant");
                grant->setSchedulingPriority(4);
                grant->setAssignedPrb(targetPrb);
                send(grant, "grantOut");
            } else {
                break;
            }
        }
    }

    virtual void finish() override {
        cancelAndDelete(cycleTimer);
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

  protected:
    virtual void initialize() override {
        numUsers = par("numUsers");
        embbDelaySignal = registerSignal("embbDelaySignal");
        urllcDelaySignal = registerSignal("urllcDelaySignal");
        embbRetxCountSignal = registerSignal("embbRetxCountSignal");
        userBlocksDelivered.assign(numUsers, 0);
        userDelaySum.assign(numUsers, SIMTIME_ZERO);
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
            emit(embbRetxCountSignal, (long)pkt->getRetxCount());
        } else {
            totalUrllcDelivered++;
            totalUrllcDelaySum += delay;
            emit(urllcDelaySignal, delay);
        }
        delete pkt;
    }

    virtual void refreshDisplay() const override {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "eMBB:%ld | URLLC:%ld", totalEmbbDelivered, totalUrllcDelivered);
        getDisplayString().setTagArg("t", 0, buf);
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
