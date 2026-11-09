#include "ChronosNRModules.h"
#include <cstdio>

Define_Module(PrbChannel);

void PrbChannel::initialize() {
  prbIndex = par("prbIndex");
  numUsers = par("numUsers");
  embbSlotDuration = par("embbSlotDuration");
  urllcMinislotDuration = par("urllcMinislotDuration");

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
  } else if (msg == endEmbbSlotTimer) {
    if (currentEmbbCopy != nullptr) {
      int ownerUser = currentEmbbCopy->getUserId();
      PrbFeedback *fb = new PrbFeedback(overwrittenByUrllc ? "NACK_OVERWRITTEN"
                                                           : "ACK_INTACT");
      fb->setSchedulingPriority(1);
      fb->setPrbIndex(prbIndex);
      fb->setUserId(ownerUser);
      fb->setBlockId(currentEmbbCopy->getBlockId());
      fb->setOverwritten(overwrittenByUrllc);
      send(fb, "feedbackOut", ownerUser);

      if (!overwrittenByUrllc) {
        send(currentEmbbCopy, "out");
      } else {
        delete currentEmbbCopy;
      }
      currentEmbbCopy = nullptr;
      overwrittenByUrllc = false;
      overwriteCountThisSlot = 0;
    }
  } else if (msg->arrivedOn("embbIn")) {
    FiveGBlock *embbCopy = check_and_cast<FiveGBlock *>(msg);
    if (currentEmbbCopy != nullptr)
      delete currentEmbbCopy;
    currentEmbbCopy = embbCopy;
    overwrittenByUrllc = false;
    overwriteCountThisSlot = 0;
    scheduleAt(simTime() + embbSlotDuration, endEmbbSlotTimer);
  } else if (msg->arrivedOn("urllcIn")) {
    FiveGBlock *urllcPkt = check_and_cast<FiveGBlock *>(msg);
    if (currentUrllcBlock != nullptr) {
      send(currentUrllcBlock, "out");
    }
    currentUrllcBlock = urllcPkt;

    if (currentEmbbCopy != nullptr) {
      overwrittenByUrllc = true;
      overwriteCountThisSlot++;
      currentEmbbCopy->setOverwritten(true);
      EV_WARN << "[PRB " << prbIndex << "] URLLC Block #"
              << urllcPkt->getBlockId() << " OVERWROTE User "
              << currentEmbbCopy->getUserId() << "'s eMBB Copy #"
              << currentEmbbCopy->getBlockId() << " at t=" << simTime() << "\n";
    } else {
      EV_INFO << "[PRB " << prbIndex << "] URLLC Block #"
              << urllcPkt->getBlockId()
              << " scheduled in FREE PRB at t=" << simTime() << "\n";
    }
    scheduleAt(simTime() + urllcMinislotDuration, endUrllcMinislotTimer);
  }
}

void PrbChannel::refreshDisplay() const {
  char buf[64];
  const char *color = "green";
  if (currentEmbbCopy != nullptr && overwrittenByUrllc) {
    std::snprintf(buf, sizeof(buf), "PUNCTURED U%d (x%d)",
                  currentEmbbCopy->getUserId(), overwriteCountThisSlot);
    color = "red";
  } else if (currentUrllcBlock != nullptr) {
    std::snprintf(buf, sizeof(buf), "FREE->URLLC");
    color = "cyan";
  } else if (currentEmbbCopy != nullptr) {
    std::snprintf(buf, sizeof(buf), "eMBB U%d #%ld",
                  currentEmbbCopy->getUserId(), currentEmbbCopy->getBlockId());
    color = "blue";
  } else {
    std::snprintf(buf, sizeof(buf), "FREE");
    color = "green";
  }
  getDisplayString().setTagArg("t", 0, buf);
  getDisplayString().setTagArg("i", 1, color);
}

void PrbChannel::finish() {
  cancelAndDelete(endEmbbSlotTimer);
  cancelAndDelete(endUrllcMinislotTimer);
  if (currentEmbbCopy != nullptr)
    delete currentEmbbCopy;
  if (currentUrllcBlock != nullptr)
    delete currentUrllcBlock;
}
