#include <udt.h>
#include <ccc.h>
#include <fstream>
#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
using namespace std;


static const double bbr_high_gain = 739.0 / (1 << 8);
// static const double bbr_high_gain = 2.0;

static const double bbr_drain_gain = 88.0 / (1 << 8);
static const double bbr_full_bw_thresh = 5/4.0;
static const uint32_t bbr_full_bw_cnt = 3;
static const double bbr_cwnd_gain = 2.0;
double temp = 1.0;
static const double bbr_pacing_gain[] = {1.25, 0.75, temp, temp, temp, temp, temp, temp};
static const uint32_t bbr_min_rtt_win_sec = 10; // 10s
static const uint32_t bbr_probe_rtt_mode_ms = 200;
#define CYCLE_LEN	8	/* number of phases in a pacing gain cycle */
double start_up_max_bw;
/* Pace at ~1% below estimated bw, on average, to reduce queue at bottleneck.
 * In order to help drive the network toward lower queues and low latency while
 * maintaining high utilization, the average pacing rate aims to be slightly
 * lower than the estimated bandwidth. This is an important aspect of the
 * design.BBR的平均paceing rate略微小于估计的可用带宽
 */
static const double bbr_pacing_margin_percent = 0.99;
static const double bbr_cwnd_min_target = 500.0;

uint64_t getTime(){
    timeval t;
    gettimeofday(&t, 0);
    return t.tv_sec * 1000000ULL + t.tv_usec;
}

struct BBR {
    uint32_t	min_rtt_us;	        /* min RTT in min_rtt_win_sec window 在rtt窗口中的最小rtt*/
    uint64_t	min_rtt_stamp;	        /* timestamp of min_rtt_us 最小rtt对应的时间戳*/
    uint32_t	probe_rtt_done_stamp;   /* end time for BBR_PROBE_RTT mode 探测rtt模式的结束时间*/
    double      bw;	/* Max recent delivery rate in pkts/uS << 24 在bw窗口内的最大传输速率*/
    uint32_t	rtt_cnt;	    /* count of packet-timed rounds elapsed，过去了多少个rtt */
    uint32_t     next_rtt_delivered; /* scb->tx.delivered at end of round */
    uint64_t	cycle_mstamp;	     /* time of this cycle phase start 本次循环开始时间*/
    uint32_t     mode:3,		     /* current bbr_mode in state machine BBR目前的状态*/
        prev_ca_state:3,     /* CA state on previous ACK */
        packet_conservation:1,  /* use packet conservation? */
        round_start:1,	     /* start of packet-timed tx->ack round? */
        idle_restart:1,	     /* restarting after idle? */
        probe_rtt_round_done:1,  /* a BBR_PROBE_RTT round at 4 pkts? */
        unused:13,
        lt_is_sampling:1,    /* taking long-term ("LT") samples now? 是否开启长期采样*/
        lt_rtt_cnt:7,	     /* round trips in long-term interval 一个长期采样的间隔中有几个rtt*/
        lt_use_bw:1;	     /* use lt_bw as our bw estimate? 是否使用lt-bw作为估计的带宽*/
    uint32_t	lt_bw;		     /* LT est delivery rate in pkts/uS << 24 长期采样估计的传输速率*/
    uint32_t	lt_last_delivered;   /* LT intvl start: tp->delivered 上一个采样周期开始时的delivered*/
    uint32_t	lt_last_stamp;	     /* LT intvl start: tp->delivered_mstamp 上一个周期的开始时间*/
    uint32_t	lt_last_lost;	     /* LT intvl start: tp->lost 上一个长期采样的丢包率*/
    double	    pacing_gain;	/* current gain for setting pacing rate */
    double      cwnd_gain;	/* current gain for setting cwnd */
    uint32_t    full_bw_reached:1,   /* reached full bw in Startup? start up阶段是否达到最大带宽*/
        full_bw_cnt:2,	/* number of rounds without large bw gains 没有获得带宽大幅度增长的回合数*/
        cycle_idx:3,	/* current index in pacing_gain cycle array */
        has_seen_rtt:1; /* have we seen an RTT sample yet?  是否已经有rtt的测量值*/

    double	prior_cwnd;	/* prior cwnd upon entering loss recovery */
    double	full_bw;	/* recent bw, to estimate if pipe is full 估算的带宽值*/

    /* For tracking ACK aggregation: */
    uint64_t	ack_epoch_mstamp;	/* start of ACK sampling epoch */
    uint16_t	extra_acked[2];		/* max excess data ACKed in epoch */
    uint32_t	ack_epoch_acked:20,	/* packets (S)ACKed in sampling epoch 在采样epoch内被确认的数据包*/
        extra_acked_win_rtts:5,	/* age of extra_acked, in round trips */
        extra_acked_win_idx:1,	/* current index in extra_acked array */
        unused_c:6;
};

