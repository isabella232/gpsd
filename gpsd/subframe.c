/* subframe.c -- interpret satellite subframe data.
 *
 * This file is Copyright 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#include "../include/gpsd_config.h"  /* must be before all includes */

#include <math.h>

#include "../include/gpsd.h"

/* convert unsigned to signed */
#define uint2int(u, bit) ((u & (1<<(bit-1))) ? u - (1<<bit) : u)

// initialize an orbit_t
static void init_orbit(orbit_t *orbit)
{
    orbit->type = 0;
    orbit->sv = 0;
    orbit->AODC = -1;
    orbit->AODE = -1;
    orbit->IODA = -1;
    orbit->IODC = -1;
    orbit->IODE = -1;
    orbit->E5bHS = -1;
    orbit->E1BHS = -1;
    orbit->toa = -1;
    orbit->toc = -1;
    orbit->toe = -1;
    orbit->svh = -1;
    orbit->WN = -1;
    orbit->af0 = NAN;
    orbit->af1 = NAN;
    orbit->af2 = NAN;
    orbit->beta0 = NAN;
    orbit->beta1 = NAN;
    orbit->beta2 = NAN;
    orbit->beta3 = NAN;
    orbit->Cic = NAN;
    orbit->Cis = NAN;
    orbit->Crc = NAN;
    orbit->Crs = NAN;
    orbit->Cuc = NAN;
    orbit->Cus = NAN;
    orbit->eccentricity = NAN;
    orbit->i0 = NAN;
    orbit->M0 = NAN;
    orbit->Omega0 = NAN;
    orbit->Omegad = NAN;
    orbit->omega = NAN;
    orbit->sqrtA = NAN;
}

// init a subrame_t
static void init_subframe(struct subframe_t *subp, uint8_t gnssId,
                          uint8_t tSVID)
{
    memset(subp, 0, sizeof(struct subframe_t));
    subp->gnssId = gnssId;
    subp->tSVID = tSVID;
    subp->WN = -1;
    subp->TOW17 = -1;
    init_orbit(&subp->orbit);
    memcpy(&subp->orbit1, &subp->orbit, sizeof(subp->orbit1));
}

/* you can find up to date almanac data for comparison here:
 * https://gps.afspc.af.mil/gps/Current/current.alm
 *
 * Current ephermeis here, note URL split:
 *  https://cddis.nasa.gov/Data_and_Derived_Products/GNSS/
 *        broadcast_ephemeris_data.html#GPShourly
 */
static void subframe_almanac(const struct gpsd_errout_t *errout,
                             uint8_t tSVID, uint32_t words[],
                             uint8_t subframe, uint8_t sv,
                             uint8_t data_id,
                             struct almanac_t *almp)
{
    almp->sv     = sv; /* ignore the 0 sv problem for now */
    almp->e      = ( words[2] & 0x00FFFF);
    almp->d_eccentricity  = pow(2.0, -21) * almp->e;
    /* careful, each SV can have more than 2 toa's active at the same time
     * you can not just store one or two almanacs for each sat */
    almp->toa      = ((words[3] >> 16) & 0x0000FF);
    almp->l_toa    = almp->toa << 12;
    // Inclination Angle at Reference Time
    // Relative to i0 = 0.30 semi-circles
    almp->deltai   = (int16_t)( words[3] & 0x00FFFF);
    almp->d_deltai = pow(2.0, -19) * almp->deltai;
    // Rate of Right Ascension, semi-circles/sec
    almp->Omegad   = (int16_t)((words[4] >>  8) & 0x00FFFF);
    // -1.19E-07 to 0, semi-circles/sec
    almp->d_Omegad = pow(2.0, -38) * almp->Omegad;
    almp->svh      = ( words[4] & 0x0000FF);
    almp->sqrtA    = ( words[5] & 0xFFFFFF);
    almp->d_sqrtA  = pow(2.0, -11) * almp->sqrtA;
    // Longitude of Ascending Node of Orbit Plane at Weekly Epoch, semi-circles
    // aka Tight Ascen at Week
    almp->Omega0   = ( words[6] & 0xFFFFFF);
    almp->Omega0   = uint2int(almp->Omega0, 24);
    almp->d_Omega0 = pow(2.0, -23) * almp->Omega0;
    // Argument of Perigee, semi-circles
    almp->omega    = ( words[7] & 0xFFFFFF);
    almp->omega    = uint2int(almp->omega, 24);
    almp->d_omega  = pow(2.0, -23) * almp->omega;
    // Mean Anomaly at Reference Time, semi-circles
    almp->M0 = words[8] & 0x00FFFFFF;
    almp->M0 = uint2int(almp->M0, 24);
    /* if you want radians, multiply by GPS_PI, but we do semi-circles
     * to match IS-GPS-200 */
    almp->d_M0     = pow(2.0, -23) * almp->M0;
    // SV Clock Drift Correction Coefficient, seconds/second
    almp->af1      = ((words[9] >>  5) & 0x0007FF);
    almp->af1      = (short)uint2int(almp->af1, 11);
    almp->d_af1    = pow(2.0, -38) * almp->af1;
    // SV Clock Bias Correction Coefficient, seconds
    almp->af0      = ((words[9] >> 16) & 0x0000FF);
    almp->af0    <<= 3;
    almp->af0     |= ((words[9] >>  2) & 0x000007);
    almp->af0      = (short)uint2int(almp->af0, 11);
    almp->d_af0    = pow(2.0, -20) * almp->af0;
    GPSD_LOG(LOG_PROG, errout,
             "50B,GPS: SF:%d SV:%2u TSV:%2u data_id %d e:%g toa:%lu "
             "deltai:%.10e Omegad:%.5e svh:%u sqrtA:%.10g Omega0:%.10e "
             "omega:%.10e M0:%.11e af0:%.5e af1:%.5e\n",
             subframe, almp->sv, tSVID, data_id,
             almp->d_eccentricity,
             almp->l_toa,
             almp->d_deltai,
             almp->d_Omegad,
             almp->svh,
             almp->d_sqrtA,
             almp->d_Omega0,
             almp->d_omega,
             almp->d_M0,
             almp->d_af0,
             almp->d_af1);
}

