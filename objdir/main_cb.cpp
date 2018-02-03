// #define SC_DISABLE_VIRTUAL_BIND 1

#include <systemc.h>
#include <vector>
#include <systemc>
#include <random>
#include <map>

#define ADDR_WIDTH 	8
#define DATA_WIDTH 	16

#define CPU_CNT     20
#define MEM_CNT 	  3

#define MAX_TRANS_PER_CPU 1000
#define MAX_TRANS_DELAY 5

#define TIMEOUT_CNT 100

// #define DEAL_WITH_STARVATION

#define MEM_RSP_MAX_DELAY 5
// #define MEM_RDRSP_MAX_DELAY  4
// #define MEM_WRRSP_MAX_DELAY  4

// 0 -> 100  
// higher means more writes 
// lower  means more reads 
#define WR_RD_DISTR 50 

using namespace std;

int trans_id=0;

double global_avg_per_trans = 0;
int global_min_per_trans    = 0;
int global_max_per_trans    = 0;
int total_transactions      = 0;
int total_time_trans        = 0;

double avg_per_trans_per_cpu[CPU_CNT] = {0};
int min_per_trans_per_cpu[CPU_CNT]    = {0};
int max_per_trans_per_cpu[CPU_CNT]    = {0};
int total_time_trans_per_cpu[CPU_CNT] = {0};
int trans_cnt_per_cpu[CPU_CNT]        = {0};
int max_queue_size_per_cpu[CPU_CNT]   = {0};


enum { READ, 
       WRITE };

//scheduler mode 
enum { SM_RR,   // Round robin 
       SM_FP,   // Fixed priority 
       SM_RP};  // Rotate priority 

enum { 
       STATUS_ERR,
       STATUS_OK
      };

struct transaction {
    int  type; // 0-read , 1-write
    int  src; 
    int  dst; 
    int  addr;
    int  data;
  
    int  tid;  // transaction id - supposed to be unique
    int  priority; //  LOW, MEDIUM, HIGH, URGENT,
    int  status;

    // for statistics 
    int start_time;
    int finish_time;

    int time_stamp_in_sch;

    transaction& operator= (const transaction& t)
    {
      this->type  = t.type; 
      this->src   = t.src; 
      this->dst   = t.dst; 
      this->addr  = t.addr; 
      this->data  = t.data;

      this->tid       = t.tid;
      this->priority  = t.priority;
      this->status    = t.status;

      this->start_time  = t.start_time;
      this->finish_time = t.finish_time;

      this->time_stamp_in_sch = t.time_stamp_in_sch;
    }

    bool operator==(const transaction& t) const {
        return false; // !!! 
    }
};

// print time_stamp 
void time_stamp(){ printf("%5llu ns: ",sc_time_stamp().value()/1000 );}
long long unsigned int get_current_time_ns(){return sc_time_stamp().value()/1000;};

ostream& operator<<(ostream& os, const transaction& t) {
	return os;
}