struct RecordUint{
    int32_t SeqNo;
    uint64_t TimeStamp;
    int64_t pktDelivered; //TODO: 这里目前采用的是收到的ack数目
};

enum bbr_mode {
	BBR_STARTUP,	/* ramp up sending rate rapidly to fill pipe */
	BBR_DRAIN,	/* drain any queue created during startup */
	BBR_PROBE_BW,	/* discover, share bw: pace around estimated bw */
	BBR_PROBE_RTT,	/* cut inflight to min to probe min_rtt */
};



class CBBR: public CCC
{
public:
    CBBR(){
        if_write = true;
        if(if_write){
            outfile.open("file.dat", ios::out | ios::app);
            outfile1.open("file1.dat", ios::out | ios::app);
        }

    }
    ~CBBR(){
        outfile.close();
        outfile1.close();
    }
    void init(){

        // m_dPktSndPeriod = (m_iMSS * 8.0) / 6.5;  // m_iMSS 1500 rate = 500 Mbps
        m_dCWndSize = 10.0; 
        setACKInterval(1);
        bbr.has_seen_rtt = 0;  // 目前是否对rtt进行过采样
        this->bbr_init_pacing_rate_from_rtt();
        
        record_index = 0;
        bbr.mode = BBR_STARTUP;
        memset(BW_Record, 0, sizeof(double)*10);



        bbr.prior_cwnd = 0;
        bbr.rtt_cnt = 0;
        bbr.next_rtt_delivered = 1;  // Total data packets delivered incl. rexmits，
                                                //交付的总数据，包括重传 初始值为1
        bbr.packet_conservation = 0; // 关闭包保护

        bbr.probe_rtt_done_stamp = 0;
        bbr.probe_rtt_round_done = 0; //是否处于rtt探测阶段
        bbr.min_rtt_us = m_iRTT;  // Minimum RTT in usec. ~0 means not available.
        bbr.min_rtt_stamp = getTime();

        bbr.round_start = 0;
        bbr.idle_restart = 0;
        bbr.full_bw_reached = 0;
        bbr.full_bw = 0;
        bbr.full_bw_cnt = 0;
        bbr.cycle_mstamp = 0;
        bbr.cycle_idx = 0;

        
        // bbr.ack_epoch_mstamp = tp.tcp_mstamp;  	/* most recent packet received/sent */
        bbr.ack_epoch_acked = 0;
        bbr.extra_acked_win_rtts = 0;
        bbr.extra_acked_win_idx = 0;
        bbr.extra_acked[0] = 0;
        bbr.extra_acked[1] = 0;
    }

    void bbr_init_pacing_rate_from_rtt(){
        double rate;
        if (m_iRTT != 100000) {		/* any RTT sample yet? 平滑往返时间 */
            rate = m_dCWndSize * m_iMSS * 8 / m_iRTT  * bbr_high_gain ; //Mbps
            bbr.has_seen_rtt = 1;
        } else {			 /* no RTT sample yet */
            rate = m_dCWndSize * m_iMSS * 8 / 1000  * bbr_high_gain ; //Mbps
        }
        rate *= bbr_pacing_margin_percent;
        m_dPktSndPeriod = (m_iMSS * 8.0) / rate;
        pacing_rate = rate;
    }

    /* Pace using current bw estimate and a gain factor. */
    void bbr_set_pacing_rate(double bw, double gain){   
        double rate = bw * gain;
        rate *= bbr_pacing_margin_percent;
        if (!bbr.has_seen_rtt && m_iRTT != 100000)
            this->bbr_init_pacing_rate_from_rtt();
        if (bbr.full_bw_reached || rate > pacing_rate){

            m_dPktSndPeriod = (m_iMSS * 8.0) / rate;
            pacing_rate = rate;
        }
    }

    void bbr_check_full_bw_reached(){

        // outfile<<pacing_rate <<" "<<bbr.bw<<" "<< bbr.full_bw <<" "<<bbr.full_bw * bbr_full_bw_thresh <<" "<<bbr.full_bw_cnt
        // <<" "<<bbr.rtt_cnt<<endl;
        if (bbr.full_bw_reached || !bbr.round_start)
            return;

        double bw_thresh = bbr.full_bw * bbr_full_bw_thresh;
        if (bbr.bw >= bw_thresh) {
            bbr.full_bw = bbr.bw;
            bbr.full_bw_cnt = 0;
            return;
        }
        ++bbr.full_bw_cnt;
        bbr.full_bw_reached = bbr.full_bw_cnt >= bbr_full_bw_cnt;
    }

