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

#include "nvmain.h"
#include "src/Config.h"
#include "src/AddressTranslator.h"
#include "src/Interconnect.h"
#include "src/SimInterface.h"
#include "src/EventQueue.h"
#include "Interconnect/InterconnectFactory.h"
#include "MemControl/MemoryControllerFactory.h"
#include "traceWriter/TraceWriterFactory.h"
#include "Decoders/DecoderFactory.h"
#include "Utils/HookFactory.h"
#include "include/NVMainRequest.h"
#include "include/NVMHelpers.h"
#include "Prefetchers/PrefetcherFactory.h"

#include <sstream>
#include <cassert>

using namespace NVM;

NVMain::NVMain()
{
    config = NULL;
    translator = NULL;
    memoryControllers = NULL;
    channelConfig = NULL;
    syncValue = 0.0f;
    preTracer = NULL;

    totalReadRequests = 0;
    totalWriteRequests = 0;

    prefetcher = NULL;
    successfulPrefetches = 0;
    unsuccessfulPrefetches = 0;

    notIssuableTag = totalReqs = reqsHit = reqsMiss = 0;
    RDHit = RDMiss = WRHit = WRMiss = 0;
    tagReplacements = 0;
    RD_Replc = RD_Clean = RD_Present = 0;
    readsNotDRAM = readsOps = 0;
    NVWtoDRW.clear();

    traceFile.open("nvmain.trace");
}

NVMain::~NVMain()
{
    if (config)
        delete config;

    if (memoryControllers)
    {
        for (unsigned int i = 0; i < numChannels; i++)
        {
            if (memoryControllers[i])
                delete memoryControllers[i];
        }

        delete[] memoryControllers;
    }

    if (translator)
        delete translator;

    if (channelConfig)
    {
        for (unsigned int i = 0; i < numChannels; i++)
        {
            delete channelConfig[i];
        }

        delete[] channelConfig;
    }
}

Config *NVMain::GetConfig()
{
    return config;
}

