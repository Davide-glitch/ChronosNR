message FiveGBlock {
    string displayString = "";
    int trafficClass;          // 0 = eMBB, 1 = URLLC
    long blockId;              // Unique radio block ID
    int userId;                // User index
    int userWeight = 1;        // User priority weight W[i]
    int assignedPrb = -1;      // Allocated PRB index (0 .. numPRBs-1)
    int retxCount = 0;         // Number of high-priority retransmissions
    bool isCopy = false;       // True when sent as a copy over PRB
    bool overwritten = false;  // True if punctured by URLLC or deep fade during the 1ms slot
    simtime_t creationTime;    // Timestamp for end-to-end delay calculation
}

message SchedGrant {
    string displayString = "";
    int assignedPrb;           // Which PRB channel the user should send its block on
    bool deepFade = false;     // True if scheduled user had poor instantaneous channel state
}

message PrbFeedback {
    string displayString = "";
    int prbIndex;
    int userId;
    long blockId;
    bool overwritten;          // True = punctured/erased (retransmit with high priority!), False = drop kept block
}