    void bbr_update_gains()
    {
        switch (bbr.mode) {
        case BBR_STARTUP:
            bbr.pacing_gain = bbr_high_gain;
            bbr.cwnd_gain	= bbr_high_gain;
            break;
        case BBR_DRAIN:
            bbr.pacing_gain = bbr_drain_gain;	/* slow, to drain */
            bbr.cwnd_gain	= bbr_high_gain;	/* keep cwnd */
            break;
        case BBR_PROBE_BW:
            bbr.pacing_gain = bbr_pacing_gain[bbr.cycle_idx];
            // bbr.pacing_gain = 1.0;
            bbr.cwnd_gain	= bbr_cwnd_gain;
            break;
        case BBR_PROBE_RTT:
            bbr.pacing_gain = 1;
            bbr.cwnd_gain	= 1;
            break;
        default:
            outfile<<"BBR bad mode: " << bbr.mode << endl;
            break;
        }
    }

   void bbr_update_bw(int32_t num){
        bbr.round_start = 0;
        m_PerfInfo = getPerfInfo();

        int i;
        for(i = 0; i < 10000; i++){
            if(RecordArray[i].SeqNo == num - 1){
                break;
            }
        }

        // outfile << RecordArray[i].pktDelivered <<" "<< bbr.next_rtt_delivered<<endl;
        if (RecordArray[i].pktDelivered >= bbr.next_rtt_delivered) {
            bbr.next_rtt_delivered = delivered_true;
            bbr.rtt_cnt++;
            bbr.round_start = 1;
            bbr.packet_conservation = 0;
        }

        if(i < 10000){
            //计算瓶颈带宽
            uint64_t ts = getTime();
            double rate = (delivered_true - RecordArray[i].pktDelivered)\
             * 1500 * 8 / (double)(ts - RecordArray[i].TimeStamp);

            if(bbr.rtt_cnt > rtt_cnt){
                bw_index = (bw_index + 1) % 10;
                BW_Record[bw_index] = rate;
                rtt_cnt = bbr.rtt_cnt;
            }else{
                if(BW_Record[bw_index] < rate){
                    BW_Record[bw_index] = rate;
                }
            }
            double max_rate = 0;
            for(int j = 0; j < 10; j ++){
                if(BW_Record[j] > max_rate)
                    max_rate = BW_Record[j];
            }
            bbr.bw = max_rate;
        }
   }

    void bbr_update_min_rtt(){
        bool filter_expired;

        /* Track min RTT seen in the min_rtt_win_sec filter window: */
        filter_expired = getTime() > (bbr.min_rtt_stamp + bbr_min_rtt_win_sec * 1e6);
        if (m_iRTT != 100000 && (m_iRTT < bbr.min_rtt_us || filter_expired)) {
            bbr.min_rtt_us = m_iRTT;
            bbr.min_rtt_stamp = getTime();
        }

        // if (bbr_probe_rtt_mode_ms > 0 && filter_expired && bbr.mode != BBR_PROBE_RTT) {
        //     bbr.mode = BBR_PROBE_RTT;  /* dip, drain queue */
        //     bbr.prior_cwnd = max(bbr.prior_cwnd, m_dCWndSize);  /* note cwnd so we can restore it */
        //     bbr.probe_rtt_done_stamp = 0;
        // }

        if (bbr.mode == BBR_PROBE_RTT) {
            /* Ignore low rate samples during this mode. */
        
            /* Maintain min packets in flight for max(200 ms, 1 round). */
            if (!bbr.probe_rtt_done_stamp && getPerfInfo()->pktFlightSize <= bbr_cwnd_min_target){
                bbr.probe_rtt_done_stamp = getTime() + bbr_probe_rtt_mode_ms * 1e3;
                bbr.probe_rtt_round_done = 0;
                bbr.next_rtt_delivered = delivered_true;
            } else if (bbr.probe_rtt_done_stamp) {
                if (bbr.round_start)
                    bbr.probe_rtt_round_done = 1;
                if (bbr.probe_rtt_round_done)
                    this->bbr_check_probe_rtt_done();
            }
        }
        // /* Restart after idle ends only once we process a new S/ACK for data */
        // if (rs->delivered > 0)
        //     bbr->idle_restart = 0;
    }

    void bbr_check_probe_rtt_done(){
        if (!(bbr.probe_rtt_done_stamp && (getTime() > bbr.probe_rtt_done_stamp)))
            return;

        bbr.min_rtt_stamp = getTime();  /* wait a while until PROBE_RTT */
        m_dCWndSize = max(m_dCWndSize, bbr.prior_cwnd);
        if (!bbr.full_bw_reached)
            bbr.mode = BBR_STARTUP;
        else
            this->bbr_reset_probe_bw_mode();
    }