SC_MODULE (master) {
  
  sc_port<sc_fifo_out_if<transaction> > out;  // send requests 
  sc_port<sc_fifo_in_if<transaction> > in;    // receive response 
  transaction treq;
  transaction trsp; 
  
  int delay; 

  int CPUID; // this should be the source id of the cpu

  int trans_per_master;   // number of transactions sent by this CPU - used in statistics 
  int responses_seen_cnt;



  static int count; 

  void req_thd()
  {
    // use this to limit nr of trans generated due to limited printing space in eda
    trans_per_master = rand() % MAX_TRANS_PER_CPU;
    int nr_trans = trans_per_master;
    
    std::default_random_engine generator;
    std::exponential_distribution<double> distribution(3.5);

    printf("CPU%d will send %d transactions. \n", CPUID, nr_trans);
    
    while (nr_trans--)
    {

      // delay = rand() % MAX_TRANS_DELAY + 1;
      double number = distribution(generator);
      delay = int(number*10);

      wait(delay,SC_NS);

      treq.type = (rand() % 100) < WR_RD_DISTR; 
      treq.src  = CPUID; 
      treq.dst  = rand() % MEM_CNT;
      treq.addr = rand() % (1 << ADDR_WIDTH); 
      treq.data = rand() % (1 << DATA_WIDTH); 
      
      treq.tid  = trans_id;

      // treq.priority = rand() % 4;
      treq.priority = CPUID % 4;

      treq.start_time = int(get_current_time_ns()); 

      time_stamp();
      printf("CPU%2d: TID:%d  CPU%d->MEM%d  %s  addr:%4x  data:%8x pr:%d\n", CPUID, 
            treq.tid, treq.src, treq.dst, 
            ((treq.type) ? "Write":"Read "), 
            treq.addr,((treq.type) ? treq.data : 0), treq.priority );

      // send transaction
      out->write(treq);

      // increment global TID
      trans_id ++;
    }
    time_stamp(); printf("CPU%d finished sending transactions -------------------------\n",CPUID);
  }
  
  // this thread waits for the responses
  // in order to mark the transaction as completed
  // Statistics gathered: 
  void rsp_thd()
  {

    responses_seen_cnt = 0; 

    int age;

    while(1){
      in->read(trsp); responses_seen_cnt++;
      time_stamp();
      printf("CPU%2d: Received %s_RSP for TID:%d  CPU%d->MEM%d   addr:%4x  data:%8x\n\n", CPUID, 
              ((trsp.type) ? "WR":"RD"), 
              trsp.tid, trsp.src, trsp.dst, 
              trsp.addr,((trsp.type==READ) ? trsp.data : 0) );
      trsp.finish_time = int(get_current_time_ns());

      age = trsp.finish_time - trsp.start_time; 

      if (age>max_per_trans_per_cpu[CPUID]) { max_per_trans_per_cpu[CPUID] = age; }
      if (age<min_per_trans_per_cpu[CPUID] || !min_per_trans_per_cpu[CPUID]) { min_per_trans_per_cpu[CPUID] = age; }

      total_time_trans_per_cpu[CPUID] += age;

      printf("CPU%2d: TID:%d took %d cycles to complete.\n",CPUID,trsp.tid, age);


      if (trans_per_master == responses_seen_cnt){
        avg_per_trans_per_cpu[CPUID] = (total_time_trans_per_cpu[CPUID] / double (trans_per_master));
        trans_cnt_per_cpu[CPUID]  = trans_per_master;
        printf("CPU%d has seen all responses.\n", CPUID );
      }
    }
  }


  SC_HAS_PROCESS(master);
  master(sc_module_name name):sc_module(name)
  {
    SC_THREAD(req_thd); // produce thread
    SC_THREAD(rsp_thd);
    CPUID = count++;
    cout<<"CPU" << CPUID <<" created. ";
  }
  
};

