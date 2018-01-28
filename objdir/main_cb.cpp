// #define SC_DISABLE_VIRTUAL_BIND 1

#include <systemc.h>
#include <vector>
#include <systemc>
#include <random>
#include <map>

#define ADDR_WIDTH 	8
#define DATA_WIDTH 	16

#define CPU_CNT     4
#define MEM_CNT 	  3

#define MAX_TRANS_DELAY 2

#define TIMEOUT_CNT 100

#define MEM_RSP_MAX_DELAY 3
// #define MEM_RDRSP_MAX_DELAY  4
// #define MEM_WRRSP_MAX_DELAY  4

// 0 -> 100  
// higher means more writes 
// lower  means more reads 
#define WR_RD_DISTR 80 

using namespace std;

int trans_id=0;

enum { READ, 
       WRITE };

//scheduler mode 
enum { SM_RR,   // Round robin 
       SM_FP,   // Fixed priority 
       SM_RP};  // Rotate priority 

struct transaction {
    int  type; // 0-read , 1-write
    int  src; 
    int  dst; 
    int  addr; 
    int  data;
  
    int  tid;  // transaction id - supposed to be unique
    int  priority; //  LOW, URGENT, HIGH, MEDIUM
    int  status;

    // for statistics 
    int start_time;
    int finish_time;

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

SC_MODULE (producer) {
  
  sc_port<sc_fifo_out_if<transaction> > out;  // send requests 
  sc_port<sc_fifo_in_if<transaction> > in;    // receive response 
  transaction treq;
  transaction rsp; 
  
  int delay; 

  int CPUID; // this should be the source id of the cpu

  static int count; 
  void pthd()
  {
    // use this to limit nr of trans generated due to limited printing space in eda
    int nr_trans = rand() % 100;
    
    printf("CPU%d will send %d transactions. \n", CPUID, nr_trans);
    
    while (nr_trans--)
    {
      delay = rand() % MAX_TRANS_DELAY + 1;
      wait(delay,SC_NS);

      treq.type = (rand() % 100) < WR_RD_DISTR; 
      treq.src  = CPUID; 
      treq.dst  = rand() % MEM_CNT;
      treq.addr = rand() % (1 << ADDR_WIDTH); 
      treq.data = rand() % (1 << DATA_WIDTH); 
      
      treq.tid  = trans_id;

      treq.priority = rand() % 4;

      treq.start_time = int(get_current_time_ns()); 

      time_stamp();
      printf("CPU: TID:%d  CPU%d->MEM%d  %s  addr:%4x  data:%8x pr:%d\n", 
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
  
  SC_HAS_PROCESS(producer);
  producer(sc_module_name name):sc_module(name)
  {
    SC_THREAD(pthd); // produce thread 
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
  
  int request_port; //only 1 port active at any moment in time
  
  int mode; 	      //scheduler modes: Round robin, Fixed priority, Rotate Priority. 
  int timeout;      // scheduler timeout . when 0 the main thread finishes.
  
  // variables used for fixed priority 
  bool        tr_avail[CPU_CNT]={0};
  transaction tr[CPU_CNT];

  void cthd()
  {
    int delay; 

    request_port 	= 0 ;
    timeout       = TIMEOUT_CNT;

    // tr_avail[0]= = {0,0,0};

    std::cout<<endl;
    while (1) 
    {
      // delay = rand() % max_delay+1;
      wait(1,SC_NS); 

      // pick_request_rr(request_port); // Round robin
      pick_request_fp();


      if(!timeout) { 
        time_stamp(); 
        printf("SCH : Scheduler finished.\n");
        return ; }
    }
  }

  // pick next request port in Round robin mode
  bool pick_request_rr(int &reqp){
    // return 0 if failed to find a fifo 
    // that has transactions to consume 

    int i=0;
    timeout = TIMEOUT_CNT;  

    while (in[request_port]->num_available() == 0){ 
      request_port = (request_port+1)%CPU_CNT;
      i++;

      if(i==CPU_CNT){
        // time_stamp(); 
        // printf("SCH : No requests to consume. Retry...\n");
        wait(1,SC_NS); i=0;
      }
      timeout--;
      if (!timeout) {
        time_stamp(); 
        printf("SCH : Scheduler timeout(%d ns) while waiting for requests\n",100); // BOZO 
        return 0;
      }
    }

    in[request_port]->read(treq); 
    time_stamp();
    printf("CPU: Picked TID:%d CPU%d->MEM%d  %s addr:%4x  data:%8x \n", 
            treq.tid, treq.src, treq.dst, 
            ((treq.type) ? "Write":"Read "),
            treq.addr, ((treq.type) ? treq.data : 0));
  } // pick_request_rr 

  bool pick_request_fp () {

    int         tr_cnt=0;
    int         tr_pos=0; 

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
        // printf("SCH: TID:%d [%d]           addr:%4x  pr:%d \n",tr[i].tid, i, tr[i].addr, tr[i].priority);
      }

    }
    // printf(" %llu SCH TO:%d tr_cnt:%d\n", get_current_time_ns(),timeout,tr_cnt);
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
            printf("SCH: Found TID:%d with higher priority\n",treq.tid);
          }else if (tr[i].priority == treq.priority) {
            // pick the oldest from the two
            if ( (get_current_time_ns() - tr[i].start_time) > (get_current_time_ns() - treq.start_time) ){
              treq = tr[i];
              tr_pos = i;
              
              time_stamp();
              printf("SCH: Found older TID:%d with same priority(age=%llu ns)\n", treq.tid, get_current_time_ns() - tr[i].start_time);
            }
          } else {
            // do nothing 
          }
        }
      }

      tr_avail[tr_pos]=0;     // clear flag for the fifo
      timeout = TIMEOUT_CNT;  // reset timeout counter

      time_stamp();
      printf("SCH: Picked TID:%d              addr:%4x  data:%8x \n", 
              treq.tid, treq.addr, ((treq.type) ? treq.data : 0));      
      // out[treq.dst]->write(treq);
    } else { //tr_cnt == 0 
      timeout--; 
      wait(1,SC_NS);
    }