    double bbr_bdp(double bw, double gain){
        double bdp;
        double w;
        if (bbr.min_rtt_us == 100000)	 /* no valid RTT samples yet? */
            return 10.0;  /* be safe: cap at default initial cwnd*/
        w = bw * bbr.min_rtt_us / 8.0 / 1500 * gain;
        return w;
    }

    void bbr_set_cwnd(double bw, double cwnd_gain){
        double cwnd = m_dCWndSize, target_cwnd = 0;

        // if (!acked){
        //     goto done;  /* no packet fully ACKed; just apply caps */
        // }

        //TODO:这个影响不大，暂时不管了
        // if (bbr_set_cwnd_to_recover_or_restore(sk, rs, acked, &cwnd)){
        // 	printk("ggg\n");
        // 	goto done;
        // }

        target_cwnd = this->bbr_bdp(bw, cwnd_gain);
        

        /* Increment the cwnd to account for excess ACKed data that seems
        * due to aggregation (of data and/or ACKs) visible in the ACK stream.
        */
        // target_cwnd += bbr_ack_aggregation_cwnd(sk);
        // target_cwnd = bbr_quantization_budget(sk, target_cwnd);

        // outfile << cwnd <<" "<<target_cwnd <<" "<<bbr.min_rtt_us<< endl;
        m_PerfInfo = getPerfInfo();
        /* If we're below target cwnd, slow start cwnd toward target cwnd. */
        if (bbr.full_bw_reached)  /* only cut cwnd if we filled the pipe */
            cwnd = min(cwnd + 1, target_cwnd);
        else if (cwnd < target_cwnd || m_PerfInfo->pktRecvACKTotal < 100)
            cwnd = cwnd + 1;
        if(bbr.mode == 1)
            cwnd = target_cwnd;
        cwnd = max(cwnd, bbr_cwnd_min_target);

        m_dCWndSize = min(cwnd, 4294967295.0);	/* apply global cap */
        if (bbr.mode == BBR_PROBE_RTT)  /* drain queue, refresh min_rtt */
            m_dCWndSize = min(m_dCWndSize, bbr_cwnd_min_target);
    }

     double bbr_inflight(double bw, double gain){
        double inflight;
        inflight = this->bbr_bdp(bw, gain);
        // inflight = bbr_quantization_budget(sk, inflight);
        return inflight;
    }

    double bbr_packets_in_net_at_edt(){
        m_PerfInfo = getPerfInfo();
        return (double)(m_PerfInfo->pktSentTotal - m_PerfInfo->pktRecvACKTotal);
        // struct tcp_sock *tp = tcp_sk(sk);
        // struct bbr *bbr = inet_csk_ca(sk);
        // u64 now_ns, edt_ns, interval_us;
        // u32 interval_delivered, inflight_at_edt;

        // now_ns = tp->tcp_clock_cache;
        // edt_ns = max(tp->tcp_wstamp_ns, now_ns);
        // interval_us = div_u64(edt_ns - now_ns, NSEC_PER_USEC);
        // interval_delivered = (u64)bbr_bw(sk) * interval_us >> BW_SCALE;
        // inflight_at_edt = inflight_now;
        // if (bbr->pacing_gain > BBR_UNIT)              /* increasing inflight */
        //     inflight_at_edt += bbr_tso_segs_goal(sk);  /* include EDT skb */
        // if (interval_delivered >= inflight_at_edt)
        //     return 0;
        // return inflight_at_edt - interval_delivered;
    }

    void bbr_check_drain(){
        m_PerfInfo = getPerfInfo();
        if (bbr.mode == BBR_STARTUP && bbr.full_bw_reached) {
            start_up_max_bw = bbr.bw;
            bbr.mode = BBR_DRAIN;	/* drain queue we created */
        }	/* fall through to check if in-flight is already small: */
        if (bbr.bw < start_up_max_bw / 2.89 && bbr.mode == BBR_DRAIN && m_PerfInfo->pktFlightSize <= this->bbr_inflight(bbr.bw, 1)){
            this->bbr_reset_probe_bw_mode();
            }
        // outfile <<"mode: "<< bbr.mode <<" cnt: "<<bbr.rtt_cnt<<" rate: "<<pacing_rate
        // <<" bw: "<<bbr.bw<<" udt-bw: "<< m_PerfInfo->mbpsBandwidth <<
        // " rtt: "<<bbr.min_rtt_us<<" "<<this->bbr_inflight(bbr.bw, 1)
        // <<" "<<m_PerfInfo->pktFlightSize
        // <<" cwnd: "<<m_dCWndSize <<" ack: " <<m_PerfInfo->SndLastAck
        // <<" maxseq: "<< m_PerfInfo->SndCurrSeqNo
        // <<" reack: "<<m_PerfInfo->pktRecvACKTotal
        // <<" deliver: "<<m_PerfInfo->pktDelivered
        // <<" cyc: "<< bbr.cycle_idx <<endl;
    }