gps_mask_t gpsd_interpret_subframe(struct gps_device_t *session,
                                   unsigned int gnssId, unsigned int tSVID,
                                   uint32_t words[])
{
    /*
     * Heavy black magic begins here!
     *
     * A description of how to decode these bits is at
     * <http://home-2.worldonline.nl/~samsvl/nav2eu.htm>
     *
     * This functions assumes an array of words without parity or inversion.
     * Inverted word 0 is OK.
     * May be called directly by a driver if the chipset emits acceptable data.
     *
     * To date this code has been tested on iTrax, SiRF and ublox.
     */
    /* FIXME!! I really doubt this is Big Endian compatible */
    uint8_t preamble;
    struct subframe_t *subp = &session->gpsdata.subframe;
    init_subframe(&session->gpsdata.subframe, gnssId, (uint8_t)tSVID);

    GPSD_LOG(LOG_DATA, &session->context->errout,
             "50B,GPS: gpsd_interpret_subframe: (%u, %u) "
             "%06x %06x %06x %06x %06x %06x %06x %06x %06x %06x\n",
             gnssId, tSVID, words[0], words[1], words[2], words[3], words[4],
             words[5], words[6], words[7], words[8], words[9]);

    preamble = (uint8_t)((words[0] >> 16) & 0x0FF);
    if (preamble == 0x8b) {
        /* somehow missed an inversion */
        preamble ^= 0xff;
        words[0] ^= 0xffffff;
    }
    if (preamble != 0x74) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "50B,GPS: gpsd_interpret_subframe bad preamble: "
                 "0x%x header 0x%x\n",
                 preamble, words[0]);
        return 0;
    }
    subp->integrity = (bool)((words[0] >> 1) & 0x01);
    /* The subframe ID is in the Hand Over Word (page 80) */
    // subframe_num is 1 to 5
    subp->subframe_num = ((words[1] >> 2) & 0x07);
    subp->antispoof = (bool)((words[1] >> 5) & 0x01);
    subp->alert = (bool)((words[1] >> 6) & 0x01);
    subp->TOW17 = (long)((words[1] >> 7) & 0x01FFFF) * 6;
    GPSD_LOG(LOG_PROG, &session->context->errout,
             "50B,GPS: SF:%d SV:%2u TOW17:%7d Alert:%u AS:%u IF:%d\n",
             subp->subframe_num, subp->tSVID, subp->TOW17,
             (unsigned)subp->alert, (unsigned)subp->antispoof,
             (unsigned)subp->integrity);
    /*
     * Consult the latest revision of IS-GPS-200 for the mapping
     * between magic SVIDs and pages.
     */
    subp->pageid  = (words[2] >> 16) & 0x00003F; /* only in frames 4 & 5 */
    subp->data_id = (words[2] >> 22) & 0x3;      /* only in frames 4 & 5 */
    subp->is_almanac = 0;

    switch (subp->subframe_num) {
    case 1:
        /* subframe 1: clock parameters for transmitting SV */
        /* get Week Number (WN) from subframe 1 */
        /*
         * This only extracts 10 bits of GPS week.
         * 13 bits are available in the extension CNAV message,
         * which we don't decode yet because we don't know
         * of any receiver that reports it.
         */
        session->context->gps_week =
            (unsigned short)((words[2] >> 14) & 0x03ff);
        subp->sub1.WN   = (uint16_t)session->context->gps_week;
        subp->sub1.l2   = (uint8_t)((words[2] >> 12) & 0x000003); /* L2 Code */
        /* URA Index */
        subp->sub1.ura  = (unsigned int)((words[2] >>  8) & 0x00000F);
        /* SV health */
        subp->sub1.hlth = (unsigned int)((words[2] >>  2) & 0x00003F);
        subp->sub1.IODC = (words[2] & 0x000003); /* IODC 2 MSB */
        subp->sub1.l2p  = ((words[3] >> 23) & 0x000001); /* L2 P flag */
        subp->sub1.Tgd  = (int8_t)( words[6] & 0x0000FF);
        subp->sub1.d_Tgd  = pow(2.0, -31) * (int)subp->sub1.Tgd;
        subp->sub1.toc  = ( words[7] & 0x00FFFF);
        subp->sub1.l_toc = (long)subp->sub1.toc  << 4;
        subp->sub1.af2  = (int8_t)((words[8] >> 16) & 0x0FF);
        subp->sub1.d_af2  = pow(2.0, -55) * (int)subp->sub1.af2;
        subp->sub1.af1  = (int16_t)( words[8] & 0x00FFFF);
        subp->sub1.d_af1  = pow(2.0, -43) * subp->sub1.af1;
        subp->sub1.af0  = (int32_t)((words[9] >>  2) & 0x03FFFFF);
        subp->sub1.af0  = uint2int(subp->sub1.af0, 22);
        subp->sub1.d_af0  = pow(2.0, -31) * subp->sub1.af0;
        subp->sub1.IODC <<= 8;
        subp->sub1.IODC |= ((words[7] >> 16) & 0x00FF);
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "50B,GPS: SF:1 SV:%2u WN:%4u IODC:%4u"
                 " L2:%u ura:%u hlth:%u L2P:%u Tgd:%g toc:%lu af2:%.4g"
                 " af1:%.6e af0:%.7e\n",
                 subp->tSVID,
                 subp->sub1.WN,
                 subp->sub1.IODC,
                 subp->sub1.l2,
                 subp->sub1.ura,
                 subp->sub1.hlth,
                 subp->sub1.l2p,
                 subp->sub1.d_Tgd,
                 subp->sub1.l_toc,
                 subp->sub1.d_af2,
                 subp->sub1.d_af1,
                 subp->sub1.d_af0);
        break;
    case 2:
        /* subframe 2: ephemeris for transmitting SV */
        subp->sub2.IODE   = ((words[2] >> 16) & 0x00FF);
        subp->sub2.Crs    = (int16_t)( words[2] & 0x00FFFF);
        subp->sub2.d_Crs  = pow(2.0, -5) * subp->sub2.Crs;
        subp->sub2.deltan = (int16_t)((words[3] >>  8) & 0x00FFFF);
        subp->sub2.d_deltan  = pow(2.0, -43) * subp->sub2.deltan;
        subp->sub2.M0     = (int32_t)( words[3] & 0x0000FF);
        subp->sub2.M0   <<= 24;
        subp->sub2.M0    |= ( words[4] & 0x00FFFFFF);
        /* if you want radians, multiply by GPS_PI, but we do semi-circles
         * to match IS-GPS-200 */
        subp->sub2.d_M0   = pow(2.0, -31) * subp->sub2.M0;
        subp->sub2.Cuc    = (int16_t)((words[5] >>  8) & 0x00FFFF);
        subp->sub2.d_Cuc  = pow(2.0, -29) * subp->sub2.Cuc;
        subp->sub2.e      = ( words[5] & 0x0000FF);
        subp->sub2.e    <<= 24;
        subp->sub2.e     |= ( words[6] & 0x00FFFFFF);
        subp->sub2.d_eccentricity  = pow(2.0, -33) * subp->sub2.e;
        subp->sub2.Cus    = (int16_t)((words[7] >>  8) & 0x00FFFF);
        subp->sub2.d_Cus  = pow(2.0, -29) * subp->sub2.Cus;
        subp->sub2.sqrtA  = ( words[7] & 0x0000FF);
        subp->sub2.sqrtA <<= 24;
        subp->sub2.sqrtA |= ( words[8] & 0x00FFFFFF);
        subp->sub2.d_sqrtA = pow(2.0, -19) * subp->sub2.sqrtA;
        subp->sub2.toe    = ((words[9] >>  8) & 0x00FFFF);
        subp->sub2.l_toe  = ((unsigned long)subp->sub2.toe << 4);
        subp->sub2.fit    = ((words[9] >>  7) & 0x000001);
        subp->sub2.AODO   = ((words[9] >>  2) & 0x00001F);
        subp->sub2.u_AODO   = subp->sub2.AODO * 900;
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "50B,GPS: SF:2 SV:%2u IODE:%3u Crs:%.6e deltan:%.6e "
                 "M0:%.11e Cuc:%.6e e:%f Cus:%.6e sqrtA:%.11g "
                 "toe:%lu FIT:%u AODO:%5u\n",
                 subp->tSVID,
                 subp->sub2.IODE,
                 subp->sub2.d_Crs,
                 subp->sub2.d_deltan,
                 subp->sub2.d_M0,
                 subp->sub2.d_Cuc,
                 subp->sub2.d_eccentricity,
                 subp->sub2.d_Cus,
                 subp->sub2.d_sqrtA,
                 subp->sub2.l_toe,
                 subp->sub2.fit,
                 subp->sub2.u_AODO);
        break;
    case 3:
        /* subframe 3: ephemeris for transmitting SV */
        subp->sub3.Cic      = (int16_t)((words[2] >>  8) & 0x00FFFF);
        subp->sub3.d_Cic    = pow(2.0, -29) * subp->sub3.Cic;
        subp->sub3.Omega0   = (int32_t)(words[2] & 0x0000FF);
        subp->sub3.Omega0 <<= 24;
        subp->sub3.Omega0  |= ( words[3] & 0x00FFFFFF);
        subp->sub3.d_Omega0 = pow(2.0, -31) * subp->sub3.Omega0;
        subp->sub3.Cis      = (int16_t)((words[4] >>  8) & 0x00FFFF);
        subp->sub3.d_Cis    = pow(2.0, -29) * subp->sub3.Cis;
        subp->sub3.i0       = (int32_t)(words[4] & 0x0000FF);
        subp->sub3.i0     <<= 24;
        subp->sub3.i0      |= ( words[5] & 0x00FFFFFF);
        subp->sub3.d_i0     = pow(2.0, -31) * subp->sub3.i0;
        subp->sub3.Crc      = (int16_t)((words[6] >>  8) & 0x00FFFF);
        subp->sub3.d_Crc    = pow(2.0, -5) * subp->sub3.Crc;
        subp->sub3.omega    = (int32_t)(words[6] & 0x0000FF);
        subp->sub3.omega  <<= 24;
        subp->sub3.omega   |= ( words[7] & 0x00FFFFFF);
        subp->sub3.d_omega  = pow(2.0, -31) * subp->sub3.omega;
        // Rate of Right Ascension
        subp->sub3.Omegad   = (int32_t)(words[8] & 0x00FFFFFF);
        subp->sub3.Omegad   = uint2int(subp->sub3.Omegad, 24);
        // -6.33E-07 to 0, semi-circles/sec
        subp->sub3.d_Omegad = pow(2.0, -43) * subp->sub3.Omegad;
        subp->sub3.IODE     = ((words[9] >> 16) & 0x0000FF);
        subp->sub3.IDOT     = (int16_t)((words[9] >>  2) & 0x003FFF);
        subp->sub3.IDOT     = uint2int(subp->sub3.IDOT, 14);
        subp->sub3.d_IDOT   = pow(2.0, -43) * subp->sub3.IDOT;
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "50B,GPS: SF:3 SV:%2u IODE:%3u I IDOT:%.6g Cic:%.6e "
                 "Omega0:%.11e Cis:%.7g i0:%.11e Crc:%.7g omega:%.11e "
                 "Omegad:%.6e\n",
                 subp->tSVID, subp->sub3.IODE, subp->sub3.d_IDOT,
                 subp->sub3.d_Cic, subp->sub3.d_Omega0, subp->sub3.d_Cis,
                 subp->sub3.d_i0, subp->sub3.d_Crc, subp->sub3.d_omega,
                 subp->sub3.d_Omegad );
        break;
    case 4:
        {
            int i = 0;   /* handy loop counter */
            int sv = -2;
            switch (subp->pageid) {
            case 0:
                /* almanac for dummy sat 0, which is same as transmitting sat */
                sv = 0;
                break;

            /* almanac data for SV 25 through 32 respectively; */
            case 25:         // aka page 2:
                sv = 25;
                break;
            case 26:         // aka page 3:
                sv = 26;
                break;
            case 27:         // aka page 4:
                sv = 27;
                break;
            case 28:         // aka page 5:
                sv = 28;
                break;
            case 29:         // aka page 7
                sv = 29;
                break;
            case 30:         // aka page 8
                sv = 30;
                break;
            case 31:         // aka page 9
                sv = 31;
                break;
            case 32:         // aka page 10
                sv = 32;
                break;

            case 52:                  // aka page 13
                /* NMCT */
                // ERD can not be char as char may be signed or unsigned.
                // FIXME: shuffle SV into correct slot.
                sv = -1;
                subp->sub4_13.ai = (unsigned char)((words[2] >> 22) & 0x000003);
                subp->sub4_13.ERD[1]  = (int8_t)((words[2] >>  8) & 0x00003F);
                subp->sub4_13.ERD[2]  = (int8_t)((words[2] >>  2) & 0x00003F);
                subp->sub4_13.ERD[3]  = (int8_t)((words[2] >>  0) & 0x000003);
                subp->sub4_13.ERD[3] <<= 2;
                subp->sub4_13.ERD[3] |= (int8_t)((words[3] >> 20) & 0x00000F);

                subp->sub4_13.ERD[4]  = (int8_t)((words[3] >> 14) & 0x00003F);
                subp->sub4_13.ERD[5]  = (int8_t)((words[3] >>  8) & 0x00003F);
                subp->sub4_13.ERD[6]  = (int8_t)((words[3] >>  2) & 0x00003F);
                subp->sub4_13.ERD[7]  = (int8_t)((words[3] >>  0) & 0x000003);

                subp->sub4_13.ERD[7] <<= 2;
                subp->sub4_13.ERD[7] |= (int8_t)((words[4] >> 20) & 0x00000F);
                subp->sub4_13.ERD[8]  = (int8_t)((words[4] >> 14) & 0x00003F);
                subp->sub4_13.ERD[9]  = (int8_t)((words[4] >>  8) & 0x00003F);
                subp->sub4_13.ERD[10] = (int8_t)((words[4] >>  2) & 0x00003F);
                subp->sub4_13.ERD[11] = (int8_t)((words[4] >>  0) & 0x00000F);

                subp->sub4_13.ERD[11] <<= 2;
                subp->sub4_13.ERD[11] |= (int8_t)((words[5] >> 20) & 0x00000F);
                subp->sub4_13.ERD[12]  = (int8_t)((words[5] >> 14) & 0x00003F);
                subp->sub4_13.ERD[13]  = (int8_t)((words[5] >>  8) & 0x00003F);
                subp->sub4_13.ERD[14]  = (int8_t)((words[5] >>  2) & 0x00003F);
                subp->sub4_13.ERD[15]  = (int8_t)((words[5] >>  0) & 0x000003);

                subp->sub4_13.ERD[15] <<= 2;
                subp->sub4_13.ERD[15] |= (int8_t)((words[6] >> 20) & 0x00000F);
                subp->sub4_13.ERD[16]  = (int8_t)((words[6] >> 14) & 0x00003F);
                subp->sub4_13.ERD[17]  = (int8_t)((words[6] >>  8) & 0x00003F);
                subp->sub4_13.ERD[18]  = (int8_t)((words[6] >>  2) & 0x00003F);
                subp->sub4_13.ERD[19]  = (int8_t)((words[6] >>  0) & 0x000003);

                subp->sub4_13.ERD[19] <<= 2;
                subp->sub4_13.ERD[19] |= (int8_t)((words[7] >> 20) & 0x00000F);
                subp->sub4_13.ERD[20]  = (int8_t)((words[7] >> 14) & 0x00003F);
                subp->sub4_13.ERD[21]  = (int8_t)((words[7] >>  8) & 0x00003F);
                subp->sub4_13.ERD[22]  = (int8_t)((words[7] >>  2) & 0x00003F);
                subp->sub4_13.ERD[23]  = (int8_t)((words[7] >>  0) & 0x000003);

                subp->sub4_13.ERD[23] <<= 2;
                subp->sub4_13.ERD[23] |= (int8_t)((words[8] >> 20) & 0x00000F);
                subp->sub4_13.ERD[24]  = (int8_t)((words[8] >> 14) & 0x00003F);
                subp->sub4_13.ERD[25]  = (int8_t)((words[8] >>  8) & 0x00003F);
                subp->sub4_13.ERD[26]  = (int8_t)((words[8] >>  2) & 0x00003F);
                subp->sub4_13.ERD[27]  = (int8_t)((words[8] >>  0) & 0x000003);

                subp->sub4_13.ERD[27] <<= 2;
                subp->sub4_13.ERD[27] |= (int8_t)((words[9] >> 20) & 0x00000F);
                subp->sub4_13.ERD[28]  = (int8_t)((words[9] >> 14) & 0x00003F);
                subp->sub4_13.ERD[29]  = (int8_t)((words[9] >>  8) & 0x00003F);
                subp->sub4_13.ERD[30]  = (int8_t)((words[9] >>  2) & 0x00003F);

                for (i = 1; i < 31; i++) {
                    // sign extend 6 bit to 8 bit
                    subp->sub4_13.ERD[i]  = uint2int(subp->sub4_13.ERD[i], 6);
                }
                // ERD for SV 32 never sent, test for it to shut up coverity.
                if (32 > subp->tSVID) {
                    // own ERD never transmitted
                    for (i = 30; i >= subp->tSVID; i--) {
                        // do the shuffle up thing
                        subp->sub4_13.ERD[i + 1]  = subp->sub4_13.ERD[i];
                    }
                    // 0x20 sign extends to 0xe0, 0xe0 is -32
                    subp->sub4_13.ERD[subp->tSVID] = -32;
                }

                GPSD_LOG(LOG_PROG, &session->context->errout,
                         "50B,GPS: SF:4-13 data_id %d ai:%u "
                         "ERD1:%d ERD2:%d ERD3:%d ERD4:%d "
                         "ERD5:%d ERD6:%d ERD7:%d ERD8:%d "
                         "ERD9:%d ERD10:%d ERD11:%d ERD12:%d "
                         "ERD13:%d ERD14:%d ERD15:%d ERD16:%d "
                         "ERD17:%d ERD18:%d ERD19:%d ERD20:%d "
                         "ERD21:%d ERD22:%d ERD23:%d ERD24:%d "
                         "ERD25:%d ERD26:%d ERD27:%d ERD28:%d "
                         "ERD29:%d ERD30:%d ERD31:%d\n",
                         subp->data_id, subp->sub4_13.ai,
                         subp->sub4_13.ERD[1], subp->sub4_13.ERD[2],
                         subp->sub4_13.ERD[3], subp->sub4_13.ERD[4],
                         subp->sub4_13.ERD[5], subp->sub4_13.ERD[6],
                         subp->sub4_13.ERD[7], subp->sub4_13.ERD[8],
                         subp->sub4_13.ERD[9], subp->sub4_13.ERD[10],
                         subp->sub4_13.ERD[11], subp->sub4_13.ERD[12],
                         subp->sub4_13.ERD[13], subp->sub4_13.ERD[14],
                         subp->sub4_13.ERD[15], subp->sub4_13.ERD[16],
                         subp->sub4_13.ERD[17], subp->sub4_13.ERD[18],
                         subp->sub4_13.ERD[19], subp->sub4_13.ERD[20],
                         subp->sub4_13.ERD[21], subp->sub4_13.ERD[22],
                         subp->sub4_13.ERD[23], subp->sub4_13.ERD[24],
                         subp->sub4_13.ERD[25], subp->sub4_13.ERD[26],
                         subp->sub4_13.ERD[27], subp->sub4_13.ERD[28],
                         subp->sub4_13.ERD[29], subp->sub4_13.ERD[30],
                         subp->sub4_13.ERD[31]);
                break;

            case 53:     // aka page 14
                /* for some inscrutable reason page 14 is sent
                 * as page 53, IS-GPS-200 Table 20-
                 * reserved */
                break;

            case 54:      // aka page 15
                /* for some inscrutable reason page 15 is sent
                 * as page 54, IS-GPS-200 Table 20-V
                 * reserved */
                break;

            case 55:  // aka page 17
                /* for some inscrutable reason page 17 is sent
                 * as page 55, IS-GPS-200 Table 20-V */
                sv = -1;
                /*
                 * "The requisite 176 bits shall occupy bits 9 through 24
                 * of word TWO, the 24 MSBs of words THREE through EIGHT,
                 * plus the 16 MSBs of word NINE." (word numbers changed
                 * to account for zero-indexing)
                 * Since we've already stripped the low six parity bits,
                 * and shifted the data to a byte boundary, we can just
                 * copy it out. */

                i = 0;
                subp->sub4_17.str[i++] = (words[2] >> 8) & 0xff;
                subp->sub4_17.str[i++] = (words[2]) & 0xff;

                subp->sub4_17.str[i++] = (words[3] >> 16) & 0xff;
                subp->sub4_17.str[i++] = (words[3] >> 8) & 0xff;
                subp->sub4_17.str[i++] = (words[3]) & 0xff;

                subp->sub4_17.str[i++] = (words[4] >> 16) & 0xff;
                subp->sub4_17.str[i++] = (words[4] >> 8) & 0xff;
                subp->sub4_17.str[i++] = (words[4]) & 0xff;

                subp->sub4_17.str[i++] = (words[5] >> 16) & 0xff;
                subp->sub4_17.str[i++] = (words[5] >> 8) & 0xff;
                subp->sub4_17.str[i++] = (words[5]) & 0xff;

                subp->sub4_17.str[i++] = (words[6] >> 16) & 0xff;
                subp->sub4_17.str[i++] = (words[6] >> 8) & 0xff;
                subp->sub4_17.str[i++] = (words[6]) & 0xff;

                subp->sub4_17.str[i++] = (words[7] >> 16) & 0xff;
                subp->sub4_17.str[i++] = (words[7] >> 8) & 0xff;
                subp->sub4_17.str[i++] = (words[7]) & 0xff;

                subp->sub4_17.str[i++] = (words[8] >> 16) & 0xff;
                subp->sub4_17.str[i++] = (words[8] >> 8) & 0xff;
                subp->sub4_17.str[i++] = (words[8]) & 0xff;

                subp->sub4_17.str[i++] = (words[9] >> 16) & 0xff;
                subp->sub4_17.str[i++] = (words[9] >> 8) & 0xff;
                subp->sub4_17.str[i] = '\0';
                GPSD_LOG(LOG_PROG, &session->context->errout,
                         "50B,GPS: SF:4-17 system message: %.24s\n",
                         subp->sub4_17.str);
                break;

            case 56:         // aka page 18
                /* for some inscrutable reason page 18 is sent
                 * as page 56, IS-GPS-200 Table 20-V */
                /* ionospheric and UTC data */

                sv = -1;
                /* current leap seconds */
                subp->sub4_18.alpha0 = (int8_t)((words[2] >> 8) & 0x0000FF);
                subp->sub4_18.d_alpha0 = pow(2.0, -30) *
                                             (int)subp->sub4_18.alpha0;
                subp->sub4_18.alpha1 = (int8_t)((words[2] >> 0) & 0x0000FF);
                subp->sub4_18.d_alpha1 = pow(2.0, -27) *
                                            (int)subp->sub4_18.alpha1;
                subp->sub4_18.alpha2 = (int8_t)((words[3] >> 16) & 0x0000FF);
                subp->sub4_18.d_alpha2 = pow(2.0, -24) *
                                            (int)subp->sub4_18.alpha2;
                subp->sub4_18.alpha3 = (int8_t)((words[3] >>  8) & 0x0000FF);
                subp->sub4_18.d_alpha3 = pow(2.0, -24) *
                                            (int)subp->sub4_18.alpha3;

                subp->sub4_18.beta0  = (int8_t)((words[3] >>  0) & 0x0000FF);
                subp->sub4_18.d_beta0 = pow(2.0, 11) * (int)subp->sub4_18.beta0;
                subp->sub4_18.beta1  = (int8_t)((words[4] >> 16) & 0x0000FF);
                subp->sub4_18.d_beta1 = pow(2.0, 14) * (int)subp->sub4_18.beta1;
                subp->sub4_18.beta2  = (int8_t)((words[4] >>  8) & 0x0000FF);
                subp->sub4_18.d_beta2 = pow(2.0, 16) * (int)subp->sub4_18.beta2;
                subp->sub4_18.beta3  = (int8_t)((words[4] >>  0) & 0x0000FF);
                subp->sub4_18.d_beta3 = pow(2.0, 16) * (int)subp->sub4_18.beta3;

                subp->sub4_18.A1     = (int32_t)((words[5] >>  0) & 0xFFFFFF);
                subp->sub4_18.A1     = uint2int(subp->sub4_18.A1, 24);
                subp->sub4_18.d_A1   = pow(2.0, -50) * subp->sub4_18.A1;
                subp->sub4_18.A0     = (int32_t)((words[6] >>  0) & 0xFFFFFF);
                subp->sub4_18.A0   <<= 8;
                subp->sub4_18.A0    |= ((words[7] >> 16) & 0x0000FF);
                subp->sub4_18.d_A0   = pow(2.0, -30) * subp->sub4_18.A0;

                /* careful WN is 10 bits, but WNt is 8 bits! */
                /* WNt (Week Number of LSF) */
                subp->sub4_18.tot    = ((words[7] >> 8) & 0x0000FF);
                subp->sub4_18.t_tot  = (unsigned long)subp->sub4_18.tot << 12;
                subp->sub4_18.WNt    = ((words[7] >> 0) & 0x0000FF);
                subp->sub4_18.leap  = (int8_t)((words[8] >> 16) & 0x0000FF);
                subp->sub4_18.WNlsf  = ((words[8] >>  8) & 0x0000FF);

                /* DN (Day Number of LSF) */
                subp->sub4_18.DN = (words[8] & 0x0000FF);
                /* leap second future */
                subp->sub4_18.lsf = (int8_t)((words[9] >> 16) & 0x0000FF);

                GPSD_LOG(LOG_PROG, &session->context->errout,
                         "50B,GPS: SF:4-18 a0:%.5g a1:%.5g a2:%.5g a3:%.5g "
                         "b0:%.5g b1:%.5g b2:%.5g b3:%.5g "
                         "A1:%.11e A0:%.11e tot:%lld WNt:%u "
                         "ls: %d WNlsf:%u DN:%u, lsf:%d\n",
                         subp->sub4_18.d_alpha0, subp->sub4_18.d_alpha1,
                         subp->sub4_18.d_alpha2, subp->sub4_18.d_alpha3,
                         subp->sub4_18.d_beta0, subp->sub4_18.d_beta1,
                         subp->sub4_18.d_beta2, subp->sub4_18.d_beta3,
                         subp->sub4_18.d_A1, subp->sub4_18.d_A0,
                         (long long)subp->sub4_18.t_tot, subp->sub4_18.WNt,
                         subp->sub4_18.leap, subp->sub4_18.WNlsf,
                         subp->sub4_18.DN, subp->sub4_18.lsf);

                /* notify the leap seconds correction in the end
                 * of current day */
                /* IS-GPS-200, paragraph 20.3.3.5.2.4 */
                /* FIXME: only allow LEAPs in June and December */
                // only need to check whole seconds
                if (((session->context->gps_week % 256) ==
                     (unsigned short)subp->sub4_18.WNlsf) &&
                    (((subp->sub4_18.DN - 1) * SECS_PER_DAY) <
                     session->context->gps_tow.tv_sec) &&
                    ((subp->sub4_18.DN * SECS_PER_DAY) >
                     session->context->gps_tow.tv_sec)) {

                   if (subp->sub4_18.leap < subp->sub4_18.lsf) {
                        session->context->leap_notify = LEAP_ADDSECOND;
                   } else if (subp->sub4_18.leap > subp->sub4_18.lsf) {
                        session->context->leap_notify = LEAP_DELSECOND;
                   } else {
                        session->context->leap_notify = LEAP_NOWARNING;
                   }
                } else {
                   session->context->leap_notify = LEAP_NOWARNING;
                }

                session->context->leap_seconds = (int)subp->sub4_18.leap;
                session->context->valid |= LEAP_SECOND_VALID;
                break;

            case 57:    // aka pages 1, 6, 11, 16, 21
                /* for some inscutable reason these pages are all sent
                 * as page 57, IS-GPS-200 Table 20-V
                 * reserved */
                break;

            case 59:         // aka page 20
                /* for some inscrutable reason page 20 is sent
                 * as page 59, IS-GPS-200 Table 20-V
                 * reserved page */
                break;

            case 60:         // aka page 22
                /* for some inscrutable reason page 22 is sent
                 * as page 60, IS-GPS-200 Table 20-V */
                /* reserved page */
                break;

            case 61:         // aka page 23
                /* for some inscrutable reason page 23 is sent
                 * as page 61, IS-GPS-200 Table 20-V */
                /* reserved page */
                break;

            case 62:     // aka pages 12 and 24
                /* for some inscrutable reason these pages are all sent
                 * as page 62, IS-GPS-200 Table 20-V
                 * reserved */
                break;

            case 63:          // aka page 25
                /* for some inscrutable reason page 25 is sent
                 * as page 63, IS-GPS-200 Table 20-V */
                /* A-S flags/SV configurations for 32 SVs,
                 * plus SV health for SV 25 through 32
                 */

                sv = -1;
                subp->sub4_25.svf[1] = (unsigned char)((words[2] >> 12) & 0x0F);
                subp->sub4_25.svf[2] = (unsigned char)((words[2] >>  8) & 0x0F);
                subp->sub4_25.svf[3] = (unsigned char)((words[2] >>  4) & 0x0F);
                subp->sub4_25.svf[4] = (unsigned char)((words[2] >>  0) & 0x0F);
                subp->sub4_25.svf[5] = (unsigned char)((words[3] >> 20) & 0x0F);
                subp->sub4_25.svf[6] = (unsigned char)((words[3] >> 16) & 0x0F);
                subp->sub4_25.svf[7] = (unsigned char)((words[3] >> 12) & 0x0F);
                subp->sub4_25.svf[8] = (unsigned char)((words[3] >>  8) & 0x0F);
                subp->sub4_25.svf[9] = (unsigned char)((words[3] >>  4) & 0x0F);
                subp->sub4_25.svf[10] = (unsigned char)((words[3] >> 0) & 0x0F);
                subp->sub4_25.svf[11] = (unsigned char)((words[4] >> 20) & 0x0F);
                subp->sub4_25.svf[12] = (unsigned char)((words[4] >> 16) & 0x0F);
                subp->sub4_25.svf[13] = (unsigned char)((words[4] >> 12) & 0x0F);
                subp->sub4_25.svf[14] = (unsigned char)((words[4] >> 8) & 0x0F);
                subp->sub4_25.svf[15] = (unsigned char)((words[4] >> 4) & 0x0F);
                subp->sub4_25.svf[16] = (unsigned char)((words[4] >> 0) & 0x0F);
                subp->sub4_25.svf[17] = (unsigned char)((words[5] >> 20) & 0x0F);
                subp->sub4_25.svf[18] = (unsigned char)((words[5] >> 16) & 0x0F);
                subp->sub4_25.svf[19] = (unsigned char)((words[5] >> 12) & 0x0F);
                subp->sub4_25.svf[20] = (unsigned char)((words[5] >> 8) & 0x0F);
                subp->sub4_25.svf[21] = (unsigned char)((words[5] >> 4) & 0x0F);
                subp->sub4_25.svf[22] = (unsigned char)((words[5] >> 0) & 0x0F);
                subp->sub4_25.svf[23] = (unsigned char)((words[6] >> 20) & 0x0F);
                subp->sub4_25.svf[24] = (unsigned char)((words[6] >> 16) & 0x0F);
                subp->sub4_25.svf[25] = (unsigned char)((words[6] >> 12) & 0x0F);
                subp->sub4_25.svf[26] = (unsigned char)((words[6] >> 8) & 0x0F);
                subp->sub4_25.svf[27] = (unsigned char)((words[6] >> 4) & 0x0F);
                subp->sub4_25.svf[28] = (unsigned char)((words[6] >> 0) & 0x0F);
                subp->sub4_25.svf[29] = (unsigned char)((words[7] >> 20) & 0x0F);
                subp->sub4_25.svf[30] = (unsigned char)((words[7] >> 16) & 0x0F);
                subp->sub4_25.svf[31] = (unsigned char)((words[7] >> 12) & 0x0F);
                subp->sub4_25.svf[32] = (unsigned char)((words[7] >> 8) & 0x0F);

                subp->sub4_25.svhx[0] = ((words[7] >>  0) & 0x00003F);
                subp->sub4_25.svhx[1] = ((words[8] >> 18) & 0x00003F);
                subp->sub4_25.svhx[2] = ((words[8] >> 12) & 0x00003F);
                subp->sub4_25.svhx[3] = ((words[8] >>  6) & 0x00003F);
                subp->sub4_25.svhx[4] = ((words[8] >>  0) & 0x00003F);
                subp->sub4_25.svhx[5] = ((words[9] >> 18) & 0x00003F);
                subp->sub4_25.svhx[6] = ((words[9] >> 12) & 0x00003F);
                subp->sub4_25.svhx[7] = ((words[9] >>  6) & 0x00003F);

                GPSD_LOG(LOG_PROG, &session->context->errout,
                         "50B,GPS: SF:4-25 data_id %d "
                         "SV1:%u SV2:%u SV3:%u SV4:%u "
                         "SV5:%u SV6:%u SV7:%u SV8:%u "
                         "SV9:%u SV10:%u SV11:%u SV12:%u "
                         "SV13:%u SV14:%u SV15:%u SV16:%u "
                         "SV17:%u SV18:%u SV19:%u SV20:%u "
                         "SV21:%u SV22:%u SV23:%u SV24:%u "
                         "SV25:%u SV26:%u SV27:%u SV28:%u "
                         "SV29:%u SV30:%u SV31:%u SV32:%u "
                         "SVH25:%u SVH26:%u SVH27:%u SVH28:%u "
                         "SVH29:%u SVH30:%u SVH31:%u SVH32:%u\n",
                         subp->data_id,
                         subp->sub4_25.svf[1],  subp->sub4_25.svf[2],
                         subp->sub4_25.svf[3],  subp->sub4_25.svf[4],
                         subp->sub4_25.svf[5],  subp->sub4_25.svf[6],
                         subp->sub4_25.svf[7],  subp->sub4_25.svf[8],
                         subp->sub4_25.svf[9],  subp->sub4_25.svf[10],
                         subp->sub4_25.svf[11], subp->sub4_25.svf[12],
                         subp->sub4_25.svf[13], subp->sub4_25.svf[14],
                         subp->sub4_25.svf[15], subp->sub4_25.svf[16],
                         subp->sub4_25.svf[17], subp->sub4_25.svf[18],
                         subp->sub4_25.svf[19], subp->sub4_25.svf[20],
                         subp->sub4_25.svf[21], subp->sub4_25.svf[22],
                         subp->sub4_25.svf[23], subp->sub4_25.svf[24],
                         subp->sub4_25.svf[25], subp->sub4_25.svf[26],
                         subp->sub4_25.svf[27], subp->sub4_25.svf[28],
                         subp->sub4_25.svf[29], subp->sub4_25.svf[30],
                         subp->sub4_25.svf[31], subp->sub4_25.svf[32],
                         subp->sub4_25.svhx[0], subp->sub4_25.svhx[1],
                         subp->sub4_25.svhx[2], subp->sub4_25.svhx[3],
                         subp->sub4_25.svhx[4], subp->sub4_25.svhx[5],
                         subp->sub4_25.svhx[6], subp->sub4_25.svhx[7]);
                break;

            default:                    // unkown page...
                ;                       /* no op */
            }
            if ( -1 < sv ) {
                subp->is_almanac = 1;
                subframe_almanac(&session->context->errout,
                                 subp->tSVID, words, subp->subframe_num,
                                 (uint8_t)sv, subp->data_id,
                                 &subp->sub4.almanac);
            } else if ( -2 == sv ) {
                /* unknown or secret page */
                GPSD_LOG(LOG_PROG, &session->context->errout,
                         "50B,GPS: SF:4-%d data_id %d\n",
                         subp->pageid, subp->data_id);
                return 0;
            }
            /* else, already handled */
        }
        break;
    case 5:
        /* Pages 0, dummy almanac for dummy SV 0
         * Pages 1 through 24: almanac data for SV 1 through 24
         * Page 25: SV health data for SV 1 through 24, the almanac
         * reference time, the almanac reference week number.
         */
        if (25 > subp->pageid) {
            subp->is_almanac = 1;
            subframe_almanac(&session->context->errout,
                             subp->tSVID, words, subp->subframe_num,
                             subp->pageid, subp->data_id, &subp->sub5.almanac);
        } else if (51 == subp->pageid) {
            /* for some inscrutable reason page 25 is sent as page 51
             * IS-GPS-200 Table 20-V */

            subp->sub5_25.toa   = ((words[2] >> 8) & 0x0000FF);
            subp->sub5_25.l_toa = subp->sub5_25.toa << 12;
            subp->sub5_25.WNa   = (words[2] & 0x0000FF);
            subp->sub5_25.sv[1] = ((words[3] >> 18) & 0x00003F);
            subp->sub5_25.sv[2] = ((words[3] >> 12) & 0x00003F);
            subp->sub5_25.sv[3] = ((words[3] >>  6) & 0x00003F);
            subp->sub5_25.sv[4] = ((words[3] >>  0) & 0x00003F);
            subp->sub5_25.sv[5] = ((words[4] >> 18) & 0x00003F);
            subp->sub5_25.sv[6] = ((words[4] >> 12) & 0x00003F);
            subp->sub5_25.sv[7] = ((words[4] >>  6) & 0x00003F);
            subp->sub5_25.sv[8] = ((words[4] >>  0) & 0x00003F);
            subp->sub5_25.sv[9] = ((words[5] >> 18) & 0x00003F);
            subp->sub5_25.sv[10] = ((words[5] >> 12) & 0x00003F);
            subp->sub5_25.sv[11] = ((words[5] >>  6) & 0x00003F);
            subp->sub5_25.sv[12] = ((words[5] >>  0) & 0x00003F);
            subp->sub5_25.sv[13] = ((words[6] >> 18) & 0x00003F);
            subp->sub5_25.sv[14] = ((words[6] >> 12) & 0x00003F);
            subp->sub5_25.sv[15] = ((words[6] >>  6) & 0x00003F);
            subp->sub5_25.sv[16] = ((words[6] >>  0) & 0x00003F);
            subp->sub5_25.sv[17] = ((words[7] >> 18) & 0x00003F);
            subp->sub5_25.sv[18] = ((words[7] >> 12) & 0x00003F);
            subp->sub5_25.sv[19] = ((words[7] >>  6) & 0x00003F);
            subp->sub5_25.sv[20] = ((words[7] >>  0) & 0x00003F);
            subp->sub5_25.sv[21] = ((words[8] >> 18) & 0x00003F);
            subp->sub5_25.sv[22] = ((words[8] >> 12) & 0x00003F);
            subp->sub5_25.sv[23] = ((words[8] >>  6) & 0x00003F);
            subp->sub5_25.sv[24] = ((words[8] >>  0) & 0x00003F);
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "50B,GPS: SF:5-25 SV:%2u ID:%u toa:%lu WNa:%u "
                     "SV1:%u SV2:%u SV3:%u SV4:%u "
                     "SV5:%u SV6:%u SV7:%u SV8:%u "
                     "SV9:%u SV10:%u SV11:%u SV12:%u "
                     "SV13:%u SV14:%u SV15:%u SV16:%u "
                     "SV17:%u SV18:%u SV19:%u SV20:%u "
                     "SV21:%u SV22:%u SV23:%u SV24:%u\n",
                     subp->tSVID, subp->data_id,
                     subp->sub5_25.l_toa, subp->sub5_25.WNa,
                     subp->sub5_25.sv[1], subp->sub5_25.sv[2],
                     subp->sub5_25.sv[3], subp->sub5_25.sv[4],
                     subp->sub5_25.sv[5], subp->sub5_25.sv[6],
                     subp->sub5_25.sv[7], subp->sub5_25.sv[8],
                     subp->sub5_25.sv[9], subp->sub5_25.sv[10],
                     subp->sub5_25.sv[11], subp->sub5_25.sv[12],
                     subp->sub5_25.sv[13], subp->sub5_25.sv[14],
                     subp->sub5_25.sv[15], subp->sub5_25.sv[16],
                     subp->sub5_25.sv[17], subp->sub5_25.sv[18],
                     subp->sub5_25.sv[19], subp->sub5_25.sv[20],
                     subp->sub5_25.sv[21], subp->sub5_25.sv[22],
                     subp->sub5_25.sv[23], subp->sub5_25.sv[24]);
        } else {
            /* unknown page */
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "50B,GPS: SF:5-%d data_id %d unknown page\n",
                     subp->pageid, subp->data_id);
            return 0;
        }
        break;
    default:
        /* unknown/illegal subframe */
        return 0;
    }
    return SUBFRAME_SET;
}

