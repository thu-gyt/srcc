#include <udt.h>
#include <ccc.h>
#include <fstream>
#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include<map>
using namespace std;

class CBBR2: public CCC
{
public:
    CBBR2(){
        if_write = true;
        if(if_write){
            outfile.open("file.csv", ios::out);
            outfile << "rtt-cnt"<<','<<"mode"<<','<<"seq"<<","<<"delivered"<<","<<"rate"<<','<<"max-rate"<<','<<"pacing-rate"
            <<','<<"inflight"<<','<<"bdp"<<','<<"min-rtt"<<','<< "cwnd" <<endl;
            outfile1.open("file1.csv", ios::out);
            outfile1 << "seq"<<','<<"delivered"<<endl;
        }
    }
    ~CBBR2(){
        outfile.close();
        outfile1.close();
    }

    void init(){
        // m_dPktSndPeriod = (m_iMSS * 8.0) / 6.5;  // m_iMSS 1500 rate = 500 Mbps
        // setRTO(0.5 * 1e6); 
        // m_dCWndSize = 1000.0; 
        // double send_rate = 47.0; //
        // m_dPktSndPeriod = 1500.0 * 8 / send_rate;

        m_dCWndSize = 10.0; 
        bbr.has_seen_rtt = 0;  // 目前是否对rtt进行过采样
        bbr.mode = BBR_STARTUP;
        this->bbr_init_pacing_rate_from_rtt();
        memset(BW_Record, 0, sizeof(double)*10);
        bbr.prior_cwnd = 0;
        bbr.rtt_cnt = 0;
        bbr.next_rtt_delivered = 1;

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

    void onACK(int32_t num){
    }

    void bbr_update_min_rtt(int32_t seq){

        map<int32_t, bw_RecordUint> ::iterator l_it;
        l_it = bw_record_map.find(seq);
        if(l_it == bw_record_map.end()){
            // outfile<<"we do not find"<<endl;
            return;
        }

        bool filter_expired;
        uint32_t rtt = getTime() - l_it->second.TimeStamp;
        if(rtt < 40000){
            return;
        }
        /* Track min RTT seen in the min_rtt_win_sec filter window: */
        filter_expired = getTime() > (bbr.min_rtt_stamp + bbr_min_rtt_win_sec * 1e6);

        if (bbr.min_rtt_us == 0 || (rtt < bbr.min_rtt_us || filter_expired)) {
            bbr.min_rtt_us = rtt;
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

        // bw_record_map.erase(seq);
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

   void onPktSent(const CPacket* packet) {
        m_PerfInfo = getPerfInfo();

        bw_RecordUint uint1;
        uint1.pktDelivered = delivered_true;
        uint1.TimeStamp = getTime();
         map<int32_t, bw_RecordUint> ::iterator l_it;
        l_it = bw_record_map.find(packet->m_iSeqNo);
        if(l_it == bw_record_map.end()){
            bw_record_map[packet->m_iSeqNo]= uint1;
            return;
        }
        
        // outfile1 << packet->m_iSeqNo <<','<<delivered_true <<endl;
   }

    void processCustomMsg(const CPacket* packet) {

        delivered_true = *(int32_t *)packet->m_pcData;
        int32_t seq = *((int32_t *)packet->m_pcData + 1);
        m_PerfInfo = getPerfInfo();

        double rate = this->bbr_update_bw(seq);
        this->bbr_check_full_bw_reached();
        this->bbr_update_cycle_phase();
        this->bbr_check_drain();
        this->bbr_update_min_rtt(seq);
        this->bbr_update_gains();
        this->bbr_set_pacing_rate(bbr.bw, bbr.pacing_gain);
        this->bbr_set_cwnd(bbr.bw, bbr.cwnd_gain);


        // outfile << bbr.rtt_cnt << ','<<bbr.mode<<','<< seq <<','<<delivered_true<<','<<rate
        // <<','<<bbr.bw <<','<<pacing_rate<<','<<m_PerfInfo->SndCurrSeqNo - delivered_true - m_PerfInfo->m_iISN
        // <<','<<this->bbr_bdp(bbr.bw, 1)<<','<<bbr.min_rtt_us<<','<<m_dCWndSize<<endl;
    }

    void bbr_check_drain(){
        m_PerfInfo = getPerfInfo();
        if (bbr.mode == BBR_STARTUP && bbr.full_bw_reached) {
            start_up_max_bw = bbr.bw;
            bbr.mode = BBR_DRAIN;	/* drain queue we created */
        }	/* fall through to check if in-flight is already small: */
        if (bbr.mode == BBR_DRAIN && (m_PerfInfo->SndCurrSeqNo - delivered_true - m_PerfInfo->m_iISN <= this->bbr_bdp(bbr.bw, 1))){
            this->bbr_reset_probe_bw_mode();
            }
    }

    void bbr_reset_probe_bw_mode(){
        bbr.mode = BBR_PROBE_BW;
        bbr.cycle_idx = rand() % CYCLE_LEN;
        bbr.cycle_mstamp = getTime();
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
            return is_full_length && (inflight >= this->bbr_bdp(bw, bbr.pacing_gain));

        return is_full_length || inflight <= this->bbr_bdp(bw, 1);
    }

    double bbr_update_bw(int32_t num){
        bbr.round_start = 0;
        m_PerfInfo = getPerfInfo();

        map<int32_t, bw_RecordUint> ::iterator l_it;; 
        l_it = bw_record_map.find(num);
        if(l_it == bw_record_map.end()){
            // outfile<<"we do not find"<<endl;
            return 0;
        }

        if (l_it->second.pktDelivered >= bbr.next_rtt_delivered) {
            bbr.next_rtt_delivered = delivered_true;
            bbr.rtt_cnt++;
            bbr.round_start = 1;
            bbr.packet_conservation = 0;
        }

        //计算瓶颈带宽
        uint64_t ts = getTime();
        double rate = (delivered_true - l_it->second.pktDelivered)\
            * 1500 * 8 / (double)(ts - l_it->second.TimeStamp);

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
        return rate;
    }

    void bbr_set_cwnd(double bw, double cwnd_gain){
        double cwnd = m_dCWndSize, target_cwnd = 0;
        target_cwnd = this->bbr_bdp(bw, cwnd_gain);

        m_PerfInfo = getPerfInfo();
        /* If we're below target cwnd, slow start cwnd toward target cwnd. */
        if (bbr.full_bw_reached)  /* only cut cwnd if we filled the pipe */
            cwnd = min(cwnd + 1, target_cwnd);
        else if (cwnd < target_cwnd)
            cwnd = cwnd + 1;
        if(bbr.mode == 1)
            cwnd = target_cwnd;
        // cwnd = target_cwnd;
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
            // outfile<<"BBR bad mode: " << bbr.mode << endl;
            break;
        }
    }

protected:
    bool if_write;
    ofstream outfile;
    ofstream outfile1;
    const CPerfMon* m_PerfInfo;
    BBR bbr;
    double pacing_rate;
    //统计最大带宽相关
    double BW_Record[10];
    uint32_t rtt_cnt = 0;
    uint32_t bw_index = 0;
    int32_t delivered_true;
    map<int32_t, bw_RecordUint> bw_record_map;
};









class CBBR4: public CCC
{
public:
    CBBR4(){
        if_write = true;
        if(if_write){
            outfile.open("file.csv", ios::out);
            outfile << "rtt-cnt"<<','<<"mode"<<','<<"seq"<<","<<"delivered"<<","<<"rate"<<','<<"max-rate"<<','<<"pacing-rate"
            <<','<<"inflight"<<','<<"bdp"<<','<<"min-rtt"<<endl;
            outfile1.open("file1.csv", ios::out);
            outfile1 << "seq"<<','<<"delivered"<<endl;
        }
    }
    ~CBBR4(){
        outfile.close();
        outfile1.close();
    }

    void init(){
        // m_dPktSndPeriod = (m_iMSS * 8.0) / 6.5;  // m_iMSS 1500 rate = 500 Mbps
        // setRTO(0.5 * 1e6); 
        // m_dCWndSize = 1000.0; 
        m_dCWndSize = 100000.0; 
        double send_rate = 490.0; //
        m_dPktSndPeriod = 1500.0 * 8 / send_rate;

        bbr.has_seen_rtt = 0;  // 目前是否对rtt进行过采样
        bbr.mode = BBR_STARTUP;

        memset(BW_Record, 0, sizeof(double)*10);
        bbr.prior_cwnd = 0;
        bbr.rtt_cnt = 0;
        bbr.next_rtt_delivered = 1;

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
    }


    void onACK(int32_t num){
    }



   void onPktSent(const CPacket* packet) {
        m_PerfInfo = getPerfInfo();
        bw_RecordUint uint1;
        uint1.pktDelivered = delivered_true;
        uint1.TimeStamp = getTime();
        bw_record_map[packet->m_iSeqNo]= uint1;
        outfile1 << packet->m_iSeqNo <<','<<delivered_true <<endl;
   }

    void processCustomMsg(const CPacket* packet) {

        delivered_true = *(int32_t *)packet->m_pcData;
        int32_t seq = *((int32_t *)packet->m_pcData + 1);
        m_PerfInfo = getPerfInfo();
        double rate = this->bbr_update_bw(seq);


        // outfile << bbr.rtt_cnt << ','<<bbr.mode<<','<< seq <<','<<delivered_true<<','<<rate
        // <<','<<bbr.bw <<','<<pacing_rate<<','<<m_PerfInfo->SndCurrSeqNo - delivered_true - m_PerfInfo->m_iISN
        // <<','<<this->bbr_bdp(bbr.bw, 1)<<','<<bbr.min_rtt_us<<','<<m_dCWndSize<<endl;
    }




    double bbr_update_bw(int32_t num){
        bbr.round_start = 0;
        m_PerfInfo = getPerfInfo();

        map<int32_t, bw_RecordUint> ::iterator l_it;; 
        l_it = bw_record_map.find(num);
        if(l_it == bw_record_map.end()){
            outfile<<"we do not find"<<endl;
            return 0;
        }

        if (l_it->second.pktDelivered >= bbr.next_rtt_delivered) {
            bbr.next_rtt_delivered = delivered_true;
            bbr.rtt_cnt++;
            bbr.round_start = 1;
            bbr.packet_conservation = 0;
        }

        //计算瓶颈带宽
        uint64_t ts = getTime();
        double rate = (delivered_true - l_it->second.pktDelivered)\
            * 1500 * 8 / (double)(ts - l_it->second.TimeStamp);

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
        return rate;
    }



protected:
    bool if_write;
    ofstream outfile;
    ofstream outfile1;
    const CPerfMon* m_PerfInfo;
    BBR bbr;
    double pacing_rate;
    //统计最大带宽相关
    double BW_Record[10];
    uint32_t rtt_cnt = 0;
    uint32_t bw_index = 0;
    int32_t delivered_true;
    map<int32_t, bw_RecordUint> bw_record_map;
};