SC_MODULE (scheduler) {
  
  // RR - Go through each port 1 at a time and pick a request 
  // FP - Pick the request with highest priority 
  //      If two have the same priority pick the oldest transaction waiting in fifo. 
  //      If a request spends more than X time waiting to be picked increase its priority 
  //        to avoid starvation
  // RP -  

  sc_port<sc_fifo_in_if<transaction> >  in[CPU_CNT];
  sc_port<sc_fifo_out_if<transaction> > out[MEM_CNT];

  sc_port<sc_fifo_in_if<transaction> >  rsp_in[MEM_CNT];
  sc_port<sc_fifo_out_if<transaction> > rsp_out[CPU_CNT];

  transaction treq;
  transaction trsp; 
  
  // for Round Robin
  int request_port; //only 1 req port active at any moment
  int response_port; //only 1 rsp port active at any moment 

  int mode; 	      //scheduler modes: Round robin, Fixed priority, Rotate Priority. 
  int req_timeout;      // scheduler timeout . when 0 the main thread finishes.
  int rsp_timeout;
  int inflight_tr_cnt;

  transaction buffered_responses[MEM_CNT];
  
  // variables used for fixed priority 
  bool        tr_avail[CPU_CNT]={0};
  transaction tr[CPU_CNT];

  void req_thd()
  {
    int delay; 

    request_port 	= 0 ;
    req_timeout       = TIMEOUT_CNT;

    // tr_avail[0]= = {0,0,0};
    mode = rand()%3;
    // mode = 0;

    switch (mode){
      case 0: time_stamp(); printf("SCH  : Mode - Round-robin.\n");
              break;
      case 1: time_stamp(); printf("SCH  : Mode - Request priority.\n");
              break;
      case 2: time_stamp(); printf("SCH  : Mode - Rotate priority.\n");
              break;
    }
    std::cout<<endl;
    while (1) 
    {
      // delay = rand() % max_delay+1;
      wait(1,SC_NS);
      switch (mode){
        case 0: pick_request_rr(request_port); // Round robin
                break;
        case 1: pick_sg_request_fp();
                break;
        case 2: pick_sg_request_fp();
                break;
      }

      if(!req_timeout) { 
        time_stamp(); 
        printf("SCH  : Scheduler finished picking requests.\n");
        return ; }
    }
  }

  void rsp_thd()
  {

    response_port  = 0 ;
    rsp_timeout       = TIMEOUT_CNT;
    while (1) 
    {
      // delay = rand() % max_delay+1;
      wait(1,SC_NS); 

      pick_responses();
      if(!rsp_timeout) { 
        time_stamp(); 
        printf("SCH  : Scheduler finished picking responses.\n");
        return ; 
      } else {
        send_responses();
      }
    }
  }

  // pick next request port in Round robin mode
  bool pick_request_rr(int &reqp){
    // return 0 if failed to find a fifo 
    // that has transactions to consume 

    int i=0;
    req_timeout = TIMEOUT_CNT;  

    while (in[request_port]->num_available() == 0){ 
      request_port = (request_port+1)%CPU_CNT;
      i++;
      if(i==CPU_CNT){
        wait(1,SC_NS); i=0;
        req_timeout--;
        time_stamp(); 
        printf("SCH  : Waiting for requests req_timeout:%d \n", req_timeout);
      }
      if (!req_timeout) {
        time_stamp(); 
        printf("SCH  : Scheduler timeout(%d ns) while waiting for requests\n",100); // BOZO 
        return 0;
      }

    }

    if (in[request_port]->num_available()>max_queue_size_per_cpu[request_port])
      max_queue_size_per_cpu[request_port] = in[request_port]->num_available();

    in[request_port]->read(treq); 
    time_stamp();
    printf("SCH  : Picked Req TID:%d CPU%d->MEM%d  %s addr:%4x  data:%8x \n", 
            treq.tid, treq.src, treq.dst, 
            ((treq.type) ? "Write":"Read "),
            treq.addr, ((treq.type==WRITE) ? treq.data : 0));

    out[treq.dst]->write(treq); // send request to slave 
  } // pick_request_rr 

  // pick a request at a time
  bool pick_sg_request_fp () {

    int         tr_cnt=0;
    int         tr_pos=0; 
    bool        tr_in_buff=0;

    for (int i=0;i<CPU_CNT;i++){
      if (in[i]->num_available() ){
        if (tr_avail[i]==0) {
          if (in[i]->num_available()>max_queue_size_per_cpu[i])
            max_queue_size_per_cpu[i] = in[i]->num_available();

          in[i]->read(tr[i]);
          tr[i].time_stamp_in_sch = get_current_time_ns();
          tr_avail[i] = 1;
        }
        tr_cnt++;
      }
    }
    for (int i = 0; i < CPU_CNT; ++i)
    {   
      if (tr_avail[i]==1){
        // debug prints
        #ifdef DEAL_WITH_STARVATION
          // we want to increase priority to avoid starvation of requests with low priority
          int waiting_time = get_current_time_ns() - tr[i].time_stamp_in_sch;
          if (waiting_time > 50 && tr[i].priority < 3)
          {
            // increase priority 
            tr[i].priority++;
            tr[i].time_stamp_in_sch = get_current_time_ns();
            time_stamp();
            printf("SCH  : Increasing priority for TID:%d to pr:%d \n",tr[i].tid, tr[i].priority);
            /* code */
          }
        #endif
        tr_in_buff = 1;
        // time_stamp();
        // printf("SCH  : TID:%d [%d]           addr:%4x  pr:%d \n",tr[i].tid, i, tr[i].addr, tr[i].priority);
      }

    }
    // printf(" %llu SCH TO:%d tr_cnt:%d\n", get_current_time_ns(),req_timeout,tr_cnt);
    // if(tr_cnt){
    if(tr_in_buff){
      for (int i=0; i<CPU_CNT; i++){
        if (tr_avail[i]==1){
          treq = tr[i]; // assign first avail transaction
          tr_pos = i;

          break;
        }
      }

      // then look for one with higher priority.
      for (int i=0; i<CPU_CNT; i++){
        if (tr_avail[i]==1 && tr[i].tid != treq.tid){
          if(tr[i].priority > treq.priority){
            // if trans with higher pr found replace the old one
            treq = tr[i];
            tr_pos = i; 

            time_stamp(); 
            printf("SCH  : Found TID:%d with higher priority\n",treq.tid);
          }else if (tr[i].priority == treq.priority) {
            // pick the oldest from the two
            if ( (get_current_time_ns() - tr[i].start_time) > (get_current_time_ns() - treq.start_time) ){
              treq = tr[i];
              tr_pos = i;
              
              time_stamp();
              printf("SCH  : Found older TID:%d with same priority(age=%llu ns)\n", treq.tid, get_current_time_ns() - tr[i].start_time);
            }
          } else {
            // do nothing 
          }
        }
      }

      tr_avail[tr_pos]=0;     // clear flag for the fifo
      req_timeout = TIMEOUT_CNT;  // reset timeout counter

      time_stamp();
      printf("SCH  : Picked Req TID:%d CPU%d->MEM%d  %s addr:%4x  data:%8x \n", 
            treq.tid, treq.src, treq.dst, 
            ((treq.type) ? "Write":"Read "),
            treq.addr, ((treq.type==WRITE) ? treq.data : 0));

      out[treq.dst]->write(treq);
    } else { //tr_cnt == 0 
      req_timeout--; 
      wait(1,SC_NS);
    }

    if (!req_timeout) {
      time_stamp(); 
      printf("SCH  : Scheduler timeout(%d ns) while waiting for requests\n",100); // BOZO 
      return 0;
    }
  } // pick_sg_request_fp

  bool pick_mt_request_fp () {

    int         tr_cnt=0;
    int         tr_pos=0;

    bool        mem_picked[MEM_CNT];



    for (int i=0;i<CPU_CNT;i++){
      if (in[i]->num_available() ){
        if (tr_avail[i]==0) {
          in[i]->read(tr[i]);
          tr_avail[i] = 1;
        }
        tr_cnt++;
      }
    }
    for (int i = 0; i < CPU_CNT; ++i)
    {   
      if (tr_avail[i]==1){
        // debug prints
        // time_stamp();
        // printf("SCH  : TID:%d [%d]           addr:%4x  pr:%d \n",tr[i].tid, i, tr[i].addr, tr[i].priority);
      }

    }
    // printf(" %llu SCH TO:%d tr_cnt:%d\n", get_current_time_ns(),req_timeout,tr_cnt);
    if(tr_cnt){
      for (int i=0; i<CPU_CNT; i++){
        if (tr_avail[i]==1){
          treq = tr[i]; // assign first avail transaction
          tr_pos = i;

          break;
        }
      }

      // then look for one with higher priority.
      for (int i=0; i<CPU_CNT; i++){
        if (tr_avail[i]==1 && tr[i].tid != treq.tid){
          if(tr[i].priority > treq.priority){
            // if trans with higher pr found replace the old one
            treq = tr[i];
            tr_pos = i; 

            time_stamp(); 
            printf("SCH  : Found TID:%d with higher priority\n",treq.tid);
          }else if (tr[i].priority == treq.priority) {
            // pick the oldest from the two
            if ( (get_current_time_ns() - tr[i].start_time) > (get_current_time_ns() - treq.start_time) ){
              treq = tr[i];
              tr_pos = i;
              
              time_stamp();
              printf("SCH  : Found older TID:%d with same priority(age=%llu ns)\n", treq.tid, get_current_time_ns() - tr[i].start_time);
            }
          } else {
            // do nothing 
          }
        }
      }

      tr_avail[tr_pos]=0;     // clear flag for the fifo
      req_timeout = TIMEOUT_CNT;  // reset timeout counter

      time_stamp();
      printf("SCH  : Picked TID:%d              addr:%4x  data:%8x \n", 
              treq.tid, treq.addr, ((treq.type==WRITE) ? treq.data : 0));      
      // out[treq.dst]->write(treq);
    } else { //tr_cnt == 0 
      req_timeout--; 
      wait(1,SC_NS);
    }

    if (!req_timeout) {
      time_stamp(); 
      printf("SCH  : Scheduler timeout(%d ns) while waiting for requests\n",100); // BOZO 
      return 0;
    }
  } // pick_mt_request_fp

  // pick responses
  bool pick_responses(){
    // return 0 if failed to find a fifo 
    // that has transactions to consume 

    int i=0;
    rsp_timeout = TIMEOUT_CNT;  
    bool rsp_avail[MEM_CNT]; 
    // go through each port
    for (int i = 0; i < MEM_CNT; ++i)
    {
      
    }
    while (rsp_in[response_port]->num_available() == 0){ 
      response_port = (response_port+1)%MEM_CNT;
      i++;
      if(i==MEM_CNT){
        wait(1,SC_NS); i=0;
        rsp_timeout--;
        time_stamp(); 
        printf("SCH  : Waiting for responses timeout:%d \n", rsp_timeout);
      }
      if (!rsp_timeout) {
        time_stamp(); 
        printf("SCH  : Scheduler timeout(%d ns) while waiting for responses\n",100); // BOZO 
        return 0;
      }
    }

    rsp_timeout = TIMEOUT_CNT;  // reset timeout counter 
    rsp_in[response_port]->read(trsp); 

    time_stamp();
    printf("SCH  : Picked Rsp TID:%d MEM%d->CPU%d %s addr:%4x  data:%8x \n", 
            trsp.tid, trsp.dst, trsp.src, 
            ((trsp.type) ? "Write":"Read "),
            trsp.addr, ((trsp.type==READ) ? trsp.data : 0));

  } // pick_responses

  void send_responses(){
    // send the response stored to CPU
    rsp_out[trsp.src]->write(trsp);
    // BOZO send rsps in paralel 
  }

  SC_HAS_PROCESS(scheduler);
  scheduler(sc_module_name name): sc_module(name) 
  {
    SC_THREAD(req_thd); // request thread
    SC_THREAD(rsp_thd); // response thread
  }
  
};

