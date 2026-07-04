#pragma once

#include <cstdint>
#include <limits>

namespace plcopen::core::fb
{

struct RTrig
{
    bool clk = false;
    bool q = false;
    bool memory = false;

    void cycle()
    {
        q = clk && !memory;
        memory = clk;
    }
};

struct FTrig
{
    bool clk = false;
    bool q = false;
    bool memory = false;

    void cycle()
    {
        q = !clk && memory;
        memory = clk;
    }
};

struct SR
{
    bool set = false;
    bool reset = false;
    bool q = false;

    void cycle()
    {
        q = set || (q && !reset);
    }
};

struct RS
{
    bool set = false;
    bool reset = false;
    bool q = false;

    void cycle()
    {
        q = (q || set) && !reset;
    }
};

class TimerBase
{
public:
    bool set_cycle_time(std::int64_t cycles)
    {
        if(cycles <= 0) {
            return false;
        }
        cycle_time_ = cycles;
        return true;
    }

protected:
    std::int64_t cycle_time_ = 1;
};

struct TON : TimerBase
{
    bool in = false;
    bool q = false;
    std::int64_t pt = 0;
    std::int64_t et = 0;

    void cycle()
    {
        if(!in) {
            q = false;
            et = 0;
            return;
        }
        if(pt == 0) {
            q = true;
            et = 0;
            return;
        }
        if(et < pt) {
            et += cycle_time_;
        }
        q = et >= pt;
    }
};

struct TOF : TimerBase
{
    bool in = false;
    bool q = false;
    std::int64_t pt = 0;
    std::int64_t et = 0;

    void cycle()
    {
        if(in) {
            q = true;
            et = 0;
            return;
        }
        if(pt == 0) {
            q = false;
            et = 0;
            return;
        }
        if(q && et < pt) {
            et += cycle_time_;
        }
        q = et < pt;
    }
};

struct TP : TimerBase
{
    bool in = false;
    bool q = false;
    std::int64_t pt = 0;
    std::int64_t et = 0;

    void cycle()
    {
        const bool rising = in && !last_;
        last_ = in;
        if(pt == 0) {
            q = false;
            et = 0;
            active_ = false;
            return;
        }
        if(rising && !active_) {
            active_ = true;
            q = true;
            et = 0;
        }
        if(active_) {
            et += cycle_time_;
            if(et >= pt) {
                active_ = false;
                q = false;
                et = pt;
            }
        }
    }

private:
    bool last_ = false;
    bool active_ = false;
};

struct CTU
{
    bool cu = false;
    bool reset = false;
    bool q = false;
    std::int64_t pv = 0;
    std::int64_t cv = 0;

    void cycle()
    {
        const bool rising = initialized_ && cu && !last_;
        initialized_ = true;
        last_ = cu;
        if(reset) {
            cv = 0;
        } else if(rising && cv < std::numeric_limits<std::int64_t>::max()) {
            ++cv;
        }
        q = cv >= pv;
    }

private:
    bool last_ = false;
    bool initialized_ = false;
};

struct CTD
{
    bool cd = false;
    bool load = false;
    bool q = false;
    std::int64_t pv = 0;
    std::int64_t cv = 0;

    void cycle()
    {
        const bool rising = initialized_ && cd && !last_;
        initialized_ = true;
        last_ = cd;
        if(load) {
            cv = pv;
        } else if(rising && cv > 0) {
            --cv;
        }
        q = cv <= 0;
    }

private:
    bool last_ = false;
    bool initialized_ = false;
};

struct CTUD
{
    bool cu = false;
    bool cd = false;
    bool reset = false;
    bool load = false;
    bool qu = false;
    bool qd = true;
    std::int64_t pv = 0;
    std::int64_t cv = 0;

    void cycle()
    {
        const bool up = initialized_ && cu && !last_up_;
        const bool down = initialized_ && cd && !last_down_;
        initialized_ = true;
        last_up_ = cu;
        last_down_ = cd;

        if(reset) {
            cv = 0;
        } else if(load) {
            cv = pv;
        } else if(up != down) {
            if(up && cv < std::numeric_limits<std::int64_t>::max()) {
                ++cv;
            } else if(down && cv > 0) {
                --cv;
            }
        }
        qu = cv >= pv;
        qd = cv <= 0;
    }

private:
    bool last_up_ = false;
    bool last_down_ = false;
    bool initialized_ = false;
};

struct RTC : TimerBase
{
    bool enable = false;
    bool q = false;
    std::int64_t pdt = 0;
    std::int64_t dt = 0;

    void cycle()
    {
        if(!enable) {
            q = false;
            dt = 0;
            active_ = false;
            return;
        }
        if(!active_) {
            dt = pdt;
            active_ = true;
        } else if(dt <= std::numeric_limits<std::int64_t>::max() - cycle_time_) {
            dt += cycle_time_;
        } else {
            dt = std::numeric_limits<std::int64_t>::max();
        }
        q = true;
    }

private:
    bool active_ = false;
};

} // namespace plcopen::core::fb