void NVMain::SetConfig(Config *conf, std::string memoryName, bool createChildren)
{
    TranslationMethod *method;
    int channels, ranks, banks, rows, cols, subarrays;

    Params *params = new Params();
    params->SetParams(conf);
    SetParams(params);

    StatName(memoryName);

    config = conf;
    if (config->GetSimInterface() != NULL)
        config->GetSimInterface()->SetConfig(conf, createChildren);
    else
        std::cout << "Warning: Sim Interface should be allocated before configuration!" << std::endl;

    if (createChildren)
    {
        if (conf->KeyExists("MATHeight"))
        {
            rows = static_cast<int>(p->MATHeight);
            subarrays = static_cast<int>(p->ROWS / p->MATHeight);
        }
        else
        {
            rows = static_cast<int>(p->ROWS);
            subarrays = 1;
        }
        cols = static_cast<int>(p->COLS);
        banks = static_cast<int>(p->BANKS);
        ranks = static_cast<int>(p->RANKS);
        channels = static_cast<int>(p->CHANNELS);
        useTagCache = p->useTagCache;

        // TODO: Add Config support
        if (useTagCache)
        {
            tagLines = 1048576; // 1048576 lines * 64 B per line = 64MB
            inverseMap.reserve(tagLines);
            replacementPolicy = new LRU(tagLines);
        }

        if (config->KeyExists("Decoder"))
            translator = DecoderFactory::CreateNewDecoder(config->GetString("Decoder"));
        else
            translator = new AddressTranslator();

        method = new TranslationMethod();

        method->SetBitWidths(NVM::mlog2(rows),
                             NVM::mlog2(cols),
                             NVM::mlog2(banks),
                             NVM::mlog2(ranks),
                             NVM::mlog2(channels),
                             NVM::mlog2(subarrays));
        method->SetCount(rows, cols, banks, ranks, channels, subarrays);
        method->SetAddressMappingScheme(p->AddressMappingScheme);
        translator->SetConfig(config, createChildren);
        translator->SetTranslationMethod(method);
        translator->SetDefaultField(CHANNEL_FIELD);

        SetDecoder(translator);

        memoryControllers = new MemoryController *[channels];
        channelConfig = new Config *[channels];
        for (int i = 0; i < channels; i++)
        {
            std::stringstream confString;
            std::string channelConfigFile;

            channelConfig[i] = new Config(*config);

            channelConfig[i]->SetSimInterface(config->GetSimInterface());

            confString << "CONFIG_CHANNEL" << i;

            if (config->GetString(confString.str()) != "")
            {
                channelConfigFile = config->GetString(confString.str());

                if (channelConfigFile[0] != '/')
                {
                    channelConfigFile = NVM::GetFilePath(config->GetFileName());
                    channelConfigFile += config->GetString(confString.str());
                }

                std::cout << "Reading channel config file: " << channelConfigFile << std::endl;

                channelConfig[i]->Read(channelConfigFile);
            }

            /* Initialize memory controller */
            memoryControllers[i] =
                MemoryControllerFactory::CreateNewController(channelConfig[i]->GetString("MEM_CTL"));

            /* When selecting a MC child, use no field from the decoder (only-child). */

            confString.str("");
            confString << memoryName << ".channel" << i << "."
                       << channelConfig[i]->GetString("MEM_CTL");
            memoryControllers[i]->StatName(confString.str());
            memoryControllers[i]->SetID(i);

            AddChild(memoryControllers[i]);
            memoryControllers[i]->SetParent(this);

            /* Set Config recursively. */
            memoryControllers[i]->SetConfig(channelConfig[i], createChildren);

            /* Register statistics. */
            memoryControllers[i]->RegisterStats();
        }
    }

    if (p->MemoryPrefetcher != "none")
    {
        prefetcher = PrefetcherFactory::CreateNewPrefetcher(p->MemoryPrefetcher);
        std::cout << "Made a " << p->MemoryPrefetcher << " prefetcher." << std::endl;
    }

    numChannels = static_cast<unsigned int>(p->CHANNELS);

    std::string pretraceFile;

    if (p->PrintPreTrace || p->EchoPreTrace)
    {
        if (config->GetString("PreTraceFile") == "")
            pretraceFile = "trace.nvt";
        else
            pretraceFile = config->GetString("PreTraceFile");

        if (pretraceFile[0] != '/')
        {
            pretraceFile = NVM::GetFilePath(config->GetFileName());
            pretraceFile += config->GetString("PreTraceFile");
        }

        std::cout << "Using trace file " << pretraceFile << std::endl;

        if (config->GetString("PreTraceWriter") == "")
            preTracer = TraceWriterFactory::CreateNewTraceWriter("NVMainTrace");
        else
            preTracer = TraceWriterFactory::CreateNewTraceWriter(config->GetString("PreTraceWriter"));

        if (p->PrintPreTrace)
            preTracer->SetTraceFile(pretraceFile);
        if (p->EchoPreTrace)
            preTracer->SetEcho(true);
    }

    RegisterStats();
}

bool NVMain::IsIssuable(NVMainRequest *request, FailReason *reason)
{
    uint64_t channel, rank, bank, row, col, subarray;
    bool rv;

    assert(request != NULL);

    GetDecoder()->Translate(request->address.GetPhysicalAddress(),
                            &row, &col, &rank, &bank, &channel, &subarray);

    rv = memoryControllers[channel]->IsIssuable(request, reason);

    return rv;
}

void NVMain::GeneratePrefetches(NVMainRequest *request, std::vector<NVMAddress> &prefetchList)
{
    std::vector<NVMAddress>::iterator iter;
    ncounter_t channel, rank, bank, row, col, subarray;

    for (iter = prefetchList.begin(); iter != prefetchList.end(); iter++)
    {
        /* Make a request from the prefetch address. */
        NVMainRequest *pfRequest = new NVMainRequest();
        *pfRequest = *request;
        pfRequest->address = (*iter);
        pfRequest->isPrefetch = true;
        pfRequest->owner = this;

        /* Translate the address, then copy to the address struct, and copy to request. */
        GetDecoder()->Translate(request->address.GetPhysicalAddress(),
                                &row, &col, &bank, &rank, &channel, &subarray);
        request->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
        request->bulkCmd = CMD_NOP;

        // std::cout << "Prefetching 0x" << std::hex << (*iter).GetPhysicalAddress() << " (trigger 0x"
        //           << request->address.GetPhysicalAddress( ) << std::dec << std::endl;

        /* Just type to issue; If the queue is full it simply won't be enqueued. */
        GetChild(pfRequest)->IssueCommand(pfRequest);
    }
}