    void bbr_reset_probe_bw_mode(){
        bbr.mode = BBR_PROBE_BW;
        bbr.cycle_idx = rand() % CYCLE_LEN;
        bbr.cycle_mstamp = getTime();
        // bbr_advance_cycle_phase(sk);	/* flip to next phase of gain cycle */
    }

    /* End cycle phase if it's time and/or we hit the phase's in-flight target. */
    bool bbr_is_next_cycle_phase(){
        bool is_full_length = getTime() - bbr.cycle_mstamp > bbr.min_rtt_us;
        double inflight, bw;
        if (bbr.pacing_gain == 1.0)
            return is_full_length;		/* just use wall clock time */
        m_PerfInfo = getPerfInfo();
        inflight = m_PerfInfo->pktFlightSize;

        if (bbr.pacing_gain > 1)
            return is_full_length && (inflight >= this->bbr_inflight(bw, bbr.pacing_gain));

        return is_full_length || inflight <= this->bbr_inflight(bw, 1);
    }

    /* Gain cycling: cycle pacing gain to converge to fair share of available bw. */
    void bbr_update_cycle_phase(){
        if (bbr.mode == BBR_PROBE_BW && this->bbr_is_next_cycle_phase()){
            bbr.cycle_idx = (bbr.cycle_idx + 1) % CYCLE_LEN;
	        bbr.cycle_mstamp = getTime();
        }
    }

    void onACK(int32_t num){
        this->bbr_update_bw(num);
        this->bbr_check_full_bw_reached();
        this->bbr_update_cycle_phase();
        this->bbr_check_drain();
        this->bbr_update_min_rtt();
        this->bbr_update_gains();
        this->bbr_set_pacing_rate(bbr.bw, bbr.pacing_gain);
        this->bbr_set_cwnd(bbr.bw, bbr.cwnd_gain);

        m_PerfInfo = getPerfInfo();
        outfile << "rtt-cnt: "<<bbr.rtt_cnt
        <<" mode: " <<bbr.mode
        <<" pacingrate: "<<pacing_rate
        <<" max-bw: "<<bbr.bw
        <<" pkt-flight: "<<m_PerfInfo->pktFlightSize
        <<" pkt-flight2: "<<m_PerfInfo->SndCurrSeqNo - m_PerfInfo->m_iISN - delivered_true
        <<" max-send: "<<m_PerfInfo->SndCurrSeqNo - m_PerfInfo->m_iISN
        <<" delivered: "<<delivered_true
        <<" bdp: "<< this->bbr_inflight(bbr.bw, 1)
        <<" rtt: "<<bbr.min_rtt_us << endl;

   }
   void onPktSent(const CPacket* packet) {
        m_PerfInfo = getPerfInfo();
        RecordArray[record_index].SeqNo = packet->m_iSeqNo;
        RecordArray[record_index].TimeStamp = getTime();
        RecordArray[record_index].pktDelivered = delivered_true;
        record_index = (record_index + 1) % 10000;
        // outfile <<"send: "<< packet->m_iSeqNo <<" cwnd: "<<m_dCWndSize << endl;
   }

    void processCustomMsg(const CPacket* packet) {
        delivered_true = *(int32_t *)packet->m_pcData;
        int32_t seq = *((int32_t *)packet->m_pcData + 1);
        outfile1 << "seq: "<<seq<<" deliverd_true: "<<delivered_true<<endl;
    }

protected:
    bool if_write;
    ofstream outfile;
    ofstream outfile1;
    const CPerfMon* m_PerfInfo;
    BBR bbr;
    int record_index;
    RecordUint RecordArray[10000];

    double pacing_rate;

    //统计最大带宽相关
    double BW_Record[10];
    uint32_t rtt_cnt = 0;
    uint32_t bw_index = 0;

    int32_t delivered_true;

};






class CBBR3: public CCC
{
public:
    CBBR3(){
        if_write = true;
        if(if_write){
            outfile.open("file.csv", ios::out);
            outfile1.open("file1.csv", ios::out);
            outfile <<"mode"<<','<< "seq" << ',' << "delivered" <<','<<"bbr.bw" <<','<<"rtt-cnt"<<','<<"bw"<<","<<"cwnd"<<
            ","<<"pacing_rate"<<','<<"bbr.min_rtt"<<','<<"pkt_inflight"<<','<<"bbr_bdp"
            << endl; 
            outfile1 << "pkt_sent_seq"<<","<<"cwnd"<<','<<"delivered"<<','<<"TS"<<endl;
        }

    }
    ~CBBR3(){
        outfile.close();
        outfile1.close();
    }

