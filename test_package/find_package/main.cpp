#include "FbSingleAxis.h"
#include "Global.h"
#include "Scheduler.h"

static_assert(plcopen::MC_SERVO_EXTENSION_DIGITAL_INPUT_BASE == 10000);
static_assert(plcopen::MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE == 20000);

int main()
{
    plcopen::Scheduler scheduler;
    plcopen::FbPower power;
    (void)scheduler;
    (void)power;
    return 0;
}