void NVMain::IssuePrefetch(NVMainRequest *request)
{
    /*
     *  Generate prefetches here. It makes the most sense to prefetch in this class
     *  since we may prefetch across channels. However, this may be applicable to any
     *  class in the hierarchy as long as we filter out the prefetch addresses that
     *  do not belong to a child of the current module.
     */
    /* TODO: We are assuming this is the master root! */
    std::vector<NVMAddress> prefetchList;
    if (prefetcher && request->type == READ && request->isPrefetch == false && prefetcher->DoPrefetch(request, prefetchList))
    {
        GeneratePrefetches(request, prefetchList);
    }
}

bool NVMain::CheckPrefetch(NVMainRequest *request)
{
    bool rv = false;
    NVMainRequest *pfRequest = NULL;
    std::list<NVMainRequest *>::const_iterator iter;
    std::vector<NVMAddress> prefetchList;

    for (iter = prefetchBuffer.begin(); iter != prefetchBuffer.end(); iter++)
    {
        if ((*iter)->address.GetPhysicalAddress() == request->address.GetPhysicalAddress())
        {
            if (prefetcher->NotifyAccess(request, prefetchList))
            {
                GeneratePrefetches(request, prefetchList);
            }

            successfulPrefetches++;
            rv = true;
            pfRequest = (*iter);
            delete pfRequest;
            break;
        }
    }

    if (pfRequest != NULL)
    {
        // std::cout << "Prefetched 0x" << std::hex << request->address.GetPhysicalAddress( )
        //           << std::dec << " (list size " << prefetchBuffer.size() << " -> ";
        prefetchBuffer.remove(pfRequest);
        // std::cout << prefetchBuffer.size() << ")" << std::endl;
    }

    return rv;
}

void NVMain::PrintPreTrace(NVMainRequest *request)
{
    /*
     *  Here we can generate a data trace to use with trace-based testing later.
     */
    if (p->PrintPreTrace || p->EchoPreTrace)
    {
        TraceLine tl;

        tl.SetLine(request->address,
                   request->type,
                   GetEventQueue()->GetCurrentCycle(),
                   request->data,
                   request->oldData,
                   request->threadId);

        preTracer->SetNextAccess(&tl);
    }
}