SC_MODULE(slave) {
  // memory will be the slave of transactions

  int MEMID;
  static int count; 

  sc_port<sc_fifo_in_if<transaction> >  in;   // req 
  sc_port<sc_fifo_out_if<transaction> > out;  // rsp 
  
  map<int,int> memory;

  int response_time; 
  
  void thd(void) {
  response_time = rand() % MEM_RSP_MAX_DELAY + 1; 
    while (true) {
      transaction T = in->read();

      // wait response time 
      wait(response_time,SC_NS);

      // process request
      if (T.type == READ) {
          if (memory.find(T.addr) == memory.end()){
              memory.insert(std::pair<int,int>(T.addr,0));
              T.data = 0xbadf00d;
          } else {
              T.data = memory[T.addr];
          }
          time_stamp();
          printf("MEM: TID:%d   RD_RSP  addr:%4x  data:%x \n",T.tid,  T.addr, T.data);
      } else if (T.type == WRITE) {
          time_stamp();
          printf("MEM: TID:%d   WR_RSP  addr:%4x  \n",T.tid, T.addr);
          memory[T.addr] = T.data;
      }

      // send the transaction back with the response.
      out->write(T);
    }
  }
    
  SC_CTOR(slave) {
    SC_THREAD(thd);
    // dont_initialize();
    MEMID = count++;
    cout<<"MEM" << MEMID <<" created. ";

  }
};

