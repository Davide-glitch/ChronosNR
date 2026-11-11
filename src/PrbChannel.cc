#include "ChronosNRModules.h"
#include <cstdio>

long PrbChannel::totalPuncturesGlobal = 0;
Define_Module(PrbChannel);

void PrbChannel::initialize() {
    prbIndex = par("prbIndex");
    numUsers = par("numUsers");
    embbSlotDuration = SimTime(par("embbSlotDuration").doubleValueInUnit("s"));
    urllcMinislotDuration = SimTime(par("urllcMinislotDuration").doubleValueInUnit("s"));
    if (prbIndex == 0) totalPuncturesGlobal = 0;

    endEmbbSlotTimer = new cMessage("endEmbbSlotTimer");
    endEmbbSlotTimer->setSchedulingPriority(0);

    endUrllcMinislotTimer = new cMessage("endUrllcMinislotTimer");
    endUrllcMinislotTimer->setSchedulingPriority(0);
}

void PrbChannel::handleMessage(cMessage *msg) {
    if (msg == endUrllcMinislotTimer) {
        if (currentUrllcBlock != nullptr) {
            send(currentUrllcBlock, "out");
            currentUrllcBlock = nullptr;
        }
    }
    else if (msg == endEmbbSlotTimer) {
        if (currentEmbbCopy != nullptr) {
            int ownerUser = currentEmbbCopy->getUserId();
            bool failed = overwrittenByUrllc || currentEmbbCopy->getOverwritten();
            PrbFeedback *fb = new PrbFeedback(failed ? "NACK_OVERWRITTEN" : "ACK_INTACT");
            fb->setDisplayString(failed ? "b=12,12,oval,#f97316,#ea580c,2" : "b=10,10,oval,#22c55e,#16a34a,1");
            fb->setSchedulingPriority(1);
            fb->setPrbIndex(prbIndex);
            fb->setUserId(ownerUser);
            fb->setBlockId(currentEmbbCopy->getBlockId());
            fb->setOverwritten(failed);
            send(fb, "feedbackOut", ownerUser);

            if (!failed) {
                send(currentEmbbCopy, "out");
            } else {
                delete currentEmbbCopy;
            }
            currentEmbbCopy = nullptr;
            overwrittenByUrllc = false;
            overwriteCountThisSlot = 0;
        }
    }
    else if (msg->arrivedOn("embbIn")) {
        FiveGBlock *embbCopy = check_and_cast<FiveGBlock *>(msg);
        if (currentEmbbCopy != nullptr) delete currentEmbbCopy;
        currentEmbbCopy = embbCopy;
        overwrittenByUrllc = false;
        overwriteCountThisSlot = 0;
        scheduleAt(simTime() + embbSlotDuration, endEmbbSlotTimer);
    }
    else if (msg->arrivedOn("urllcIn")) {
        FiveGBlock *urllcPkt = check_and_cast<FiveGBlock *>(msg);
        if (currentUrllcBlock != nullptr) {
            send(currentUrllcBlock, "out");
        }
        currentUrllcBlock = urllcPkt;

        if (currentEmbbCopy != nullptr) {
            if (!overwrittenByUrllc) {
                totalPuncturesGlobal++;
            }
            overwrittenByUrllc = true;
            overwriteCountThisSlot++;
            currentEmbbCopy->setOverwritten(true);
            if (hasGUI()) {
                bubble("PUNCTURED eMBB!");
            }
            EV_WARN << "[PRB " << prbIndex << "] URLLC Block #" << urllcPkt->getBlockId()
                    << " OVERWROTE User " << currentEmbbCopy->getUserId()
                    << "'s eMBB Copy #" << currentEmbbCopy->getBlockId()
                    << " at t=" << simTime() << "\n";
        } else {
            if (hasGUI()) {
                bubble("FREE PRB -> URLLC");
            }
            EV_INFO << "[PRB " << prbIndex << "] URLLC Block #" << urllcPkt->getBlockId()
                    << " scheduled in FREE PRB at t=" << simTime() << "\n";
        }
        scheduleAt(simTime() + urllcMinislotDuration, endUrllcMinislotTimer);
    }
}

void PrbChannel::refreshDisplay() const {
    char buf[64];
    const char *color = "#22c55e";
    if (currentEmbbCopy != nullptr && overwrittenByUrllc) {
        std::snprintf(buf, sizeof(buf), "PUNCTURED U%d (x%d)", currentEmbbCopy->getUserId(), overwriteCountThisSlot);
        color = "#ef4444";
    } else if (currentUrllcBlock != nullptr) {
        std::snprintf(buf, sizeof(buf), "FREE -> URLLC");
        color = "#06b6d4";
    } else if (currentEmbbCopy != nullptr) {
        std::snprintf(buf, sizeof(buf), "eMBB U%d #%ld", currentEmbbCopy->getUserId(), currentEmbbCopy->getBlockId());
        color = "#3b82f6";
    } else {
        std::snprintf(buf, sizeof(buf), "FREE");
        color = "#22c55e";
    }
    getDisplayString().setTagArg("t", 0, buf);
    getDisplayString().setTagArg("t", 2, color);
    getDisplayString().setTagArg("i", 1, color);
}

void PrbChannel::finish() {
    cancelAndDelete(endEmbbSlotTimer);
    cancelAndDelete(endUrllcMinislotTimer);
    endEmbbSlotTimer = nullptr;
    endUrllcMinislotTimer = nullptr;
    if (currentEmbbCopy != nullptr) {
        delete currentEmbbCopy;
        currentEmbbCopy = nullptr;
    }
    if (currentUrllcBlock != nullptr) {
        delete currentUrllcBlock;
        currentUrllcBlock = nullptr;
    }
}
