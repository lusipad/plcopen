/*
 * Event.h
 *
 * Copyright 2020 (C) SYMG(Shanghai) Intelligence System Co.,Ltd
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#ifndef PLCOPEN_EVENT_HPP_
#define PLCOPEN_EVENT_HPP_

#include <cstdint>
#include <list>
#include <stdio.h>

namespace plcopen
{

#ifdef PLCOPEN_DEBUGMSG
#define PLCOPEN_MSG(...) printf(__VA_ARGS__)
#else
#define PLCOPEN_MSG(...)
#endif

// TODO: 需要实现内核态兼容的版本，当前使用std::list在内核态不可用，需要调整为自定义容器
#define PLCOPEN_DEFINE_EVENT(Event, ...) std::list<void (*)(__VA_ARGS__)> Event;
#define PLCOPEN_ADD_HANDLER(Event, FuncPtr) Event.push_back(FuncPtr);
#define PLCOPEN_CALL_EVENT(Event, ...) \
    for (auto &f : Event)             \
        (*f)(__VA_ARGS__);

}

#endif /** PLCOPEN_EVENT_HPP_ **/
