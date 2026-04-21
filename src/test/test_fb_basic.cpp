#include <catch2/catch_test_macros.hpp>

#include "FbBasic.h"

using namespace plcopen;

TEST_CASE("R_TRIG emits a one-cycle pulse on a rising edge after cold start", "[fb][basic][trigger]")
{
    FbRTrig block;

    block.mCLK = true;
    block.call();
    REQUIRE_FALSE(block.mQ);

    block.call();
    REQUIRE_FALSE(block.mQ);

    block.mCLK = false;
    block.call();
    REQUIRE_FALSE(block.mQ);

    block.mCLK = true;
    block.call();
    REQUIRE(block.mQ);

    block.call();
    REQUIRE_FALSE(block.mQ);
}

TEST_CASE("F_TRIG emits a one-cycle pulse on a falling edge after cold start", "[fb][basic][trigger]")
{
    FbFTrig block;

    block.call();
    REQUIRE_FALSE(block.mQ);

    block.mCLK = true;
    block.call();
    REQUIRE_FALSE(block.mQ);

    block.mCLK = false;
    block.call();
    REQUIRE(block.mQ);

    block.call();
    REQUIRE_FALSE(block.mQ);
}

TEST_CASE("SR is set-dominant and RS is reset-dominant", "[fb][basic][bistable]")
{
    FbSr sr;
    sr.mS1 = true;
    sr.call();
    REQUIRE(sr.mQ1);

    sr.mS1 = false;
    sr.call();
    REQUIRE(sr.mQ1);

    sr.mS1 = true;
    sr.mR = true;
    sr.call();
    REQUIRE(sr.mQ1);

    sr.mS1 = false;
    sr.call();
    REQUIRE_FALSE(sr.mQ1);

    FbRs rs;
    rs.mS = true;
    rs.call();
    REQUIRE(rs.mQ1);

    rs.mS = false;
    rs.call();
    REQUIRE(rs.mQ1);

    rs.mS = true;
    rs.mR1 = true;
    rs.call();
    REQUIRE_FALSE(rs.mQ1);
}

TEST_CASE("TON sets Q only after IN stays true for PT and clears on falling edge", "[fb][basic][timer]")
{
    FbTon ton;
    REQUIRE(ton.setCycleTime(10));
    ton.mPT = 30;

    ton.mIN = true;
    ton.call();
    REQUIRE_FALSE(ton.mQ);
    REQUIRE(ton.mET == 0);

    ton.call();
    REQUIRE_FALSE(ton.mQ);
    REQUIRE(ton.mET == 10);

    ton.call();
    REQUIRE_FALSE(ton.mQ);
    REQUIRE(ton.mET == 20);

    ton.call();
    REQUIRE(ton.mQ);
    REQUIRE(ton.mET == 30);

    ton.mIN = false;
    ton.call();
    REQUIRE_FALSE(ton.mQ);
    REQUIRE(ton.mET == 0);
}

TEST_CASE("TON with PT zero completes immediately", "[fb][basic][timer][boundary]")
{
    FbTon ton;
    ton.mIN = true;
    ton.mPT = 0;
    ton.call();

    REQUIRE(ton.mQ);
    REQUIRE(ton.mET == 0);
}

TEST_CASE("TOF keeps Q true for PT after IN falls and cancels the off-delay when IN returns", "[fb][basic][timer]")
{
    FbTof tof;
    REQUIRE(tof.setCycleTime(10));
    tof.mPT = 20;

    tof.mIN = true;
    tof.call();
    REQUIRE(tof.mQ);
    REQUIRE(tof.mET == 0);

    tof.mIN = false;
    tof.call();
    REQUIRE(tof.mQ);
    REQUIRE(tof.mET == 0);

    tof.call();
    REQUIRE(tof.mQ);
    REQUIRE(tof.mET == 10);

    tof.mIN = true;
    tof.call();
    REQUIRE(tof.mQ);
    REQUIRE(tof.mET == 0);

    tof.mIN = false;
    tof.call();
    tof.call();
    tof.call();
    REQUIRE_FALSE(tof.mQ);
    REQUIRE(tof.mET == 20);
}

TEST_CASE("TOF with PT zero drops immediately on a falling edge", "[fb][basic][timer][boundary]")
{
    FbTof tof;
    tof.mIN = true;
    tof.call();
    REQUIRE(tof.mQ);

    tof.mIN = false;
    tof.mPT = 0;
    tof.call();
    REQUIRE_FALSE(tof.mQ);
    REQUIRE(tof.mET == 0);
}

TEST_CASE("TP keeps a pulse active for PT and ignores retrigger attempts while active", "[fb][basic][timer]")
{
    FbTp tp;
    REQUIRE(tp.setCycleTime(10));
    tp.mPT = 20;

    tp.mIN = true;
    tp.call();
    REQUIRE_FALSE(tp.mQ);

    tp.mIN = false;
    tp.call();
    REQUIRE_FALSE(tp.mQ);

    tp.mIN = true;
    tp.call();
    REQUIRE(tp.mQ);
    REQUIRE(tp.mET == 0);

    tp.mIN = false;
    tp.call();
    REQUIRE(tp.mQ);
    REQUIRE(tp.mET == 10);

    tp.mIN = true;
    tp.call();
    REQUIRE_FALSE(tp.mQ);
    REQUIRE(tp.mET == 20);

    tp.mIN = false;
    tp.call();
    REQUIRE_FALSE(tp.mQ);
    REQUIRE(tp.mET == 20);
}

