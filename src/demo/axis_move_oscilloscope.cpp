/*
 * axis_move_oscilloscope.cpp
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
 * PLCOpen Motion Control Oscilloscope Demo
 * Shows timing diagrams of Execute, Busy, Active, Error signals
 *
 */

#include "FbSingleAxis.h"
#include "Scheduler.h"
#include <iomanip>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <string>

using namespace plc_runtime::scheduler;
using std::cout;
using std::endl;
using std::vector;
using std::string;

// Oscilloscope class for displaying timing diagrams
class Oscilloscope {
private:
    struct Signal {
        string name;
        vector<bool> data;
        char high_char;
        char low_char;
        
        Signal(const string& n, char h = '-', char l = ' ') 
            : name(n), high_char(h), low_char(l) {}
    };
    
    vector<Signal> signals;
    int max_samples;
    int current_sample;
    
public:
    Oscilloscope(int samples = 100) : max_samples(samples), current_sample(0) {}
    
    void AddSignal(const string& name, char high_char = '-', char low_char = ' ') {
        signals.push_back(Signal(name, high_char, low_char));
    }
    
    void Sample(const string& name, bool value) {
        for (auto& signal : signals) {
            if (signal.name == name) {
                if (signal.data.size() >= max_samples) {
                    signal.data.erase(signal.data.begin());
                }
                signal.data.push_back(value);
                break;
            }
        }
    }
    
    void Display() {
        // Clear screen (cross-platform)
        #ifdef _WIN32
            system("cls");
        #else
            system("clear");
        #endif
        
        cout << "PLCOpen Motion Control Oscilloscope" << endl;
        cout << "====================================" << endl;
        cout << "Time: " << current_sample << " cycles" << endl << endl;
        
        // Find the maximum signal name length for alignment
        size_t max_name_len = 0;
        for (const auto& signal : signals) {
            max_name_len = std::max(max_name_len, signal.name.length());
        }
        
        // Display each signal
        for (const auto& signal : signals) {
            cout << std::setw(max_name_len + 1) << std::left << signal.name << " ";
            
            // Display the waveform - simulate oscilloscope style
            if (!signal.data.empty()) {
                bool prev_state = false;
                for (size_t i = 0; i < signal.data.size(); ++i) {
                    bool current_state = signal.data[i];
                    
                    if (i == 0) {
                        // First point
                        cout << (current_state ? signal.high_char : signal.low_char);
                    } else {
                        // Check for transitions
                        if (prev_state != current_state) {
                            if (current_state) {
                                cout << "/"; // Rising edge
                            } else {
                                cout << "\\"; // Falling edge
                            }
                        } else {
                            cout << (current_state ? signal.high_char : signal.low_char);
                        }
                    }
                    prev_state = current_state;
                }
            }
            cout << endl;
        }
        
        // Time scale line
        cout << endl;
        cout << string(max_name_len + 1, ' ') << " ";
        if (!signals.empty() && !signals[0].data.empty()) {
            for (size_t i = 0; i < signals[0].data.size(); ++i) {
                if (i % 10 == 0) {
                    cout << "|";
                } else if (i % 5 == 0) {
                    cout << ":";
                } else {
                    cout << " ";
                }
            }
        }
        cout << endl;
        
        cout << endl;
    }
    
    void NextSample() {
        current_sample++;
    }
};