bool NVMain::IssueCommand(NVMainRequest *request)
{
    ncounter_t channel, rank, bank, row, col, subarray;
    ncounter_t cChannel, cRank, cBank, cRow, cCol, cSubarray;
    bool mc_rv;
    
    // traceFile << request->arrivalTick << ",";
    // if(request->type == NVM::READ)
    //     traceFile << "R" << ",";
    // else
    // {
    //     if(request->type == NVM::WRITE)
    //         traceFile << "W" << ",";
    //     else
    //         traceFile << "X" << ",";
    // }
    // traceFile << "0x" << std::hex << request->address.GetPhysicalAddress() << std::dec << ",";
    // traceFile << request->data.GetSize() << std::endl;


    if (!config)
    {
        std::cout << "NVMain: Received request before configuration!\n";
        return false;
    }

    /* Translate the address, then copy to the address struct, and copy to request. */
    GetDecoder()->Translate(request->address.GetPhysicalAddress(),
                            &row, &col, &bank, &rank, &channel, &subarray);
    request->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
    request->bulkCmd = CMD_NOP;

    /* Check for any successful prefetches. */
    if (CheckPrefetch(request))
    {
        GetEventQueue()->InsertEvent(EventResponse, this, request,
                                     GetEventQueue()->GetCurrentCycle() + 1);

        return true;
    }

    if (!(request->flags & (NVMainRequest::FLAG_EVICT | NVMainRequest::FLAG_SKIP_TAG)) && useTagCache)
    {
        ++totalReqs;
        channel = 1;
        request->address.SetPhysicalAddress(GetDecoder()->ReverseTranslate(row, col, bank, rank, channel, subarray));
        request->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
        int present = 0;
        uint64_t cachedAddr = replacementPolicy->get(request->address.GetPhysicalAddress(), &present);
        GetDecoder()->Translate(cachedAddr, &cRow, &cCol, &cBank, &cRank, &cChannel, &cSubarray);
        if (!present) // Need to insert the new data in the DRAM cache
        {
            ++reqsMiss;
            if (request->type == NVM::WRITE)
            // TODO Only writeback if the address is dirty (All blocks should be written back to Main Memory
            //      as all are writes that the NVRAM doesnt have)
            {
                ++WRMiss;
                if (replacementPolicy->isFull()) // When the cache has no more free addresses, a Writeback to NVRAM is needed
                {
                    ++tagReplacements;
                    uint64_t freeAddr = replacementPolicy->removeLRU();

                    replacementPolicy->put(request->address.GetPhysicalAddress(), freeAddr);
                    inverseMap[freeAddr] = request->address.GetPhysicalAddress();

                    // Channel should already be 0
                    GetDecoder()->Translate(freeAddr, &row, &col, &bank, &rank, &channel, &subarray);

                    // Create DRAM Write request to the appropiate address
                    NVMainRequest *DRAMWrite = new NVMainRequest();
                    DRAMWrite->access = UNKNOWN_ACCESS;
                    DRAMWrite->address.SetPhysicalAddress(freeAddr);
                    DRAMWrite->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
                    DRAMWrite->status = MEM_REQUEST_INCOMPLETE;
                    DRAMWrite->type = NVM::WRITE;
                    DRAMWrite->owner = this;
                    DRAMWrite->data = request->data; // Data to be written
                    DRAMWrite->flags |= NVMainRequest::FLAG_EVICT;
                    DRAMWrite->flags |= NVMainRequest::FLAG_SKIP_TAG;
                    waitingDRAMWrite.push_back(DRAMWrite);

                    DRAMWriteSize++;

                    // TODO Change size handling to RequestComplete, this is only temporary.
                    if (DRAMWriteSize == 20)
                        DRAMWriteSize--;

                    // Generate DRAM Read request to the freeAddr Address
                    NVMainRequest *DRead = new NVMainRequest();
                    DRead->access = UNKNOWN_ACCESS;
                    DRead->address.SetPhysicalAddress(freeAddr);
                    DRead->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
                    DRead->status = MEM_REQUEST_INCOMPLETE;
                    DRead->type = READ;
                    DRead->owner = this;
                    DRead->flags |= NVMainRequest::FLAG_EVICT;
                    DRead->data.SetSize(64);

                    // Try to enqueue the new request, but first mark the request as waiting for DRAM Read
                    DRAMReadSize++;
                    // TODO Change size handling to RequestComplete, this is only temporary.
                    if (DRAMReadSize == 20)
                        DRAMReadSize--;
                    this->IssueCommand(DRead);

                    // Disregard the current request
                    return true;
                }
                else // Allocate one memory address
                {
                    ncounter_t rRow, rCol, rBank, rRank, rChannel, rSubarray;
                    GetDecoder()->Translate(tmpAddr, &rRow, &rCol, &rBank, &rRank, &rChannel, &rSubarray);
                    rChannel = 0;
                    uint64_t randAddr = GetDecoder()->ReverseTranslate(rRow, rCol, rBank, rRank, rChannel, rSubarray);

                    while (replacementPolicy->has(randAddr))
                    {
                        tmpAddr+=64;
                        GetDecoder()->Translate(tmpAddr, &rRow, &rCol, &rBank, &rRank, &rChannel, &rSubarray);
                        rChannel = 0;
                        randAddr = GetDecoder()->ReverseTranslate(rRow, rCol, rBank, rRank, rChannel, rSubarray);
                    }
                    tmpAddr+=64;
                    row = rRow;
                    col = rCol;
                    bank = rBank;
                    rank = rRank;
                    channel = rChannel;
                    subarray = rSubarray;

                    replacementPolicy->put(request->address.GetPhysicalAddress(), randAddr);
                    inverseMap.emplace(randAddr, request->address.GetPhysicalAddress());

                    // Update the request target address
                    request->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
                    request->address.SetPhysicalAddress(GetDecoder()->ReverseTranslate(row, col, bank, rank, channel, subarray));
                }
            }
            else
            {
                ++RDMiss;
                // Update the request target address
                request->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
                request->address.SetPhysicalAddress(GetDecoder()->ReverseTranslate(row, col, bank, rank, channel, subarray));
            }
        }
        else // Data already present in DRAM cache, read address.
        {
            ++reqsHit;
            GetDecoder()->Translate(cachedAddr, &row, &col,
                                    &bank, &rank, &channel, &subarray);

            if (request->type == NVM::READ)
                ++RDHit;
            else
            {
                if (request->type == NVM::WRITE)
                    ++WRHit;
            }
            // Update the request target address
            request->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
            request->address.SetPhysicalAddress(GetDecoder()->ReverseTranslate(row, col, bank, rank, channel, subarray));
        }
    }

    assert(GetChild(request)->GetTrampoline() == memoryControllers[channel]);
    mc_rv = GetChild(request)->IssueCommand(request);
    if (mc_rv == true)
    {
        IssuePrefetch(request);

        if (request->type == READ)
        {
            totalReadRequests++;
        }
        else
        {
            totalWriteRequests++;
        }

        PrintPreTrace(request);
    }

    return mc_rv;
}