/* Stub of code to decode BeiDou Subframes
 *
 * for now only handles the 10 word subframe.
 *
 * http://en.beidou.gov.cn/SYSTEMS/ICD/
 * BeiDou Interface Control Document v1.0
 * See u-blox8-M8_ReceiverDescrProtSpec_UBX-13003221.pdf
 * Section 10.4 BeiDou
 * or
 * ZED-F9P_IntegrationManual_(UBX-18010802).pdf
 * Section 3.13.1.4 BeiDou
 * gotta decode the u-blox munging and the BeiDou packing...
 *
 * 10 words.  ignore top 2 bits, so 30 bits.  The 8 LSB are parity.
 * except words[0] is 4 parity
 */
static gps_mask_t subframe_bds(struct gps_device_t *session,
                               unsigned int tSVID,
                               uint32_t words[],
                               unsigned int numwords)
{
    gps_mask_t mask = 0;
    char *word_desc = "";
    unsigned FraID = (words[0] >> 12) & 7;
    unsigned SOW;
    struct subframe_t *subp = &session->gpsdata.subframe;
    long tmp;

    init_subframe(&session->gpsdata.subframe, GNSSID_BD, (uint8_t)tSVID);
    subp->subframe_num = FraID;

    SOW = ((words[0] >> 4) & 0x0ff) << 12;
    SOW |= (words[1] >> 18) & 0x0fff;
    subp->TOW17 = SOW;

    GPSD_LOG(LOG_DATA, &session->context->errout,
             "50B,BDS: len %u: "
             "%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
             numwords,
             words[0], words[1], words[2], words[3], words[4],
             words[5], words[6], words[7], words[8], words[9]);

    switch (FraID) {
    case 1:
        word_desc = "Ephemeris 1";
        subp->orbit.sv = tSVID;
        // subp->SatH1 = (words[1] >> 17) & 1;
        subp->orbit.AODC = (words[1] >> 12) & 0x01f;
        // subp->URAI = (words[1] >> 8) & 0x0f;
        subp->WN = (words[2] >> 17) & 0x01fff;
        subp->orbit.toc = ((words[2] >> 8) & 0x0ff) << 8;
        subp->orbit.toc |= (words[3] >> 24) & 0x0ff;
        subp->orbit.toc <<= 8;

        tmp = (words[3] >> 12) & 0x03ff;   // TGD1
        subp->orbit.TGD1 = uint2int(tmp, 10);

        tmp = ((words[3] >> 8) & 0x0f) << 6;   // TGD2
        tmp |= (words[4] >> 26) & 0x03f;
        subp->orbit.TGD2 = uint2int(tmp, 10);

        tmp = (words[4] >> 16) & 0x0ff;         // alpha0
        tmp = uint2int(tmp, 8);
        subp->orbit.alpha0 = tmp * pow(2.0, -30);

        tmp = (words[4] >> 8) & 0x0ff;          // alpha1
        tmp = uint2int(tmp, 8);
        subp->orbit.alpha1 = tmp * pow(2.0, -27);

        tmp = (words[5] >> 22) & 0x0ff;         // alpha2
        tmp = uint2int(tmp, 8);
        subp->orbit.alpha2 = tmp * pow(2.0, -24);

        tmp = (words[5] >> 14) & 0x0ff;         // alpha3
        tmp = uint2int(tmp, 8);
        subp->orbit.alpha3 = tmp * pow(2.0, -24);

        tmp = ((words[4] >> 8) & 0x03f) << 2;    // beta0
        tmp |= (words[5] >> 28) & 3;
        tmp = uint2int(tmp, 8);
        subp->orbit.beta0 = tmp << 14;

        tmp = (words[5] >> 20) & 0x0ff;          // beta1
        tmp = uint2int(tmp, 8);
        subp->orbit.beta1 = tmp << 14;

        tmp = (words[5] >> 12) & 0x0ff;          // beta2
        tmp = uint2int(tmp, 8);
        subp->orbit.beta2 = tmp << 16;

        tmp = ((words[5] >> 8) & 0x03f) << 4;    // beta3
        tmp |= (words[6] >> 26) & 0x0f;
        tmp = uint2int(tmp, 8);
        subp->orbit.beta3 = tmp << 16;

        tmp = (words[6] >> 15) & 0x03ff;          // a2
        tmp = uint2int(tmp, 11);
        subp->orbit.af2 = tmp * pow(2.0, -66);

        tmp = ((words[7] >> 8) & 0x03f) << 17;    // a0
        tmp |= (words[8] >> 13) & 0x01ffff;
        tmp = uint2int(tmp, 24);
        subp->orbit.af0 = tmp * pow(2.0, -33);

        tmp = ((words[8] >> 8) & 0x01f) << 17;    // a1
        tmp |= (words[9] >> 13) & 0x01ffff;
        tmp = uint2int(tmp, 22);
        subp->orbit.af1 = tmp * pow(2.0, -52);

        subp->orbit.AODE = (words[8] >> 8) & 0x01f;
        subp->is_almanac = SUBFRAME_ORBIT;
        subp->orbit.type = ORBIT_EPHEMERIS;
        mask = SUBFRAME_SET;
        break;
    case 2:
        word_desc = "Ephemeris 2";
        subp->is_almanac = SUBFRAME_ORBIT;
        subp->orbit.type = ORBIT_EPHEMERIS;
        break;
    case 3:
        word_desc = "Ephemeris 3";
        subp->is_almanac = SUBFRAME_ORBIT;
        subp->orbit.type = ORBIT_EPHEMERIS;
        break;
    case 4:
        {
            unsigned Pnum = (words[1] >> 10) & 0x7f;
            // unsigned AmEpID = (words[9] >> 8) & 3; // unused, for now
            if (1 <= Pnum && 24 >= Pnum) {
                long tmp;
                word_desc = "Almanac 1";
                subp->is_almanac = SUBFRAME_ORBIT;
                subp->orbit.type = ORBIT_ALMANAC;
                subp->orbit.sv = Pnum;

                tmp = ((words[1] >> 8) & 3) << 22;
                tmp |= (words[2] >> 8) & 0x03fffff;
                subp->orbit.sqrtA = tmp * pow(2.0, -11);

                tmp = (words[3] >> 19) & 0x07ff;
                tmp = uint2int(tmp, 11);
                subp->orbit.af1 = tmp * pow(2.0, -38);

                tmp = (words[3] >> 8) & 0x07ff;
                tmp = uint2int(tmp, 11);
                subp->orbit.af0 = tmp * pow(2.0, -20);

                tmp = ((words[4] >> 8) & 0x3fffff) << 2;
                tmp |= (words[5] >> 28) & 3;
                tmp = uint2int(tmp, 24);
                subp->orbit.Omega0 = tmp * pow(2.0, -23);

                tmp = (words[5] >> 11) & 0x001ffff;
                subp->orbit.eccentricity = tmp * pow(2.0, -21);

                tmp = ((words[5] >> 3) & 0x03f) << 13;
                tmp |= (words[6] >> 17) & 0x01ffff;
                tmp = uint2int(tmp, 16);
                subp->orbit.i0 = tmp * pow(2.0, -19);
                // also convert deltai to i0
                if ((1 <= subp->orbit.sv && 6 >= subp->orbit.sv) ||
                    (59 <= subp->orbit.sv && 63 >= subp->orbit.sv)) {
                    // GEO sats add 0, MEO/IGSO adding 0.30
                    subp->orbit.i0 = tmp * pow(2.0, -19);
                }

                subp->orbit.toa = ((words[6] >> 9) & 0x0ff) << 12;  // toa

                tmp = ((words[6] >> 8) & 1) << 16;
                tmp |= (words[7] >> 14) & 0x0ffff;
                subp->orbit.Omegad = tmp * pow(2.0, -38);

                tmp = ((words[7] >> 8) & 0x03f) << 18;
                tmp |= (words[8] >> 12) & 0x03ffff;
                tmp = uint2int(tmp, 24);
                subp->orbit.omega = tmp * pow(2.0, -23);

                tmp = ((words[8] >> 8) & 0x0f) << 20;
                tmp |= (words[9] >> 10) & 0x0fffff;
                tmp = uint2int(tmp, 24);
                subp->orbit.M0 = tmp * pow(2.0, -23);

                mask = SUBFRAME_SET;
            } else {
                word_desc = "Reserved";
            }
        }
        break;
    case 5:
        {
            unsigned Pnum = (words[1] >> 10) & 0x7f;
            unsigned AmEpID = (words[9] >> 8) & 3;
            if ((1 <= Pnum && 6 >= Pnum) || (11 <= Pnum && 23 >= Pnum)) {
                if (3 == AmEpID) {
                    word_desc = "Almanac 2";
                    subp->is_almanac = SUBFRAME_ORBIT;
                    subp->orbit.type = ORBIT_EPHEMERIS;
                } else {
                    word_desc = "Reserved";
                }
            } else if (7 == Pnum) {
                word_desc = "Health 1";
            } else if (8 == Pnum) {
                word_desc = "Health 20";
            } else if (9 == Pnum) {
                word_desc = "GST-GPS";
            } else if (10 == Pnum) {
                word_desc = "GST-UTC";
            } else {
                word_desc = "Other";
            }
        }
        break;
    default:
        word_desc = "Unknown FraID";
        break;
    }

    GPSD_LOG(LOG_PROG, &session->context->errout,
             "50B,BDS: FraID %u (%s) SOW %u\n",
             FraID, word_desc, SOW);

    return mask;
}