SC_MODULE (sys) {
  
  sc_fifo<transaction> cfifo[CPU_CNT];
  sc_fifo<transaction> mfifo[MEM_CNT];
  sc_fifo<transaction> cfifo_rsp[CPU_CNT];
  sc_fifo<transaction> mfifo_rsp[MEM_CNT];

  vector<master*> cpus; 
  vector<slave*> mems; 
  scheduler sch; 
  char cname[5] = "Cpu ";
  char mname[5] = "Mem ";
  //SC_HAS_PROCESS(system);
  SC_CTOR(sys): sch("System") {
  
  //create and connect cpus  
  for (int i = 0; i < CPU_CNT ;i++){
    cname[3] = '0'+i;
    cpus.push_back(new master(cname));
    // req channel 
    sch.in[i](cfifo[i]);
    cpus[i]->out(cfifo[i]);
    // rsp channel 
    sch.rsp_out[i](cfifo_rsp[i]);
    cpus[i]->in(cfifo_rsp[i]);
    printf("Connected CPU%d -> Scheduler\n",i);
  }

  for (int i = 0; i < MEM_CNT; i++)
  {
    mname[3] = '0'+i;
    mems.push_back(new slave(mname));
    // req channel 
    mems[i]->in(mfifo[i]);
    sch.out[i](mfifo[i]);
    // rsp channel  
    mems[i]->out(mfifo_rsp[i]);
    sch.rsp_in[i](mfifo_rsp[i]);
    printf("Connecting MEM%d <- Scheduler\n",i);

  }

  }

};