bool NVMain::IssueAtomic(NVMainRequest *request)
{
    ncounter_t channel, rank, bank, row, col, subarray;
    bool mc_rv;

    if (!config)
    {
        std::cout << "NVMain: Received request before configuration!\n";
        return false;
    }

    /* Translate the address, then copy to the address struct, and copy to request. */
    GetDecoder()->Translate(request->address.GetPhysicalAddress(),
                            &row, &col, &bank, &rank, &channel, &subarray);
    request->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
    request->bulkCmd = CMD_NOP;

    /* Check for any successful prefetches. */
    if (CheckPrefetch(request))
    {
        return true;
    }

    mc_rv = memoryControllers[channel]->IssueAtomic(request);
    if (mc_rv == true)
    {
        IssuePrefetch(request);

        if (request->type == READ)
        {
            totalReadRequests++;
        }
        else
        {
            totalWriteRequests++;
        }

        PrintPreTrace(request);
    }

    return mc_rv;
}

bool NVMain::RequestComplete(NVMainRequest *request)
{
    bool rv = false;

    if ((request->type == NVM::READ) && useTagCache && !(request->flags & NVMainRequest::FLAG_EVICT) &&
        !(request->flags & NVMainRequest::FLAG_SKIP_TAG) && !(request->isPrefetch))
    {
        ++readsOps;
        uint64_t row, col, bank, rank, channel, subarray;
        uint64_t cRow, cCol, cBank, cRank, cChannel, cSubarray;
        GetDecoder()->Translate(request->address.GetPhysicalAddress(), &row, &col, &bank, &rank, &channel, &subarray);
        if (channel != 0)
        {
            ++readsNotDRAM;
            request->address.SetPhysicalAddress(GetDecoder()->ReverseTranslate(row, col, bank, rank, channel, subarray));
            int present = 0;
            uint64_t cachedAddr = replacementPolicy->get(request->address.GetPhysicalAddress(), &present);
            GetDecoder()->Translate(cachedAddr, &cRow, &cCol, &cBank, &cRank, &cChannel, &cSubarray);
            if (!present) // Need to insert the new data in the DRAM cache
            {
                if (replacementPolicy->isFull()) // When the cache has no more free addresses, a Writeback to NVRAM is needed
                {
                    ++RD_Replc;
                    uint64_t freeAddr = replacementPolicy->removeLRU();

                    replacementPolicy->put(request->address.GetPhysicalAddress(), freeAddr);
                    inverseMap[freeAddr] = request->address.GetPhysicalAddress();

                    // Channel should already be 0
                    GetDecoder()->Translate(freeAddr, &row, &col, &bank, &rank, &channel, &subarray);

                    // Create DRAM Write request to the appropiate address
                    NVMainRequest *DRAMWrite = new NVMainRequest();
                    DRAMWrite->access = UNKNOWN_ACCESS;
                    DRAMWrite->address.SetPhysicalAddress(freeAddr);
                    DRAMWrite->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
                    DRAMWrite->status = MEM_REQUEST_INCOMPLETE;
                    DRAMWrite->type = NVM::WRITE;
                    DRAMWrite->owner = this;
                    DRAMWrite->data = request->data; // Data to be written
                    DRAMWrite->flags |= NVMainRequest::FLAG_EVICT;
                    DRAMWrite->flags |= NVMainRequest::FLAG_SKIP_TAG;
                    waitingDRAMWrite.push_back(DRAMWrite);

                    DRAMWriteSize++;

                    // TODO Change size handling to RequestComplete, this is only temporary.
                    if (DRAMWriteSize == 20)
                        DRAMWriteSize--;

                    // Generate DRAM Read request to the freeAddr Address
                    NVMainRequest *DRead = new NVMainRequest();
                    DRead->access = UNKNOWN_ACCESS;
                    DRead->address.SetPhysicalAddress(freeAddr);
                    DRead->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
                    DRead->status = MEM_REQUEST_INCOMPLETE;
                    DRead->type = NVM::READ;
                    DRead->owner = this;
                    DRead->flags |= NVMainRequest::FLAG_EVICT;
                    DRead->data.SetSize(64);

                    // Try to enqueue the new request, but first mark the request as waiting for DRAM Read
                    DRAMReadSize++;
                    // TODO Change size handling to RequestComplete, this is only temporary.
                    if (DRAMReadSize == 20)
                        DRAMReadSize--;
                    this->IssueCommand(DRead);
                }
                else // Allocate one memory address
                {
                    ++RD_Clean;
                    ncounter_t rRow, rCol, rBank, rRank, rChannel, rSubarray;
                    GetDecoder()->Translate(tmpAddr, &rRow, &rCol, &rBank, &rRank, &rChannel, &rSubarray);
                    rChannel = 0;
                    uint64_t randAddr = GetDecoder()->ReverseTranslate(rRow, rCol, rBank, rRank, rChannel, rSubarray);

                    while (replacementPolicy->has(randAddr))
                    {
                        tmpAddr+=64;
                        GetDecoder()->Translate(tmpAddr, &rRow, &rCol, &rBank, &rRank, &rChannel, &rSubarray);
                        rChannel = 0;
                        randAddr = GetDecoder()->ReverseTranslate(rRow, rCol, rBank, rRank, rChannel, rSubarray);
                    }
                    tmpAddr+=64;
                    row = rRow;
                    col = rCol;
                    bank = rBank;
                    rank = rRank;
                    channel = rChannel;
                    subarray = rSubarray;

                    replacementPolicy->put(request->address.GetPhysicalAddress(), randAddr);
                    inverseMap.emplace(randAddr, request->address.GetPhysicalAddress());

                    // Send the request to DRAM directly
                    NVMainRequest *DRAMWrite = new NVMainRequest();
                    DRAMWrite->access = UNKNOWN_ACCESS;
                    DRAMWrite->address.SetPhysicalAddress(randAddr);
                    DRAMWrite->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
                    DRAMWrite->status = MEM_REQUEST_INCOMPLETE;
                    DRAMWrite->type = NVM::WRITE;
                    DRAMWrite->owner = this;
                    DRAMWrite->data = request->data; // Data to be written
                    DRAMWrite->flags |= NVMainRequest::FLAG_EVICT;
                    DRAMWrite->flags |= NVMainRequest::FLAG_SKIP_TAG;

                    DRAMWriteSize++;

                    // TODO Change size handling to RequestComplete, this is only temporary.
                    if (DRAMWriteSize == 20)
                        DRAMWriteSize--;

                    this->IssueCommand(DRAMWrite);
                }
            }
            else // Data already present in DRAM cache, write to that address.
            {
                //TODO Esto puede estar MUY MAL, especialmente porque es un WRITE
                ++RD_Present;
                GetDecoder()->Translate(cachedAddr, &row, &col,
                                        &bank, &rank, &channel, &subarray);

                // Send the request to DRAM directly
                NVMainRequest *DRAMWrite = new NVMainRequest();
                DRAMWrite->access = UNKNOWN_ACCESS;
                DRAMWrite->address.SetPhysicalAddress(GetDecoder()->ReverseTranslate(row, col, bank, rank, channel, subarray));
                DRAMWrite->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
                DRAMWrite->status = MEM_REQUEST_INCOMPLETE;
                DRAMWrite->type = NVM::WRITE;
                DRAMWrite->owner = this;
                DRAMWrite->data = request->data; // Data to be written
                DRAMWrite->flags |= NVMainRequest::FLAG_EVICT;
                DRAMWrite->flags |= NVMainRequest::FLAG_SKIP_TAG;

                DRAMWriteSize++;

                // TODO Change size handling to RequestComplete, this is only temporary.
                if (DRAMWriteSize == 20)
                    DRAMWriteSize--;

                this->IssueCommand(DRAMWrite);
            }
        }
    }

    if (request->owner == this) // All Requests owners should be _this_
    {
        if (request->isPrefetch)
        {
            // std::cout << "Placing 0x" << std::hex << request->address.GetPhysicalAddress( )
            //           << std::dec << " into prefetch buffer (cur size: " << prefetchBuffer.size( )
            //           << ")." << std::endl;

            /* Place in prefetch buffer. */
            if (prefetchBuffer.size() >= p->PrefetchBufferSize)
            {
                unsuccessfulPrefetches++;
                // std::cout << "Prefetch buffer is full. Removing oldest prefetch: 0x" << std::hex
                //           << prefetchBuffer.front()->address.GetPhysicalAddress() << std::dec
                //           << std::endl;

                delete prefetchBuffer.front();
                prefetchBuffer.pop_front();
            }

            prefetchBuffer.push_back(request);
            rv = true;
        }
        else
        {
            if (useTagCache)
            {
                if ((request->flags & NVMainRequest::FLAG_EVICT) && !(request->flags & NVMainRequest::FLAG_SKIP_TAG))
                {
                    if (request->type == NVM::WRITE)
                    {
                    }
                    if (request->type == NVM::READ)
                    {
                        for (auto it = waitingDRAMWrite.begin(); it != waitingDRAMWrite.end(); it++)
                        {
                            if ((*it)->address.GetPhysicalAddress() == request->address.GetPhysicalAddress())
                            {
                                uint64_t row, col, bank, rank, channel, subarray;

                                this->IssueCommand(*it);

                                it = waitingDRAMWrite.erase(it);

                                uint64_t targetAddr = inverseMap[request->address.GetPhysicalAddress()];

                                // Create NVRAM Write request to the appropiate address
                                NVMainRequest *NVWrite = new NVMainRequest();
                                NVWrite->access = UNKNOWN_ACCESS;
                                GetDecoder()->Translate(targetAddr, &row, &col, &bank, &rank, &channel, &subarray);
                                channel = 1; // NVRAM channel
                                targetAddr = GetDecoder()->ReverseTranslate(row, col, bank, rank, channel, subarray);
                                NVWrite->address.SetPhysicalAddress(targetAddr);
                                NVWrite->address.SetTranslatedAddress(row, col, bank, rank, channel, subarray);
                                NVWtoDRW[targetAddr] = request->address.GetPhysicalAddress();
                                NVWrite->status = MEM_REQUEST_INCOMPLETE;
                                NVWrite->type = NVM::WRITE;
                                NVWrite->owner = this;
                                NVWrite->data = request->data; // Read data from DRAM
                                NVWrite->flags |= NVMainRequest::FLAG_EVICT;
                                NVWrite->flags |= NVMainRequest::FLAG_SKIP_TAG;

                                NVRAMWriteSize++;

                                // TODO Cambiar gestion del tamanyo a RequestComplete
                                if (NVRAMWriteSize == 20)
                                    NVRAMWriteSize--;

                                this->IssueCommand(NVWrite);

                                break;
                            }
                        }
                    }
                }
                else
                {
                }
            }
            delete request;
            rv = true;
        }
    }
    else
    {
        rv = GetParent()->RequestComplete(request);
    }

    /* This is used in the main memory system when a DRAMCache is present.
     * DRAMCache misses need to issue main memory requests than might not
     * be issuable at that time. Try to issue these here. */
    if (!pendingMemoryRequests.empty())
    {
        NVMainRequest *staleMemReq = pendingMemoryRequests.front();
        if (IsIssuable(staleMemReq, NULL))
        {
            IssueCommand(staleMemReq);
            pendingMemoryRequests.pop();
        }
    }

    return rv;
}