/* Stub of code to decode Galileo Subframes
 *
 * for now only handles the 8 word subframe.
 *
 * Galileo_OS_SIS_ICD_v2.0.pdf
 * See u-blox8-M8_ReceiverDescrProtSpec_UBX-13003221.pdf
 * Section 10.5 Galileo
 * gotta decode the u-blox munging and the Galileo packing...
 * porting to none u-blox will require separate mungin
 */
static gps_mask_t subframe_gal(struct gps_device_t *session,
                               unsigned int tSVID,
                               uint32_t words[],
                               unsigned int numwords)
{
    gps_mask_t mask = 0;
    char *word_desc = "";
    // always zero on E5b-I, always 1 on E1-B
    unsigned even = words[0] >> 31;
    // # zero for nominal page, one for alert page
    unsigned page_type;
    unsigned word_type;
    struct subframe_t *subp;
    long tmp;

    if (8 > numwords) {
        // Later on there will be different lengths than 8.
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "50B,GAL: expected 8 words, got %u\n",
                 numwords);
        return 0;
    }
    page_type = (words[0] >> 30) & 1;
    word_type = (words[0] >> 24) & 0x03f;

    GPSD_LOG(LOG_DATA, &session->context->errout,
             "50B,GAL: tSVID %u len %u: "
             "%08x %08x %08x %08x %08x %08x %08x %08x\n", tSVID, numwords,
             words[0], words[1], words[2], words[3], words[4],
             words[5], words[6], words[7]);

    if (1 == page_type) {
        // Alerts pages are all "Reserved"
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "50B,GAL: ignoring Alert Page \n");
        return 0;
    }
    if (1 == even) {
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "50B,GAL: page flipped?\n");
        return 0;
    }
    subp = &session->gpsdata.subframe;
    init_subframe(subp, GNSSID_GAL, (uint8_t)tSVID);
    subp->subframe_num = word_type;
    subp->pageid = word_type;

    switch (word_type) {
    case 0:
        word_desc = "Spare Word";
        subp->WN = (words[3] >> 18) & 0x0fff;               // WN
        subp->TOW17 = ((words[3] >> 14) & 0x0f) << 16;      // TOW
        subp->TOW17 |= (words[4] >> 14) & 0x0ffff;
        mask = SUBFRAME_SET;
        break;
    case 1:
        word_desc = "Ephemeris 1";
        break;
    case 2:
        word_desc = "Ephemeris 2";
        break;
    case 3:
        word_desc = "Ephemeris 3";
        break;
    case 4:
        word_desc = "Ephemeris 4";
        break;
    case 5:
        word_desc = "Ionosphere";
        subp->WN = (words[2] >> 9) & 0x0fff;                 // WN
        subp->TOW17 = (words[2] & 0x01ff) << 11;             // TOW
        subp->TOW17 |= (words[3] >> 21) & 0x07ff;
        mask = SUBFRAME_SET;
        break;
    case 6:
        word_desc = "GST-UTC";
        subp->TOW17 = ((words[3] >> 14) & 0x07f) << 13;       // TOW
        subp->TOW17 |= (words[4] >> 17) & 0x01fff;
        mask = SUBFRAME_SET;
        break;
    case 7:
        word_desc = "Almanacs 1";
        subp->is_almanac = SUBFRAME_ORBIT;
        subp->orbit.type = ORBIT_ALMANAC;
        subp->orbit.IODA = (words[0] >> 20) & 0x0f;            // IODa
        subp->orbit.WN = (words[0] >> 18) & 3;                // WNa
        subp->orbit.toa = ((words[0] >> 8) & 0x03ff) * 600;   // toa
        subp->orbit.sv = (words[0] >> 2) & 0x03f;             // SVN1
        if (0 == subp->orbit.sv || 36 < subp->orbit.sv) {
            // dummy, or reserved, almanac
            break;
        }
        tmp = (words[0] & 3) << 11;         // delta sqrtA ?
        tmp |= (words[1] >> 21) & 0x07ff;
        tmp = uint2int(tmp, 13);
        // Table 1 from ICD
        subp->orbit.sqrtA = (tmp * pow(2.0, -9)) + sqrt(29600000);

        tmp = (words[1] >> 10)  & 0x03ff;   // e
        subp->orbit.eccentricity = tmp * pow(2.0, -16);

        tmp = (words[1] & 0x03ff) << 6;         // omega
        tmp |= (words[2] >> 26) & 0x03f;
        tmp = uint2int(tmp, 16);
        subp->orbit.omega = tmp * pow(2.0, -15);

        tmp = (words[2] >> 11) & 0x07ff;  // deltai
        tmp = uint2int(tmp, 11);
        // Table 1 from ICD
        subp->orbit.i0 = (tmp * pow(2.0, -14)) + (56.0 / 180.0);

        tmp = (words[2] & 0x07fff) << 1;        // Omega0
        tmp |= (words[3] >> 31) & 1;
        tmp = uint2int(tmp, 16);
        subp->orbit.Omega0 = tmp * pow(2.0, -35);

        tmp = (words[3] >> 20) & 0x07fff;     // Omegadot
        tmp = uint2int(tmp, 11);
        subp->orbit.Omegad = tmp * pow(2.0, -33);

        tmp = ((words[3] >> 14) & 0x03f) << 10;     // M0
        tmp |= (words[4] >> 20) & 0x03ff;
        tmp = uint2int(tmp, 16);
        subp->orbit.M0 = tmp * pow(2.0, -15);

        mask = SUBFRAME_SET;
        break;
    case 8:
        // Now it gets weird.  2/2 of Almanac 1, and 1/2 of Almanac 2
        word_desc = "Almanacs 2";
        subp->orbit1.sv = (words[1] >> 11) & 0x03f;             // SVID2
        if (0 >= subp->orbit1.sv || 36 < subp->orbit1.sv) {
            // dummy, or reserved, almanac
            break;
        }
        subp->is_almanac = SUBFRAME_ORBIT;
        subp->orbit.type = ORBIT_ALMANAC;

        // how do we know the SVID1?  It is one less.
        subp->orbit.sv = subp->orbit1.sv - 1;

        subp->orbit.IODA = (words[0] >> 20) & 0x0f;    // IODa

        tmp = (words[0] >> 4) & 0x0ffff;              // af0
        tmp = uint2int(tmp, 16);
        subp->orbit.af0 = tmp * pow(2.0, -19);

        tmp = (words[0] & 0x0f) << 9;                 // af1
        tmp |= (words[1] >> 23) & 0x01ff;
        tmp = uint2int(tmp, 13);
        subp->orbit.af1 = tmp * pow(2.0, -38);

        subp->orbit.E5bHS = (words[1] >> 21) & 3;
        subp->orbit.E1BHS = (words[1] >> 19) & 3;

        // start of 2nd SV
        subp->orbit1.type = ORBIT_ALMANAC;
        subp->orbit1.IODA = subp->orbit.IODA;     // IODa
        tmp = (words[1] >> 5) & 0x07ff;         // delta sqrtA ?
        tmp = uint2int(tmp, 13);
        // Table 1 from ICD
        subp->orbit1.sqrtA = (tmp * pow(2.0, -9)) + sqrt(29600000);

        tmp = (words[2] >> 21)  & 0x07ff;       // e
        subp->orbit1.eccentricity = tmp * pow(2.0, -16);

        tmp = (words[2] >> 5) & 0x0ffff;        // omega
        tmp = uint2int(tmp, 16);
        subp->orbit1.omega = tmp * pow(2.0, -15);

        tmp = (words[2] & 0x01f) << 6;        // deltai
        tmp |= (words[3] >> 26) & 0x3f;
        tmp = uint2int(tmp, 11);
        // Table 1 from ICD
        subp->orbit1.i0 = (tmp * pow(2.0, -14)) + (56.0 / 180.0);

        tmp = (words[3] & 0x0fff) << 4;        // Omega0
        tmp |= (words[3] >> 12) & 0x0f;
        tmp = uint2int(tmp, 16);
        subp->orbit1.Omega0 = tmp * pow(2.0, -35);

        tmp = (words[3] >> 1) & 0x07fff;     // Omegadot
        tmp = uint2int(tmp, 11);
        subp->orbit1.Omegad = tmp * pow(2.0, -33);

        mask = SUBFRAME_SET;
        break;
    case 9:
        word_desc = "Almanacs 3";
        subp->orbit1.sv = (words[2] >> 17) & 0x03f;             // SVID3
        if (0 >= subp->orbit1.sv || 36 < subp->orbit1.sv) {
            // dummy, or reserved, almanac
            break;
        }
        subp->is_almanac = SUBFRAME_ORBIT;
        subp->orbit.type = ORBIT_ALMANAC;

        // how do we know the SVID2?  It is one less.
        subp->orbit.sv = subp->orbit1.sv - 1;
        subp->is_almanac = SUBFRAME_ORBIT;
        subp->orbit.type = ORBIT_ALMANAC;
        subp->orbit.IODA = (words[0] >> 20) & 0x0f;            // IODa
        subp->orbit.WN = (words[0] >> 18) & 3;                // WNa
        subp->orbit.toa = ((words[0] >> 8) & 0x03ff) * 600;  // toa

        tmp = (words[0] & 0x0ff) << 8;     // M0
        tmp |= (words[1] >> 24) & 0x0ff;
        tmp = uint2int(tmp, 16);
        subp->orbit.M0 = tmp * pow(2.0, -15);

        tmp = (words[1] >> 8) & 0x0ffff;              // af0
        tmp = uint2int(tmp, 16);
        subp->orbit.af0 = tmp * pow(2.0, -19);

        tmp = (words[1] & 0x0ff) << 8;                // af1
        tmp |= (words[2] >> 27) & 0x01f;
        tmp = uint2int(tmp, 13);
        subp->orbit.af1 = tmp * pow(2.0, -38);

        subp->orbit.E5bHS = (words[2] >> 25) & 3;
        subp->orbit.E1BHS = (words[2] >> 23) & 3;

        // SVID3
        subp->orbit1.type = ORBIT_ALMANAC;
        subp->orbit1.IODA = subp->orbit.IODA;     // IODa
        tmp = (words[2] >> 6) & 0x07ff;         // delta sqrtA ?
        tmp = uint2int(tmp, 13);
        // Table 1 from ICD
        subp->orbit1.sqrtA = (tmp * pow(2.0, -9)) + sqrt(29600000);

        tmp = (words[2] & 0x0f) << 7;                // af1
        tmp |= (words[3] >> 11) & 0x03f;
        subp->orbit1.eccentricity = tmp * pow(2.0, -16);

        tmp = (words[3] & 0x07ff) << 5;         // omega
        tmp |= (words[4] >> 11) & 0x01f;
        tmp = uint2int(tmp, 16);
        subp->orbit1.omega = tmp * pow(2.0, -15);

        tmp = (words[4] >> 26) & 0x3f;
        tmp = uint2int(tmp, 11);
        // Table 1 from ICD
        subp->orbit1.i0 = (tmp * pow(2.0, -14)) + (56.0 / 180.0);

        mask = SUBFRAME_SET;
        break;
    case 10:
        word_desc = "Almanacs 4";
        subp->is_almanac = SUBFRAME_ORBIT;
        subp->orbit.type = ORBIT_ALMANAC;

        // how do we know the SVID3?
        tmp = (words[0] >> 4) & 0x0ff;        // Omega0
        tmp = uint2int(tmp, 16);
        subp->orbit.Omega0 = tmp * pow(2.0, -35);

        tmp = (words[3] & 0x0f) << 7;         // Omegadot
        tmp |= (words[3] >> 25) & 0x07f;
        tmp = uint2int(tmp, 11);
        subp->orbit.Omegad = tmp * pow(2.0, -33);

        tmp = (words[1] >> 9) & 0x0ffff;       // M0
        tmp = uint2int(tmp, 16);
        subp->orbit.M0 = tmp * pow(2.0, -15);

        tmp = (words[1] & 0x01ff) << 7;              // af0
        tmp |= (words[2] >> 25) & 0x07f;
        tmp = uint2int(tmp, 16);
        subp->orbit.af0 = tmp * pow(2.0, -19);

        tmp = (words[2] >> 12) & 0x01fff;            // af1
        tmp = uint2int(tmp, 13);
        subp->orbit.af1 = tmp * pow(2.0, -38);

        break;
    case 16:
        word_desc = "Reduced Clock and Ephemeris Data";
        break;
    case 17:
        word_desc = "FEC2 Reed-Solomon for Clock and Ephemeris Data";
        break;
    case 63:
        word_desc = "Dummy Page";
        break;
    default:
        word_desc = "Unknown Word";
        break;
    }

    GPSD_LOG(LOG_PROG, &session->context->errout,
             "50B,GAL: len %u even %u page_type %u word_type %u (%s)\n",
             numwords, even, page_type, word_type, word_desc);

    return mask;
}


