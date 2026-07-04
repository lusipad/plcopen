#include "fb/basic.h"

#include <iostream>

int main()
{
    plcopen::core::fb::TON ton;
    ton.set_cycle_time(10);
    ton.pt = 20;
    ton.in = true;
    ton.cycle();
    ton.cycle();

    plcopen::core::fb::CTU counter;
    counter.pv = 1;
    counter.cycle();
    counter.cu = true;
    counter.cycle();

    std::cout << "basic fb demo: TON.Q=" << ton.q << ", CTU.CV=" << counter.cv << '\n';
    return ton.q && counter.cv == 1 ? 0 : 1;
}
