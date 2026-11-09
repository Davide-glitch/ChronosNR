package chronosnr.src;

simple PacketGenerator
{
    parameters:
        int trafficClass; // 0 = eMBB, 1 = URLLC
        int userId;
        int userWeight = default(1);
        volatile double interArrivalTime @unit(ms);
        @display("i=block/source");
    gates:
        output out;
}

simple EmbbUserBuffer
{
    parameters:
        int userId;
        int userWeight = default(1);
        int numPRBs = default(10);
        @display("i=block/queue");
        @signal[retxQueuedSignal](type="long");
        @signal[keptDroppedSignal](type="long");
        @statistic[embbRetransmissionsQueued](title="Overwritten eMBB Blocks Moved to High-Prio Retx"; source="retxQueuedSignal"; record=count,vector);
        @statistic[embbKeptOriginalsDropped](title="Intact eMBB Kept Originals Dropped"; source="keptDroppedSignal"; record=count,vector);
    gates:
        input in;
        input grantIn;
        input feedbackIn[numPRBs];
        output prbOut[numPRBs];
}

module EmbbUser
{
    parameters:
        int userId;
        int userWeight = default(1);
        int numPRBs = default(10);
        @display("i=device/pocketpc;bgb=210,120");
    gates:
        input grantIn;
        input feedbackIn[numPRBs];
        output prbOut[numPRBs];
    submodules:
        gen: PacketGenerator {
            trafficClass = 0;
            userId = parent.userId;
            userWeight = parent.userWeight;
            @display("p=50,60");
        }
        buffer: EmbbUserBuffer {
            userId = parent.userId;
            userWeight = parent.userWeight;
            numPRBs = parent.numPRBs;
            @display("p=150,60");
        }
    connections:
        gen.out --> buffer.in;
        grantIn --> buffer.grantIn;
        for b=0..numPRBs-1 {
            feedbackIn[b] --> buffer.feedbackIn[b];
            buffer.prbOut[b] --> prbOut[b];
        }
}

simple UrllcQueueModule
{
    parameters:
        int numPRBs = default(10);
        @display("i=block/queue");
    gates:
        input in;
        input grantIn;
        output prbOut[numPRBs];
}

module UrllcUser
{
    parameters:
        int numPRBs = default(10);
        @display("i=device/Cpu;bgb=210,120");
    gates:
        input grantIn;
        output prbOut[numPRBs];
    submodules:
        gen: PacketGenerator {
            trafficClass = 1;
            userId = 0;
            userWeight = 10;
            @display("p=50,60");
        }
        buffer: UrllcQueueModule {
            numPRBs = parent.numPRBs;
            @display("p=150,60");
        }
    connections:
        gen.out --> buffer.in;
        grantIn --> buffer.grantIn;
        for b=0..numPRBs-1 {
            buffer.prbOut[b] --> prbOut[b];
        }
}

simple EmbbScheduler
{
    parameters:
        int numUsers = default(5);
        int numPRBs = default(10);
        double cycle @unit(ms) = default(1.0ms);
        string schedulingPolicy = default("WRR"); // "WRR", "PF", "MAX_CI", or "RR"
        @display("i=block/control");
    gates:
        output grantOut[numUsers];
}

simple UrllcScheduler
{
    parameters:
        int numPRBs = default(10);
        double cycle @unit(ms) = default(0.1ms);
        @display("i=block/control");
        @signal[urllcFreeChannelSignal](type="long");
        @signal[urllcPunctureChannelSignal](type="long");
        @statistic[urllcScheduledInFreePRB](title="URLLC Scheduled in Free PRB"; source="urllcFreeChannelSignal"; record=count,vector);
        @statistic[urllcScheduledByPuncturing](title="URLLC Scheduled by Overwriting eMBB PRB"; source="urllcPunctureChannelSignal"; record=count,vector);
    gates:
        output grantOut;
}

simple PrbChannel
{
    parameters:
        int prbIndex;
        int numUsers = default(5);
        double embbSlotDuration @unit(ms) = default(1.0ms);
        double urllcMinislotDuration @unit(ms) = default(0.1ms);
        @display("i=block/routing");
    gates:
        input embbIn[numUsers];
        input urllcIn;
        output feedbackOut[numUsers];
        output out;
}

simple RadioSink
{
    parameters:
        int numPRBs = default(10);
        int numUsers = default(5);
        @display("i=block/sink");
        @signal[embbDelaySignal](type="simtime_t");
        @signal[urllcDelaySignal](type="simtime_t");
        @signal[embbRetxCountSignal](type="long");
        @statistic[embbDelay](title="eMBB End-to-End Delay"; source="embbDelaySignal"; record=mean,min,max,vector,histogram);
        @statistic[urllcDelay](title="URLLC End-to-End Delay"; source="urllcDelaySignal"; record=mean,min,max,vector,histogram);
        @statistic[embbRetxPerDeliveredBlock](title="Retransmissions per Delivered eMBB Block"; source="embbRetxCountSignal"; record=mean,max,histogram);
    gates:
        input in[numPRBs];
}

network ChronosNRNetwork
{
    parameters:
        int numUsers = default(5);
        int numPRBs = default(10);
        @display("bgb=900,560");
    submodules:
        embbUser[numUsers]: EmbbUser {
            userId = index;
            numPRBs = parent.numPRBs;
            userWeight = (index == 0) ? 6 : ((index <= 2) ? 2 : 1); // HP=6, MP=2, LP=1
            @display("p=110,70,c,75");
        }
        urllcUser: UrllcUser {
            numPRBs = parent.numPRBs;
            @display("p=110,480");
        }
        embbScheduler: EmbbScheduler {
            numUsers = parent.numUsers;
            numPRBs = parent.numPRBs;
            @display("p=310,60");
        }
        urllcScheduler: UrllcScheduler {
            numPRBs = parent.numPRBs;
            @display("p=310,480");
        }
        prb[numPRBs]: PrbChannel {
            prbIndex = index;
            numUsers = parent.numUsers;
            @display("p=540,60,c,45");
        }
        sink: RadioSink {
            numPRBs = parent.numPRBs;
            numUsers = parent.numUsers;
            @display("p=780,270");
        }
    connections:
        for u=0..numUsers-1 {
            embbScheduler.grantOut[u] --> embbUser[u].grantIn;
        }
        urllcScheduler.grantOut --> urllcUser.grantIn;

        for u=0..numUsers-1 {
            for b=0..numPRBs-1 {
                embbUser[u].prbOut[b] --> prb[b].embbIn[u];
                prb[b].feedbackOut[u] --> embbUser[u].feedbackIn[b];
            }
        }

        for b=0..numPRBs-1 {
            urllcUser.prbOut[b] --> prb[b].urllcIn;
            prb[b].out --> sink.in[b];
        }
}