    void processCustomMsg(const CPacket* packet) {

        delivered_true = *(int32_t *)packet->m_pcData;
        int32_t seq = *((int32_t *)packet->m_pcData + 1);
        this->bbr_update_bw(seq);
        this->bbr_check_full_bw_reached();
        this->bbr_update_cycle_phase();
        this->bbr_check_drain();
        this->bbr_update_min_rtt(seq);
        this->bbr_update_gains();
        this->bbr_set_pacing_rate(bbr.bw, bbr.pacing_gain);
        this->bbr_set_cwnd(bbr.bw, bbr.cwnd_gain);
        // m_dCWndSize *= 1.1;
        
        int i;
        for(i = 0; i < 10000; i++){
            if(RecordArray[i].SeqNo == seq){
                break;
            }
        }
        double rate = 0.8 * (delivered_true - RecordArray[i].pktDelivered)\
             * 1500 * 8 / (double)(getTime() - RecordArray[i].TimeStamp);


        m_PerfInfo = getPerfInfo();
        outfile <<bbr.mode<<","<<seq<<','<<delivered_true<<','<<bbr.bw<<','<<bbr.rtt_cnt<<','
        << rate <<","<<m_dCWndSize <<","<<pacing_rate<<','<<bbr.min_rtt_us
        <<','<<m_PerfInfo->pktFlightSize<<','<<this->bbr_inflight(bbr.bw, 1)<<endl;
    }

    void init(){

        // m_dPktSndPeriod = (m_iMSS * 8.0) / 30.0;  // m_iMSS 1500 rate = 500 Mbps
        m_dCWndSize = 10.0; 
        setACKInterval(1);
        bbr.has_seen_rtt = 0;  // 目前是否对rtt进行过采样
        this->bbr_init_pacing_rate_from_rtt();
        
        record_index = 0;
        bbr.mode = BBR_STARTUP;
        memset(BW_Record, 0, sizeof(double)*10);



        bbr.prior_cwnd = 0;
        bbr.rtt_cnt = 0;
        bbr.next_rtt_delivered = 1;  // Total data packets delivered incl. rexmits，
                                                //交付的总数据，包括重传 初始值为1
        bbr.packet_conservation = 0; // 关闭包保护

        bbr.probe_rtt_done_stamp = 0;
        bbr.probe_rtt_round_done = 0; //是否处于rtt探测阶段
        bbr.min_rtt_us = 0;  // Minimum RTT in usec. ~0 means not available.
        bbr.min_rtt_stamp = getTime();

        bbr.round_start = 0;
        bbr.idle_restart = 0;
        bbr.full_bw_reached = 0;
        bbr.full_bw = 0;
        bbr.full_bw_cnt = 0;
        bbr.cycle_mstamp = 0;
        bbr.cycle_idx = 0;

        
        // bbr.ack_epoch_mstamp = tp.tcp_mstamp;  	/* most recent packet received/sent */
        bbr.ack_epoch_acked = 0;
        bbr.extra_acked_win_rtts = 0;
        bbr.extra_acked_win_idx = 0;
        bbr.extra_acked[0] = 0;
        bbr.extra_acked[1] = 0;
    }

    void bbr_update_cycle_phase(){
        if (bbr.mode == BBR_PROBE_BW && this->bbr_is_next_cycle_phase()){
            bbr.cycle_idx = (bbr.cycle_idx + 1) % CYCLE_LEN;
	        bbr.cycle_mstamp = getTime();
        }
    }

    bool bbr_is_next_cycle_phase(){
        bool is_full_length = getTime() - bbr.cycle_mstamp > bbr.min_rtt_us;
        double inflight, bw;
        if (bbr.pacing_gain == 1.0)
            return is_full_length;		/* just use wall clock time */
        m_PerfInfo = getPerfInfo();
        inflight = m_PerfInfo->pktFlightSize;

        if (bbr.pacing_gain > 1)
            return is_full_length && (inflight >= this->bbr_inflight(bw, bbr.pacing_gain));

        return is_full_length || inflight <= this->bbr_inflight(bw, 1);
    }

