/*
 * openDSME
 *
 * Implementation of the Deterministic & Synchronous Multi-channel Extension (DSME)
 * introduced in the IEEE 802.15.4e-2012 standard
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

#ifndef TIMERMULTIPLEXER_H
#define TIMERMULTIPLEXER_H

#include <stdint.h>
#include "TimerAbstractions.h"
#include "dsme_platform.h"

namespace dsme {

template<typename T, typename R, typename G, typename S>
class TimerMultiplexer {
protected:
    typedef T timer_t;
    typedef void (R::*handler_t)(int32_t lateness);

    TimerMultiplexer(R* instance, ReadonlyTimerAbstraction<G> &now, WriteonlyTimerAbstraction<S> &timer) :
            instance(instance),
            _NOW(now),
            _TIMER(timer) {
        for (uint8_t i = 0; i < timer_t::TIMER_COUNT; ++i) {
            this->symbols_until[i] = -1;
            this->handlers[i] = nullptr;
        }
    }

    void _initialize() {
        this->lastDispatchSymbolCounter = _NOW;
    }

    void _timerInterrupt() {
        dispatchEvents();
        _scheduleTimer();
    }

    template<T E>
    inline void _startTimer(uint32_t nextEventSymbolCounter, handler_t handler) {
        if (nextEventSymbolCounter < this->lastDispatchSymbolCounter) {
            /* '-> an event was scheduled too far in the past */
            uint32_t now = _NOW;
            LOG_WARN("now:" << now
                    << ", nextEvent: "<< nextEventSymbolCounter
                    << ", lastDispatch: " << this->lastDispatchSymbolCounter
                    << ", Event " << (uint16_t)E);
            DSME_ASSERT(false);
        }

        this->symbols_until[E] = nextEventSymbolCounter - this->lastDispatchSymbolCounter;
        this->handlers[E] = handler;
        return;
    }

    template<T E>
    inline void _stopTimer() {
        this->symbols_until[E] = -1;
        return;
    }

    void _scheduleTimer() {
        uint32_t symsUntilNextEvent = -1;

        for (uint8_t i = 0; i < timer_t::TIMER_COUNT; ++i) {
            if (0 < this->symbols_until[i] && static_cast<uint32_t>(this->symbols_until[i]) < symsUntilNextEvent) {
                symsUntilNextEvent = symbols_until[i];
            }
        }

        uint32_t timer = this->lastDispatchSymbolCounter + symsUntilNextEvent;

        uint32_t currentSymCnt = _NOW;
        if (timer < currentSymCnt + 2) {
            timer = currentSymCnt + 2;
        }
        _TIMER = timer;

        uint32_t now = _NOW;
        if (timer <= now) {
            LOG_INFO("now: " << now << " timer: " << timer);
            DSME_ASSERT(false);
        }

        return;
    }

private:
    void dispatchEvents() {
        uint32_t currentSymbolCounter = _NOW;

        /* The difference also works if there was a wrap around since lastSymCnt (modulo by casting to uint32_t). */
        int32_t symbolsSinceLastDispatch = (uint32_t) (currentSymbolCounter - this->lastDispatchSymbolCounter);

        for (uint8_t i = 0; i < timer_t::TIMER_COUNT; ++i) {
            if (0 < this->symbols_until[i] && this->symbols_until[i] <= symbolsSinceLastDispatch) {
                int32_t lateness = symbolsSinceLastDispatch - this->symbols_until[i];
                DSME_ASSERT(this->handlers[i] != nullptr);
                (instance->*(this->handlers[i]))(lateness);
            }
        }

        for (uint8_t i = 0; i < timer_t::TIMER_COUNT; ++i) {
            if (this->symbols_until[i] > 0) {
                this->symbols_until[i] -= symbolsSinceLastDispatch;
            }
        }
        this->lastDispatchSymbolCounter = currentSymbolCounter;
        return;
    }

private:
    uint32_t lastDispatchSymbolCounter;
    int32_t symbols_until[timer_t::TIMER_COUNT];
    handler_t handlers[timer_t::TIMER_COUNT];

    R* instance;
    ReadonlyTimerAbstraction<G> &_NOW;
    WriteonlyTimerAbstraction<S> &_TIMER;
};

} /* dsme */

#endif /* TIMERMULTIPLEXER_H */