int master::count = 0;  // initialize count of CPUs
int slave::count = 0; 



int sc_main (int argc, char* argv[]) {
  
sys system1("MDSys");
   
  sc_start(); 

  printf("\n\n############  Summary  Report ############\n\n");
  printf("CROSSBAR %d CPUs x %d MEMs \n\n", CPU_CNT, MEM_CNT);

  printf("Total transactions issued:    %d  \n", trans_id);
  printf("Total transactions completed: %d  \n", trans_id); // BOZO change this

  int cumulative_time = 0; 
  for (int i = 0; i < CPU_CNT; ++i)
  {
    // total_transactions += trans_per_master[i];
    total_time_trans   += total_time_trans_per_cpu[i];
    cumulative_time    += avg_per_trans_per_cpu[i];
    if (!global_max_per_trans) global_min_per_trans = min_per_trans_per_cpu[i]; // for first
    global_min_per_trans = (min_per_trans_per_cpu[i]<global_min_per_trans) ? min_per_trans_per_cpu[i] : global_min_per_trans;
    global_max_per_trans = (max_per_trans_per_cpu[i]>global_max_per_trans) ? max_per_trans_per_cpu[i] : global_max_per_trans;
  }
  // if (trans_per_master!=trans_id) printf("Error: Mismatch regarding the total_transactions issued.");
  
  total_transactions = trans_id;
  // global_avg_per_trans = total_time_trans / total_transactions;
  global_avg_per_trans = cumulative_time / double (CPU_CNT);


  printf("Time spent per transaction:\n");
  for(int i=0; i<CPU_CNT;i++) {
    printf("\tCPU[%2d] AVG: %8.2lf    MIN:%2d    MAX:%6d %4d transactions eff:%lf, Max queue size: %d\n",i, 
        avg_per_trans_per_cpu[i], 
        min_per_trans_per_cpu[i], 
        max_per_trans_per_cpu[i],
        trans_cnt_per_cpu[i],
        avg_per_trans_per_cpu[i] / double(trans_cnt_per_cpu[i]),
        max_queue_size_per_cpu[i]);
  }
  printf("\tGLOBAL AVG: %lf    MIN:%d    MAX:%d \n", 
        global_avg_per_trans, 
        global_min_per_trans, 
        global_max_per_trans);

  return 0; 
}