   void bbr_update_bw(int32_t num){
        bbr.round_start = 0;
        m_PerfInfo = getPerfInfo();

        int i;
        for(i = 0; i < 10000; i++){
            if(RecordArray[i].SeqNo == num){
                break;
            }
        }

        // outfile << RecordArray[i].pktDelivered <<" "<< bbr.next_rtt_delivered<<endl;
        if (RecordArray[i].pktDelivered >= bbr.next_rtt_delivered) {
            bbr.next_rtt_delivered = delivered_true;
            bbr.rtt_cnt++;
            bbr.round_start = 1;
            bbr.packet_conservation = 0;
        }

        if(i < 10000){
            //计算瓶颈带宽
            uint64_t ts = getTime();
            double rate = (delivered_true - RecordArray[i].pktDelivered)\
             * 1500 * 8 / (double)(ts - RecordArray[i].TimeStamp);

            // rate *= 0.8;

            if(bbr.rtt_cnt > rtt_cnt){
                bw_index = (bw_index + 1) % 10;
                BW_Record[bw_index] = rate;
                rtt_cnt = bbr.rtt_cnt;
            }else{
                if(BW_Record[bw_index] < rate){
                    BW_Record[bw_index] = rate;
                }
            }
            double max_rate = 0;
            for(int j = 0; j < 10; j ++){
                if(BW_Record[j] > max_rate)
                    max_rate = BW_Record[j];
            }
            bbr.bw = max_rate;
        }
   }

    void bbr_check_probe_rtt_done(){
        if (!(bbr.probe_rtt_done_stamp && (getTime() > bbr.probe_rtt_done_stamp)))
            return;
        bbr.min_rtt_stamp = getTime();  /* wait a while until PROBE_RTT */
        m_dCWndSize = max(m_dCWndSize, bbr.prior_cwnd);
        if (!bbr.full_bw_reached)
            bbr.mode = BBR_STARTUP;
        else
            this->bbr_reset_probe_bw_mode();
    }

     double bbr_inflight(double bw, double gain){
        double inflight;
        inflight = this->bbr_bdp(bw, gain);
        // inflight = bbr_quantization_budget(sk, inflight);
        return inflight;
    }

    void bbr_init_pacing_rate_from_rtt(){
        double rate;
        if (bbr.min_rtt_us != 0) {		/* any RTT sample yet? 平滑往返时间 */
            rate = m_dCWndSize * m_iMSS * 8 / m_iRTT  * bbr_high_gain ; //Mbps
            bbr.has_seen_rtt = 1;
        } else {			 /* no RTT sample yet */
            rate = m_dCWndSize * m_iMSS * 8 / 1000  * bbr_high_gain ; //Mbps
        }
        rate *= bbr_pacing_margin_percent;
        m_dPktSndPeriod = (m_iMSS * 8.0) / rate;
        pacing_rate = rate;
    }

    void bbr_update_gains()
    {
        switch (bbr.mode) {
        case BBR_STARTUP:
            bbr.pacing_gain = bbr_high_gain;
            bbr.cwnd_gain	= bbr_high_gain;
            break;
        case BBR_DRAIN:
            bbr.pacing_gain = bbr_drain_gain;	/* slow, to drain */
            bbr.cwnd_gain	= bbr_high_gain;	/* keep cwnd */
            break;
        case BBR_PROBE_BW:
            bbr.pacing_gain = bbr_pacing_gain[bbr.cycle_idx];
            // bbr.pacing_gain = 1.0;
            bbr.cwnd_gain	= bbr_cwnd_gain;
            break;
        case BBR_PROBE_RTT:
            bbr.pacing_gain = 1;
            bbr.cwnd_gain	= 1;
            break;
        default:
            outfile<<"BBR bad mode: " << bbr.mode << endl;
            break;
        }
    }

    void bbr_reset_probe_bw_mode(){
        bbr.mode = BBR_PROBE_BW;
        bbr.cycle_idx = rand() % CYCLE_LEN;
        bbr.cycle_mstamp = getTime();
    }

    void onACK(int32_t num){
        // this->bbr_update_bw(num);
        // outfile <<num<<','<<delivered_true<<','<<bbr.bw<<','<<bbr.rtt_cnt<<endl;
   }

    /* Pace using current bw estimate and a gain factor. */
    void bbr_set_pacing_rate(double bw, double gain){   
        double rate = bw * gain;
        rate *= bbr_pacing_margin_percent;
        if (!bbr.has_seen_rtt && bbr.min_rtt_us != 0)
            this->bbr_init_pacing_rate_from_rtt();
        if (bbr.full_bw_reached || rate > pacing_rate){

            m_dPktSndPeriod = (m_iMSS * 8.0) / rate;
            pacing_rate = rate;
        }
    }


    void bbr_check_full_bw_reached(){
        if (bbr.full_bw_reached || !bbr.round_start)
            return;
        double bw_thresh = bbr.full_bw * bbr_full_bw_thresh;
        if (bbr.bw >= bw_thresh) {
            bbr.full_bw = bbr.bw;
            bbr.full_bw_cnt = 0;
            return;
        }
        ++bbr.full_bw_cnt;
        bbr.full_bw_reached = bbr.full_bw_cnt >= bbr_full_bw_cnt;
    }

