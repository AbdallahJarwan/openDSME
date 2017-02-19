/*
 * openDSME
 *
 * Implementation of the Deterministic & Synchronous Multi-channel Extension (DSME)
 * described in the IEEE 802.15.4-2015 standard
 *
 * Authors: Florian Meier <florian.meier@tuhh.de>
 *          Maximilian Koestler <maximilian.koestler@tuhh.de>
 *          Sandrina Backhauss <sandrina.backhauss@tuhh.de>
 *
 * Based on
 *          DSME Implementation for the INET Framework
 *          Tobias Luebkert <tobias.luebkert@tuhh.de>
 *
 * Copyright (c) 2015, Institute of Telematics, Hamburg University of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "GTSController.h"

#include "../../dsme_platform.h"
#include "../mac_services/pib/MAC_PIB.h"
#include "DSMEAdaptionLayer.h"
#include "../dsmeLayer/DSMELayer.h"
#include <algorithm>

constexpr int16_t K_P_POS = 0;
constexpr int16_t K_I_POS = 30;
constexpr int16_t K_D_POS = 26;

constexpr int16_t K_P_NEG = 50;
constexpr int16_t K_I_NEG = 30;
constexpr int16_t K_D_NEG = 38;

constexpr uint16_t SCALING = 128;

namespace dsme {

static bool header = false;

GTSController::GTSController(DSMEAdaptionLayer& dsmeAdaptionLayer) : dsmeAdaptionLayer(dsmeAdaptionLayer) {
    if(!header) {
        std::cerr << "from"
              << "," << "to"
              << "," << "in"
              << "," << "out"
              << "," << "slots"
              << "," << "queue"
              << "," << "maIn"
              << "," << "maServiceTime"
              << "," << "avgServiceTime"
              << "," << "maxServiceTime"
              << "," << "stSlots"
              << "," << "maStSlots"
              << "," << "optSlots"
              << "," << "finOptSlots"
              << "," << "maInTime"
              << "," << "maOutTime"
              << "," << "musuDuration" << std::endl;
        header = true;
    }
}

GTSControllerData::GTSControllerData()
    : address(0xffff), messagesInLastMultisuperframe(0), messagesOutLastMultisuperframe(0), error_sum(0), last_error(0), control(1) {
}

void GTSController::reset() {
    while(this->links.size() > 0) {
        auto it = this->links.begin();
        this->links.remove(it);
    }
}

void GTSController::registerIncomingMessage(uint16_t address) {
    LOG_DEBUG("Controller-Incoming");

    uint32_t now = dsmeAdaptionLayer.getDSME().getPlatform().getSymbolCounter();

    iterator it = this->links.find(address);
    if(it == this->links.end()) {
        GTSControllerData data;
        data.address = address;
        data.messagesInLastMultisuperframe++;
        data.lastIn = now;
        this->links.insert(data, address);
    } else {
        float a = 0.8; // TODO
        it->avgInTime = it->avgInTime*a + (now-it->lastIn) * (1-a);
        it->messagesInLastMultisuperframe++;
        it->lastIn = now;
    }
    return;
}

void GTSController::registerOutgoingMessage(uint16_t address, bool success, int32_t serviceTime) {
    iterator it = this->links.find(address);

    uint32_t now = dsmeAdaptionLayer.getDSME().getPlatform().getSymbolCounter();

    if(it != this->links.end()) {
        it->messagesOutLastMultisuperframe++;

        if(success) {
            //float a = 0.5; // TODO -> adapt to frequency
            float a = 0.95; // TODO -> adapt to frequency

            if(it->serviceTimeCnt == 0) {
                it->maxServiceTime = serviceTime;
            }
            else {
                it->maxServiceTime = std::max(it->maxServiceTime,serviceTime);
            }
            it->serviceTimeSum += serviceTime;
            it->serviceTimeCnt++;
            it->avgServiceTime = it->avgServiceTime*a + (1-a)*serviceTime;
            if(it->lastOut > 0) {
                float a = 0.8; // TODO
                it->avgOutTime = it->avgOutTime*a + (now-it->lastOut) * (1-a);
            }
            it->lastOut = now;
        }
    }

    return;
}

void GTSController::multisuperframeEvent() {
    std::map<uint16_t,uint8_t> slots;
    for (DSMEAllocationCounterTable::iterator it = dsmeAdaptionLayer.getMAC_PIB().macDSMEACT.begin();
                    it != dsmeAdaptionLayer.getMAC_PIB().macDSMEACT.end(); it++) {
        if(it->getState() == ACTState::VALID) {
            if(slots.find(it->getAddress()) == slots.end()) {
                slots[it->getAddress()] = 0;
            }
            slots[it->getAddress()]++;
        }
    }

    uint32_t now = dsmeAdaptionLayer.getDSME().getPlatform().getSymbolCounter();
    uint32_t musuDuration = now-lastMusu;
    lastMusu = now;

    float a = 0.8;

    for(GTSControllerData& data : this->links) {
       data.avgIn = data.avgIn*a + data.messagesInLastMultisuperframe*(1-a);
       // data.control = 1+data.avgIn-slots[data.address];
       auto x = data.avgIn;
//       auto rnd = ((rand()%1000)/1000.0)-0.5; // [-0.5,0.5)
       float rnd = 0;
       float optSlots = -0.03438027*x*x + 1.30657293*x + 0.28796228 + rnd;

       uint16_t w = data.messagesInLastMultisuperframe;
       uint16_t y = data.messagesOutLastMultisuperframe;

       int16_t e = w - y;
       int16_t d = e - data.last_error;
       //std::cerr << "e " << e << " dd: " << d << std::endl;
       //std::cerr << d << ",";
       int16_t& i = data.error_sum;
       int16_t& u = data.control;

       i += e;


       /*
       double expOut = musuDuration/(double)slots[data.address];
//       double expectedServiceTime = i*data.avgOutTime;
       double expectedServiceTime = i*expOut;

       e = i*(d/s)
       s = i*d/e
       */

       double a = 0.95; // TODO -> adapt to frequency
       double stSlots = i*musuDuration/data.avgServiceTime;
       data.maStSlots = a*data.maStSlots + (1-a)*stSlots;

       /*
       double a = 0.95; // TODO -> adapt to frequency
       data.maExpectedServiceTime = data.maExpectedServiceTime*a + (1-a)*expectedServiceTime;
       if(data.avgServiceTime > data.maExpectedServiceTime) {
           optSlots += 1;
       }
       */
       float finOptSlots = std::max((float)stSlots,optSlots);

       data.control = ((int)(finOptSlots+0.5))-slots[data.address];