int main(void)
{
    cout.precision(8);

    // Scheduler initialization
    Scheduler sched;
    double frequency = 10; // Slower frequency for better visualization
    int32_t axisId = 1;
    sched.setFrequency(frequency);
    Axis *axis = sched.newAxis(axisId, new Servo());

    // Function block initialization
    FbPower power;
    power.mAxis = axis;
    power.mEnable = true;
    power.mEnablePositive = true;
    power.mEnableNegative = true;

    FbMoveAbsolute move1;
    move1.mAxis = axis;
    move1.mPosition = 500;
    move1.mVelocity = 400;
    move1.mAcceleration = 500;
    move1.mDeceleration = 500;

    FbMoveAbsolute move2;
    move2.mAxis = axis;
    move2.mPosition = 1000;
    move2.mVelocity = 200;
    move2.mAcceleration = 300;
    move2.mDeceleration = 300;

    FbMoveAbsolute move3;
    move3.mAxis = axis;
    move3.mPosition = 200;
    move3.mVelocity = 300;
    move3.mAcceleration = 400;
    move3.mDeceleration = 400;

    // Initialize oscilloscope
    Oscilloscope scope(60);
    scope.AddSignal("Execute", '-', '_');
    scope.AddSignal("Busy", '-', '_');
    scope.AddSignal("Active", '-', '_');
    scope.AddSignal("Done", '-', '_');
    scope.AddSignal("Error", '-', '_');
    scope.AddSignal("CommandAborted", '-', '_');

    double t = 0;
    int cycle_count = 0;
    bool move1_started = false;
    bool move2_started = false;
    bool move3_started = false;
    bool power_on = false;

    // Simulation loop
    while (cycle_count < 200) // Run for 200 cycles
    {
        // Scheduler cycle processing
        sched.runCycle();

        // Function block calls
        power.call();
        move1.call();
        move2.call();
        move3.call();

        // Power on logic
        if (power.mStatus && power.mValid && !power_on) {
            power_on = true;
        }

        // Movement sequence logic
        if (power_on && cycle_count > 10 && !move1_started) {
            move1.mExecute = true;
            move1_started = true;
        }

        if (move1.mDone && cycle_count > 50 && !move2_started) {
            move1.mExecute = false;
            move2.mExecute = true;
            move2_started = true;
        }

        if (move2.mDone && cycle_count > 100 && !move3_started) {
            move2.mExecute = false;
            move3.mExecute = true;
            move3_started = true;
        }

        if (move3.mDone && cycle_count > 150) {
            move3.mExecute = false;
        }

        // Sample signals for oscilloscope
        bool execute_signal = move1.mExecute || move2.mExecute || move3.mExecute;
        bool busy_signal = move1.mBusy || move2.mBusy || move3.mBusy;
        bool active_signal = move1.mActive || move2.mActive || move3.mActive;
        bool done_signal = move1.mDone || move2.mDone || move3.mDone;
        bool error_signal = move1.mError || move2.mError || move3.mError;
        bool aborted_signal = move1.mCommandAborted || move2.mCommandAborted || move3.mCommandAborted;

        scope.Sample("Execute", execute_signal);
        scope.Sample("Busy", busy_signal);
        scope.Sample("Active", active_signal);
        scope.Sample("Done", done_signal);
        scope.Sample("Error", error_signal);
        scope.Sample("CommandAborted", aborted_signal);

        // Display oscilloscope every few cycles
        if (cycle_count % 2 == 0) {
            scope.Display();
            
            // Show current values
            cout << "Current Status:" << endl;
            cout << "  Power: " << (power_on ? "ON" : "OFF") << endl;
            cout << "  Move1: Ex=" << move1.mExecute << " Bu=" << move1.mBusy 
                 << " Ac=" << move1.mActive << " Do=" << move1.mDone << endl;
            cout << "  Move2: Ex=" << move2.mExecute << " Bu=" << move2.mBusy 
                 << " Ac=" << move2.mActive << " Do=" << move2.mDone << endl;
            cout << "  Move3: Ex=" << move3.mExecute << " Bu=" << move3.mBusy 
                 << " Ac=" << move3.mActive << " Do=" << move3.mDone << endl;
            cout << endl;
        }

        scope.NextSample();
        cycle_count++;
        t += 1.0 / frequency;

        // Delay for visualization
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    cout << "\nOscilloscope demo completed!" << endl;
    cout << "Final timing diagram shows the PLCOpen function block state transitions." << endl;

    // Clean up resources
    sched.release();
    
    return 0;
}