    void bbr_set_cwnd(double bw, double cwnd_gain){
        double cwnd = m_dCWndSize, target_cwnd = 0;
        target_cwnd = this->bbr_bdp(bw, cwnd_gain);

        m_PerfInfo = getPerfInfo();
        /* If we're below target cwnd, slow start cwnd toward target cwnd. */
        if (bbr.full_bw_reached)  /* only cut cwnd if we filled the pipe */
            cwnd = min(cwnd + 1, target_cwnd);
        else if (cwnd < target_cwnd || m_PerfInfo->pktRecvACKTotal < 100)
            cwnd = cwnd + 1;
        if(bbr.mode == 1)
            cwnd = target_cwnd;
        cwnd = max(cwnd, bbr_cwnd_min_target);

        m_dCWndSize = min(cwnd, 4294967295.0);	/* apply global cap */
        if (bbr.mode == BBR_PROBE_RTT)  /* drain queue, refresh min_rtt */
            m_dCWndSize = min(m_dCWndSize, bbr_cwnd_min_target);
    }

    double bbr_bdp(double bw, double gain){
        double bdp;
        double w;
        if (bbr.min_rtt_us == 0)	 /* no valid RTT samples yet? */
            return 10.0;  /* be safe: cap at default initial cwnd*/
        w = bw * bbr.min_rtt_us / 8.0 / 1500 * gain;
        return w;
    }

    void bbr_update_min_rtt(int32_t seq){
        int i;
        for(i = 0; i < 10000; i++){
            if(RecordArray[i].SeqNo == seq){
                break;
            }
        }
        bool filter_expired;

        uint32_t rtt = getTime() - RecordArray[i].TimeStamp;
        /* Track min RTT seen in the min_rtt_win_sec filter window: */
        filter_expired = getTime() > (bbr.min_rtt_stamp + bbr_min_rtt_win_sec * 1e6);

        if (bbr.min_rtt_us == 0 || (rtt < bbr.min_rtt_us || filter_expired)) {
            bbr.min_rtt_us = rtt;
            bbr.min_rtt_stamp = getTime();
        }

        if (bbr_probe_rtt_mode_ms > 0 && filter_expired && bbr.mode != BBR_PROBE_RTT) {
            bbr.mode = BBR_PROBE_RTT;  /* dip, drain queue */
            bbr.prior_cwnd = max(bbr.prior_cwnd, m_dCWndSize);  /* note cwnd so we can restore it */
            bbr.probe_rtt_done_stamp = 0;
        }

        if (bbr.mode == BBR_PROBE_RTT) {
            /* Ignore low rate samples during this mode. */
        
            /* Maintain min packets in flight for max(200 ms, 1 round). */
            // if (!bbr.probe_rtt_done_stamp && getPerfInfo()->pktFlightSize <= bbr_cwnd_min_target){
            if (!bbr.probe_rtt_done_stamp ){
                bbr.probe_rtt_done_stamp = getTime() + bbr_probe_rtt_mode_ms * 1e3;
                bbr.probe_rtt_round_done = 0;
                bbr.next_rtt_delivered = delivered_true;
            } else if (bbr.probe_rtt_done_stamp) {
                if (bbr.round_start)
                    bbr.probe_rtt_round_done = 1;
                if (bbr.probe_rtt_round_done)
                    this->bbr_check_probe_rtt_done();
            }
        }
    }

    void bbr_check_drain(){
        m_PerfInfo = getPerfInfo();
        if (bbr.mode == BBR_STARTUP && bbr.full_bw_reached) {
            start_up_max_bw = bbr.bw;
            bbr.mode = BBR_DRAIN;	/* drain queue we created */
        }	/* fall through to check if in-flight is already small: */
        if (bbr.mode == BBR_DRAIN && (m_PerfInfo->pktFlightSize <= this->bbr_inflight(bbr.bw, 1) || bbr.bw < start_up_max_bw / 2.89)){
            this->bbr_reset_probe_bw_mode();
            }
    }

   void onPktSent(const CPacket* packet) {
        m_PerfInfo = getPerfInfo();
        RecordArray[record_index].SeqNo = packet->m_iSeqNo;
        RecordArray[record_index].TimeStamp = getTime();
        RecordArray[record_index].pktDelivered = delivered_true;
        record_index = (record_index + 1) % 10000;
        outfile1 << packet->m_iSeqNo <<","<<m_dCWndSize <<','<<delivered_true
        <<','<<RecordArray[record_index].TimeStamp<< endl;
   }

protected:
    bool if_write;
    ofstream outfile;
    ofstream outfile1;
    const CPerfMon* m_PerfInfo;
    BBR bbr;
    int record_index;
    RecordUint RecordArray[10000];

    double pacing_rate;

    //统计最大带宽相关
    double BW_Record[10];
    uint32_t rtt_cnt = 0;
    uint32_t bw_index = 0;
    int32_t delivered_true;
};