TEST_CASE("TP with PT zero does not emit a pulse", "[fb][basic][timer][boundary]")
{
    FbTp tp;

    tp.mIN = true;
    tp.call();
    REQUIRE_FALSE(tp.mQ);

    tp.mIN = false;
    tp.call();

    tp.mIN = true;
    tp.mPT = 0;
    tp.call();
    REQUIRE_FALSE(tp.mQ);
    REQUIRE(tp.mET == 0);
}

TEST_CASE("CTU counts only rising edges and reset clears CV", "[fb][basic][counter]")
{
    FbCtu ctu;
    ctu.mPV = 2;

    ctu.mCU = true;
    ctu.call();
    REQUIRE(ctu.mCV == 0);
    REQUIRE_FALSE(ctu.mQ);

    ctu.mCU = false;
    ctu.call();
    ctu.mCU = true;
    ctu.call();
    REQUIRE(ctu.mCV == 1);
    REQUIRE_FALSE(ctu.mQ);

    ctu.call();
    REQUIRE(ctu.mCV == 1);

    ctu.mCU = false;
    ctu.call();
    ctu.mCU = true;
    ctu.call();
    REQUIRE(ctu.mCV == 2);
    REQUIRE(ctu.mQ);

    ctu.mR = true;
    ctu.call();
    REQUIRE(ctu.mCV == 0);
    REQUIRE_FALSE(ctu.mQ);
}

TEST_CASE("CTU asserts Q immediately when PV is zero", "[fb][basic][counter][boundary]")
{
    FbCtu ctu;
    ctu.call();

    REQUIRE(ctu.mQ);
    REQUIRE(ctu.mCV == 0);
}

TEST_CASE("CTD loads PV and clamps at zero while counting down on rising edges", "[fb][basic][counter]")
{
    FbCtd ctd;
    ctd.mPV = 2;
    ctd.mLD = true;
    ctd.call();
    REQUIRE(ctd.mCV == 2);
    REQUIRE_FALSE(ctd.mQ);

    ctd.mLD = false;
    ctd.mCD = true;
    ctd.call();
    REQUIRE(ctd.mCV == 1);
    REQUIRE_FALSE(ctd.mQ);

    ctd.mCD = false;
    ctd.call();
    ctd.mCD = true;
    ctd.call();
    REQUIRE(ctd.mCV == 0);
    REQUIRE(ctd.mQ);

    ctd.mCD = false;
    ctd.call();
    ctd.mCD = true;
    ctd.call();
    REQUIRE(ctd.mCV == 0);
    REQUIRE(ctd.mQ);

    ctd.mCD = false;
    ctd.call();
    ctd.mCD = true;
    ctd.call();
    REQUIRE(ctd.mCV == 0);
    REQUIRE(ctd.mQ);
}

TEST_CASE("CTUD applies reset then load before count edges and cancels simultaneous edges", "[fb][basic][counter]")
{
    FbCtud ctud;
    ctud.mPV = 2;
    ctud.mLD = true;
    ctud.call();
    REQUIRE(ctud.mCV == 2);
    REQUIRE(ctud.mQU);
    REQUIRE_FALSE(ctud.mQD);

    ctud.mLD = false;
    ctud.mCU = true;
    ctud.mCD = true;
    ctud.call();
    REQUIRE(ctud.mCV == 2);

    ctud.mCU = false;
    ctud.mCD = false;
    ctud.call();

    ctud.mCD = true;
    ctud.call();
    REQUIRE(ctud.mCV == 1);
    REQUIRE_FALSE(ctud.mQD);

    ctud.mCD = false;
    ctud.call();
    ctud.mCD = true;
    ctud.call();
    REQUIRE(ctud.mCV == 0);
    REQUIRE(ctud.mQD);

    ctud.mR = true;
    ctud.mLD = true;
    ctud.call();
    REQUIRE(ctud.mCV == 0);
    REQUIRE(ctud.mQD);
}

TEST_CASE("CTUD does not create cold-start edges when one counter input starts high", "[fb][basic][counter][boundary]")
{
    FbCtud ctud;
    ctud.mPV = 2;
    ctud.mCD = true;
    ctud.call();

    REQUIRE(ctud.mCV == 0);
    REQUIRE_FALSE(ctud.mQU);
    REQUIRE(ctud.mQD);

    ctud.mCD = false;
    ctud.call();
    ctud.mCD = true;
    ctud.call();
    REQUIRE(ctud.mCV == 0);
    REQUIRE(ctud.mQD);

    ctud.mCU = true;
    ctud.call();
    REQUIRE(ctud.mCV == 1);
    REQUIRE_FALSE(ctud.mQU);
}
