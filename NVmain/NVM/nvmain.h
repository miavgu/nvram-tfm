/*******************************************************************************
* Copyright (c) 2012-2014, The Microsystems Design Labratory (MDL)
* Department of Computer Science and Engineering, The Pennsylvania State University
* All rights reserved.
* 
* This source code is part of NVMain - A cycle accurate timing, bit accurate
* energy simulator for both volatile (e.g., DRAM) and non-volatile memory
* (e.g., PCRAM). The source code is free and you can redistribute and/or
* modify it by providing that the following conditions are met:
* 
*  1) Redistributions of source code must retain the above copyright notice,
*     this list of conditions and the following disclaimer.
* 
*  2) Redistributions in binary form must reproduce the above copyright notice,
*     this list of conditions and the following disclaimer in the documentation
*     and/or other materials provided with the distribution.
* 
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
* 
* Author list: 
*   Matt Poremba    ( Email: mrp5060 at psu dot edu 
*                     Website: http://www.cse.psu.edu/~poremba/ )
*******************************************************************************/

#ifndef __NVMAIN_H__
#define __NVMAIN_H__

#include <iostream>
#include <fstream>
#include <stdint.h>
#include "src/Params.h"
#include "src/NVMObject.h"
#include "src/Prefetcher.h"
#include "include/NVMainRequest.h"
#include "traceWriter/GenericTraceWriter.h"
#include <unordered_map>
#include <vector>
#include <deque>
// #include <array>
#include <queue>

namespace NVM {

class Config;
class MemoryController;
class MemoryControllerManager;
class Interconnect;
class AddressTranslator;
class SimInterface;
class NVMainRequest;

class NVMain : public NVMObject
{
  public:
    NVMain( );
    ~NVMain( );

    void SetConfig( Config *conf, std::string memoryName = "defaultMemory", bool createChildren = true );

    Config *GetConfig( );

    void IssuePrefetch( NVMainRequest *request );
    bool IssueCommand( NVMainRequest *request );
    bool IssueAtomic( NVMainRequest *request );
    bool IsIssuable( NVMainRequest *request, FailReason *reason );

    bool RequestComplete( NVMainRequest *request );

    bool CheckPrefetch( NVMainRequest *request );

    void RegisterStats( );
    void CalculateStats( );

    void Cycle( ncycle_t steps );

    void EnqueuePendingMemoryRequests( NVMainRequest *request );

    std::ofstream traceFile;

  private:
    Config *config;
    Config **channelConfig;
    MemoryController **memoryControllers;
    AddressTranslator *translator;

    ncounter_t totalReadRequests;
    ncounter_t totalWriteRequests;
    ncounter_t successfulPrefetches;
    ncounter_t unsuccessfulPrefetches;

    unsigned int numChannels;
    double syncValue;

    Prefetcher *prefetcher;
    std::list<NVMainRequest *> prefetchBuffer;
    std::queue<NVMainRequest *> pendingMemoryRequests;

    std::ofstream pretraceOutput;
    GenericTraceWriter *preTracer;

    void PrintPreTrace( NVMainRequest *request );
    void GeneratePrefetches( NVMainRequest *request, std::vector<NVMAddress>& prefetchList );

    // std::unordered_map<uint64_t,uint64_t> directMap;
    std::unordered_map<uint64_t,uint64_t> inverseMap;
    uint64_t tagLines;
    
    class RequestStub {
      public:
        uint64_t pAddr;
        uint64_t targetAddr;
        NVM::OpType type;
        NVMDataBlock& data;

        RequestStub(uint64_t _pAddr, uint64_t _targetAddr, NVM::OpType _type, NVMDataBlock& _data)
        : pAddr(_pAddr), targetAddr(_targetAddr), type(_type), data(_data)
        {
          this->data = _data;
        }
    };

    // std::vector<RequestStub> waitingDRAMWrite;
    // uint8_t DRAMWriteSize = 0;

    std::vector<NVMainRequest*> waitingDRAMRead;
    int DRAMReadSize = 0;
    std::vector<NVMainRequest*> waitingDRAMWrite;
    int DRAMWriteSize = 0;
    std::vector<NVMainRequest*> waitingNVRAMWrite;
    int NVRAMWriteSize = 0;
    std::vector<NVMainRequest*> declinedReq;
    int DeclinedReqSize = 0;
    uint64_t totalReqs, reqsHit, reqsMiss, notIssuableTag, tagReplacements;
    uint64_t RDHit, RDMiss, WRHit, WRMiss;
    uint64_t RD_Replc, RD_Clean, RD_Present;
    uint64_t readsOps, readsNotDRAM;

    bool useTagCache = false;
    
    uint64_t tmpAddr = 0;

    std::unordered_map<uint64_t, uint64_t> NVWtoDRW;

    class LRU {
      public:
        int size;                                 //Size variable
        std::unordered_map<uint64_t, uint64_t> m; //Data storage
        std::deque<uint64_t> dq;                  //Implement LRU logic

        LRU(uint64_t capacity) {
          m.clear();
          dq.clear();
          size = capacity;
        }

        inline bool has(uint64_t key)
        {
          return !(m.find(key) == m.end());
        }

        uint64_t get(uint64_t key, int *found)
        {
          if (m.find(key) == m.end()) //Not in cache
          {
            *found = 0;
            return 0;
          }

          auto it = dq.begin();
          while(*it != key)
            it++;
          
          dq.erase(it);
          dq.push_front(key);

          *found = 1;
          return m[key];
        }

        void put(uint64_t key, uint64_t value)
        {
          if (m.find(key) == m.end()) //Not in cache
          {
            if(size == dq.size())     //Cache is full, need to replace
            {
              uint64_t leastUsed = dq.back();
              dq.pop_back();
              m.erase(leastUsed);
            }
          }
          else                        //Already present, need to delet the already present
          {
            auto it = dq.begin();
            while(*it != key)
              it++;

            dq.erase(it);
            m.erase(key);
          }

          //Add the pair
          dq.push_front(key);
          m[key] = value;
        }

        inline uint64_t removeLRU()
        {
          uint64_t leastUsed = dq.back();
          dq.pop_back();
          uint64_t freedAddr = m[leastUsed];
          m.erase(leastUsed);
          return freedAddr;
        }

        inline bool isFull()
        {
          return (size == dq.size());
        }
    };

    LRU *replacementPolicy;
};

};

#endif