    if (!timeout) {
      time_stamp(); 
      printf("SCH : Scheduler timeout(%d ns) while waiting for requests\n",100); // BOZO 
      return 0;
    }

  } // pick_request_fp

  // pick responses
  bool pick_responses(){
    // return 0 if failed to find a fifo 
    // that has transactions to consume 

    int i=0;
    timeout = TIMEOUT_CNT;  
    rsp_avail[CPU_CNT]; 
    // go through each memory port
    for (int i = 0; i < MEM_CNT; ++i)
    {
      
    }
    while (in_rs[request_port]->num_available() == 0){ 
      request_port = (request_port+1)%CPU_CNT;
      i++;

      if(i==CPU_CNT){
        // time_stamp(); 
        // printf("SCH : No requests to consume. Retry...\n");
        wait(1,SC_NS); i=0;
      }
      timeout--;
      if (!timeout) {
        time_stamp(); 
        printf("SCH : Scheduler timeout(%d ns) while waiting for requests\n",100); // BOZO 
        return 0;
      }
    }

    in[request_port]->read(treq); 
    time_stamp();
    printf("CPU: Picked TID:%d CPU%d->MEM%d  %s addr:%4x  data:%8x \n", 
            treq.tid, treq.src, treq.dst, 
            ((treq.type) ? "Write":"Read "),
            treq.addr, ((treq.type) ? treq.data : 0));
  } // pick_request_rr 

  SC_HAS_PROCESS(scheduler);
  scheduler(sc_module_name name): sc_module(name) 
  {
    SC_THREAD(cthd); // consumer thread
  }
  
};

SC_MODULE(consumer) {
  // memory will be the consumer of transactions

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

      // process request
      if (T.type == READ) {
          if (memory.find(T.addr) == memory.end()){
              memory.insert(std::pair<int,int>(T.addr,0));
              T.data = 0xbadf00d;
          } else {
              T.data = memory[T.addr];
          }
          time_stamp();
          printf("MEM: TID:%d   Read     addr:%4x  pr:%d \n",T.tid,  T.addr, T.priority);
      } else if (T.type == WRITE) {
          time_stamp();
          printf("MEM: TID:%d   Write    addr:%4x  pr:%d \n",T.tid, T.addr, T.priority);
          memory[T.addr] = T.data;
      }
      // wait response time 
      wait(response_time,SC_NS);

      // send the transaction back with the response.
    }
  }
    
  SC_CTOR(consumer) {
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

  vector<producer*> cpus; 
  vector<consumer*> mems; 
  scheduler sch; 
  char cname[5] = "Cpu ";
  char mname[5] = "Mem ";
  //SC_HAS_PROCESS(system);
  SC_CTOR(sys): sch("System") {
  
  //create and connect cpus  
  for (int i = 0; i < CPU_CNT ;i++){
    cname[3] = '0'+i;
    cpus.push_back(new producer(cname));
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
    mems.push_back(new consumer(mname));
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

int producer::count = 0;  // initialize count of CPUs
int consumer::count = 0;  

int sc_main (int argc, char* argv[]) {
  
sys system1("MDSys");
   
  sc_start(); 

  printf("############  Summary  ############\n\n");
  printf("CROSSBAR %d CPUs x %d MEMs \n\n", CPU_CNT, MEM_CNT);

  printf("Total transactions issued:    %d  \n", trans_id);
  printf("Total transactions completed: %d  \n", trans_id); // BOZO change this

  printf("Time spent per transaction: \n");



  
  return 0; 
}