/* Stub of code to decode GLONASS Subframes
 *
 * ZED-F9P_IntegrationManual_(UBX-18010802).pdf
 * Section 3.13.1.3 GLONASS
 * L10F and L20F only
 * ICD_GLONASS_5.1_(2008)_en.pdf "ICD L1, L2 GLONASS"
 * gotta decode the u-blox munging and the GLONASS packing...
 *
 * 4 words
 */
static gps_mask_t subframe_glo(struct gps_device_t *session,
                               unsigned int tSVID,
                               uint32_t words[],
                               unsigned int numwords)
{
    char *word_desc = "";
    struct subframe_t *subp;
    unsigned stringnum = (words[0] >> 27) & 0x0f;
    unsigned supernum = (words[3] >> 16) & 0x0f;
    unsigned framenum = words[3] & 0x0f;

    subp = &session->gpsdata.subframe;
    init_subframe(subp, GNSSID_GLO, (uint8_t)tSVID);

    GPSD_LOG(LOG_DATA, &session->context->errout,
             "50B,GLO: tSVID %u len %u: "
             "%08x %08x %08x %08x\n", tSVID, numwords,
             words[0], words[1], words[2], words[3]);

    switch (stringnum) {
    case 1:
        word_desc = "Ephemeris 1";
        break;
    case 2:
        word_desc = "Ephemeris 2";
        break;
    case 3:
        word_desc = "Ephemeris 3";
        break;
    case 4:
        word_desc = "Ephemeris 4";
        break;
    case 5:
        word_desc = "Time";
        break;
    case 6:
        FALLTHROUGH
    case 8:
        FALLTHROUGH
    case 10:
        FALLTHROUGH
    case 12:
        FALLTHROUGH
    case 14:
        if (5 == framenum) {
            word_desc = "Extra 1";
        } else {
            word_desc = "Almanac 1";
        }
        break;
    case 7:
        FALLTHROUGH
    case 9:
        FALLTHROUGH
    case 11:
        FALLTHROUGH
    case 13:
        FALLTHROUGH
    case 15:
        if (5 == framenum) {
            word_desc = "Extra 2";
        } else {
            word_desc = "Almanac 2";
        }
        break;
    default:
        word_desc = "Unknown stringnum";
        break;
    }
    GPSD_LOG(LOG_PROG, &session->context->errout,
             "50B,GLO: len %u supernum %u framenum %u stringnum %u (%s)\n",
             numwords, supernum, framenum, stringnum, word_desc);
    return 0;
}