void NVMain::Cycle(ncycle_t /*steps*/)
{
}

void NVMain::RegisterStats()
{
    AddStat(totalReadRequests);
    AddStat(totalWriteRequests);
    AddStat(successfulPrefetches);
    AddStat(unsuccessfulPrefetches);
    AddStat(totalReqs);
    AddStat(reqsHit);
    AddStat(reqsMiss);
    AddStat(notIssuableTag);
    AddStat(RDHit);
    AddStat(RDMiss);
    AddStat(WRHit);
    AddStat(WRMiss);
    AddStat(tagReplacements);
    AddStat(readsOps);
    AddStat(readsNotDRAM);
    AddStat(RD_Clean);
    AddStat(RD_Present);
    AddStat(RD_Replc);
}

void NVMain::CalculateStats()
{
    std::cout << statName << " totalReqs: " << totalReqs << " Hits: " << reqsHit << " Misses: " << reqsMiss << std::endl;
    std::cout << statName << " Not issued due to IsIssuable( ) " << notIssuableTag << std::endl;
    std::cout << statName << " Read Requests treated: " << readsOps << std::endl;
    for (unsigned int i = 0; i < numChannels; i++)
        memoryControllers[i]->CalculateStats();
}

void NVMain::EnqueuePendingMemoryRequests(NVMainRequest *req)
{
    pendingMemoryRequests.push(req);
}