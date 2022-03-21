#include <udt.h>
#include <ccc.h>
#include <fstream>
#include <iostream>
using namespace std;


// ofstream outfile;
// outfile.open("file.dat");
// outfile << m_iMSS << endl;
// outfile.close();

class CBBR: public CCC
{
public:
   void init(){
        m_dCWndSize = 1000000.0; 
        setACKInterval(1);
   }
   void onACK(int32_t num){
        // m_PerfInfo = getPerfInfo();
        // ofstream outfile;
        // outfile.open("file.dat", ios::out | ios::app);
        // outfile << num <<" "<< m_PerfInfo->msRTT <<" "<<m_PerfInfo->mbpsRecvRate << endl;
        // outfile.close();
   }
   void onPktSent(const CPacket* packet) {
        // ofstream outfile;
        // outfile.open("file1.dat", ios::out | ios::app);
        // outfile << packet->m_iSeqNo << endl;
        // outfile.close();
   }

protected:
    const CPerfMon* m_PerfInfo;
    struct bbr {
        uint32_t	min_rtt_us;	        /* min RTT in min_rtt_win_sec window 在rtt窗口中的最小rtt*/
        uint32_t	min_rtt_stamp;	        /* timestamp of min_rtt_us 最小rtt对应的时间戳*/
        uint32_t	probe_rtt_done_stamp;   /* end time for BBR_PROBE_RTT mode 探测rtt模式的结束时间*/
        // struct minmax bw;	/* Max recent delivery rate in pkts/uS << 24 在bw窗口内的最大传输速率*/
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
        uint32_t	pacing_gain:10,	/* current gain for setting pacing rate */
            cwnd_gain:10,	/* current gain for setting cwnd */
            full_bw_reached:1,   /* reached full bw in Startup? start up阶段是否达到最大带宽*/
            full_bw_cnt:2,	/* number of rounds without large bw gains 没有获得带宽大幅度增长的回合数*/
            cycle_idx:3,	/* current index in pacing_gain cycle array */
            has_seen_rtt:1, /* have we seen an RTT sample yet?  是否已经有rtt的测量值*/
            unused_b:5;
        uint32_t	prior_cwnd;	/* prior cwnd upon entering loss recovery */
        uint32_t	full_bw;	/* recent bw, to estimate if pipe is full 估算的带宽值*/

        /* For tracking ACK aggregation: */
        uint64_t	ack_epoch_mstamp;	/* start of ACK sampling epoch */
        uint16_t	extra_acked[2];		/* max excess data ACKed in epoch */
        uint32_t	ack_epoch_acked:20,	/* packets (S)ACKed in sampling epoch 在采样epoch内被确认的数据包*/
            extra_acked_win_rtts:5,	/* age of extra_acked, in round trips */
            extra_acked_win_idx:1,	/* current index in extra_acked array */
            unused_c:6;
    };

};