package chronosnr.src;

simple PacketGenerator
{
    parameters:
        int trafficClass; // 0 = eMBB, 1 = URLLC
        int userId;
        int userWeight = default(1);
        volatile double interArrivalTime @unit(s);
        @display("i=block/source,#3b82f6");
    gates:
        output out;
}

simple EmbbUserBuffer
{
    parameters:
        int userId;
        int userWeight = default(1);
        int numPRBs = default(10);
        @display("i=block/queue,#2563eb");
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
        @display("i=device/pocketpc,#2563eb;bgb=230,120;bgg=50,2,grey90");
    gates:
        input grantIn;
        input feedbackIn[numPRBs];
        output prbOut[numPRBs];
    submodules:
        gen: PacketGenerator {
            trafficClass = 0;
            userId = parent.userId;
            userWeight = parent.userWeight;
            @display("p=55,60");
        }
        buffer: EmbbUserBuffer {
            userId = parent.userId;
            userWeight = parent.userWeight;
            numPRBs = parent.numPRBs;
            @display("p=165,60");
        }
    connections:
        gen.out --> { @display("ls=#2563eb,2"); } --> buffer.in;
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
        @display("i=block/queue,#dc2626");
    gates:
        input in;
        input grantIn;
        output prbOut[numPRBs];
}

module UrllcUser
{
    parameters:
        int numPRBs = default(10);
        @display("i=device/server2,#dc2626;bgb=230,120;bgg=50,2,grey90");
    gates:
        input grantIn;
        output prbOut[numPRBs];
    submodules:
        gen: PacketGenerator {
            trafficClass = 1;
            userId = 0;
            userWeight = 10;
            @display("p=55,60;i=block/source,#dc2626");
        }
        buffer: UrllcQueueModule {
            numPRBs = parent.numPRBs;
            @display("p=165,60");
        }
    connections:
        gen.out --> { @display("ls=#dc2626,2"); } --> buffer.in;
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
        double cycle @unit(s) = default(1.0ms);
        string schedulingPolicy = default("WRR"); // "WRR", "PF", "MAX_CI", or "RR"
        @display("i=block/control,#d97706");
    gates:
        output grantOut[numUsers];
}

simple UrllcScheduler
{
    parameters:
        int numPRBs = default(10);
        double cycle @unit(s) = default(0.1ms);
        @display("i=block/control,#dc2626");
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
        double embbSlotDuration @unit(s) = default(1.0ms);
        double urllcMinislotDuration @unit(s) = default(0.1ms);
        @display("i=block/routing,#16a34a");
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
        @display("i=device/antennatower,#7c3aed;is=l");
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
        @display("bgb=1150,700;bgl=2");

        // --- Visual Dashboard Figures on Qtenv Canvas ---
        @figure[headerBox](type=rectangle; pos=20,12; size=1110,62; fillColor=#0f172a; fillOpacity=0.92; lineColor=#38bdf8; lineWidth=2; cornerRadius=8; zIndex=-2);
        @figure[titleText](type=text; pos=40,22; text="ChronosNR: 5G New Radio Dual-Timescale eMBB (1.0 ms) & URLLC (0.1 ms) MAC Scheduler"; font=Arial,14,bold; color=#f8fafc);
        @figure[hudText](type=text; pos=40,46; text="Initializing 5G NR Telemetry HUD..."; font=Consolas,11,bold; color=#38bdf8);

        @figure[zoneUsers](type=rectangle; pos=20,90; size=400,590; fillColor=#eff6ff; fillOpacity=0.65; lineColor=#93c5fd; lineWidth=2; cornerRadius=10; zIndex=-2);
        @figure[zoneUsersTitle](type=text; pos=35,100; text="1. UE TRAFFIC QUEUES & DUAL SCHEDULERS"; font=Arial,10,bold; color=#1e3a8a);

        @figure[zonePrbs](type=rectangle; pos=450,90; size=370,590; fillColor=#f0fdf4; fillOpacity=0.65; lineColor=#86efac; lineWidth=2; cornerRadius=10; zIndex=-2);
        @figure[zonePrbsTitle](type=text; pos=470,100; text="2. SHARED RADIO CHANNELS (PRB 0..9)"; font=Arial,10,bold; color=#14532d);

        @figure[zoneSink](type=rectangle; pos=850,90; size=280,590; fillColor=#faf5ff; fillOpacity=0.65; lineColor=#d8b4fe; lineWidth=2; cornerRadius=10; zIndex=-2);
        @figure[zoneSinkTitle](type=text; pos=870,100; text="3. 5G NR gNB RECEIVER SINK"; font=Arial,10,bold; color=#581c87);

        @figure[legendText](type=text; pos=870,480; text="PRB COLOR LEGEND:\n- GREEN : FREE Channel\n- BLUE   : eMBB Copy In-Flight\n- CYAN  : URLLC on Free PRB\n- RED    : PUNCTURED by URLLC!\n\nQUEUE FORMAT:\nW=Weight | wait | kept | retx"; font=Consolas,9; color=#334155);

    submodules:
        embbScheduler: EmbbScheduler {
            numUsers = parent.numUsers;
            numPRBs = parent.numPRBs;
            @display("p=110,160");
        }
        embbUser[numUsers]: EmbbUser {
            userId = index;
            numPRBs = parent.numPRBs;
            userWeight = (index == 0) ? 6 : ((index <= 2) ? 2 : 1); // HP=6, MP=2, LP=1
            @display("p=300,155,c,82");
        }
        urllcScheduler: UrllcScheduler {
            numPRBs = parent.numPRBs;
            @display("p=110,610");
        }
        urllcUser: UrllcUser {
            numPRBs = parent.numPRBs;
            @display("p=300,610");
        }
        prb[numPRBs]: PrbChannel {
            prbIndex = index;
            numUsers = parent.numUsers;
            @display("p=635,145,c,50");
        }
        sink: RadioSink {
            numPRBs = parent.numPRBs;
            numUsers = parent.numUsers;
            @display("p=990,340");
        }
    connections:
        for u=0..numUsers-1 {
            embbScheduler.grantOut[u] --> { @display("ls=#f59e0b,2,s"); } --> embbUser[u].grantIn;
        }
        urllcScheduler.grantOut --> { @display("ls=#dc2626,2,s"); } --> urllcUser.grantIn;

        for u=0..numUsers-1, for b=0..numPRBs-1 {
            embbUser[u].prbOut[b] --> { @display("ls=#94a3b8,1,d"); } --> prb[b].embbIn[u];
            prb[b].feedbackOut[u] --> { @display("ls=#cbd5e1,1,d"); } --> embbUser[u].feedbackIn[b];
        }

        for b=0..numPRBs-1 {
            urllcUser.prbOut[b] --> { @display("ls=#fca5a5,1"); } --> prb[b].urllcIn;
            prb[b].out --> { @display("ls=#8b5cf6,2"); } --> sink.in[b];
        }
}