gps_mask_t gpsd_interpret_subframe_raw(struct gps_device_t *session,
                                       unsigned int gnssId,
                                       unsigned int tSVID,
                                       uint32_t words[],
                                       unsigned int numwords)
{
    unsigned int i;
    uint8_t preamble;
    unsigned int numwords_expected = 0;

    if (session->subframe_count++ == 0) {
        speed_t speed = gpsd_get_speed(session);

        if (speed < 38400) {
            GPSD_LOG(LOG_WARN, &session->context->errout,
                     "50B: speed less than 38,400 may cause data lag and "
                     "loss of functionality\n");
        }
    }

    switch (gnssId) {
    case GNSSID_GPS:
        FALLTHROUGH
    case GNSSID_QZSS:
        // GPS and QZSS use the same subframe structure
        numwords_expected = 10;
        break;
    case GNSSID_SBAS:
        GPSD_LOG(LOG_INFO, &session->context->errout,
                 "50B,SBAS: subframe protocol is not publicly documented");
        return 0;
    case GNSSID_GAL:
        numwords_expected = 8;
        if (numwords_expected == numwords) {
            return subframe_gal(session, tSVID, words, numwords);
        }
        break;

    case GNSSID_BD:
        numwords_expected = 10;
        if (numwords_expected == numwords) {
            return subframe_bds(session, tSVID, words, numwords);
        }
        break;
    case GNSSID_GLO:
        numwords_expected = 4;
        if (numwords_expected == numwords) {
            return subframe_glo(session, tSVID, words, numwords);
        }
        break;
    case GNSSID_IMES:
        FALLTHROUGH
    case GNSSID_IRNSS:
        FALLTHROUGH
    default:
        GPSD_LOG(LOG_INFO, &session->context->errout,
                 "50B: Unsupportd gnssId %u\n", gnssId);
        return 0;
    }

    if (numwords_expected > numwords) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "50B: gnssId %u  Expected numwords %u, got %u\n",
                 gnssId, numwords_expected, numwords);
        return 0;
    }

    /*
     * This function assumes an array of 10 ints, each of which carries
     * a raw 30-bit GPS word use your favorite search engine to find the
     * latest version of the specification: IS-GPS-200.
     *
     * Each raw 30-bit word is made of 24 data bits and 6 parity bits. The
     * raw word and transport word are emitted from the GPS MSB-first and
     * right justified. In other words, masking the raw word against 0x3f
     * will return just the parity bits. Masking with 0x3fffffff and shifting
     * 6 bits to the right returns just the 24 data bits. The top two bits
     * (b31 and b30) are undefined; chipset designers may store copies of
     * the bits D29* and D30* here to aid parity checking.
     *
     * Since bits D29* and D30* are not available in word 0, it is tested for
     * a known preamble to help check its validity and determine whether the
     * word is inverted.
     *
     */
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "50B,GPS: gpsd_interpret_subframe_raw: "
             "%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
             words[0], words[1], words[2], words[3], words[4],
             words[5], words[6], words[7], words[8], words[9]);

    preamble = (uint8_t)((words[0] >> 22) & 0xFF);
    if (preamble == 0x8b) {     /* preamble is inverted */
        words[0] ^= 0x3fffffc0; /* invert */
    } else if (preamble != 0x74) {
        /* strangely this is very common, so don't log it */
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "50B,GPS: gpsd_interpret_subframe_raw: bad preamble 0x%x\n",
                 preamble);
        return 0;
    }
    words[0] = (words[0] >> 6) & 0xffffff;

    for (i = 1; i < 10; i++) {
        int invert;
        uint32_t parity;
        /* D30* says invert */
        invert = (words[i] & 0x40000000) ? 1 : 0;
        /* inverted data, invert it back */
        if (invert) {
            words[i] ^= 0x3fffffc0;
        }
        parity = (uint32_t)isgps_parity((isgps30bits_t)words[i]);
        if (parity != (words[i] & 0x3f)) {
            GPSD_LOG(LOG_DATA, &session->context->errout,
                     "50B,GPS: gpsd_interpret_subframe_raw parity fail "
                     "words[%d] 0x%x != 0x%x\n",
                     i, parity, (words[i] & 0x1));
            return 0;
        }
        words[i] = (words[i] >> 6) & 0xffffff;
    }

    return gpsd_interpret_subframe(session, gnssId, tSVID, words);
}

// vim: set expandtab shiftwidth=4