#if 0
        if(e > 0) {
            u = (K_P_POS * e + K_I_POS * i + K_D_POS * d) / SCALING;
        } else {
            u = (K_P_NEG * e + K_I_NEG * i + K_D_NEG * d) / SCALING;
        }

        LOG_DEBUG_PREFIX;
        LOG_DEBUG_PURE("Controller-Data->" << data.address);
        LOG_DEBUG_PURE("; w: " << (const char*)(" ") << w);
        LOG_DEBUG_PURE("; y: " << (const char*)(" ") << y);
        LOG_DEBUG_PURE("; e: " << (const char*)(e < 0 ? "" : " ") << e);
        LOG_DEBUG_PURE("; i: " << (const char*)(i < 0 ? "" : " ") << i);
        LOG_DEBUG_PURE("; d: " << (const char*)(d < 0 ? "" : " ") << d);
        LOG_DEBUG_PURE("; u: " << (const char*)(u < 0 ? "" : " ") << u);
        LOG_DEBUG_PURE(LOG_ENDL);
        data.last_error = e;
#endif

        std::cerr << dsmeAdaptionLayer.getDSME().getMAC_PIB().macShortAddress
                  << "," << data.address
                  << "," << w
                  << "," << y
                  << "," << slots[data.address]
                  << "," << i
                  << "," << data.avgIn
                  << "," << data.avgServiceTime
                  << "," << data.serviceTimeSum / (float)data.serviceTimeCnt
                  << "," << data.maxServiceTime
                  << "," << stSlots
                  << "," << data.maStSlots
                  << "," << optSlots
                  << "," << finOptSlots
                  << "," << data.avgInTime
                  << "," << data.avgOutTime
                  << "," << musuDuration << std::endl;

        data.messagesInLastMultisuperframe = 0;
        data.messagesOutLastMultisuperframe = 0;
        data.serviceTimeSum = 0;
        data.serviceTimeCnt = 0;
        //data.maxServiceTime = 0;
    }
}

int16_t GTSController::getControl(uint16_t address) {
    iterator it = this->links.find(address);
    DSME_ASSERT(it != this->links.end());

    return it->control;
}

void GTSController::indicateChange(uint16_t address, int16_t change) {
    iterator it = this->links.find(address);
    DSME_ASSERT(it != this->links.end());

    it->control -= change;
    return;
}

static uint16_t abs(int16_t v) {
    if(v > 0) {
        return v;
    } else {
        return -v;
    }
}

uint16_t GTSController::getPriorityLink() {
    uint16_t address = IEEE802154MacAddress::NO_SHORT_ADDRESS;
    int16_t control = 0;
    for(const GTSControllerData& d : this->links) {
        if(abs(control) < abs(d.control)) {
            control = d.control;
            address = d.address;
        }
    }
    return address;
}

} /* dsme */
